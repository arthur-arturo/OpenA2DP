#include <windows.h>
#include <avrt.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <propidl.h>
#include <propsys.h>
#include <wrl/client.h>

#include <ldacBT.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace std::chrono_literals;

namespace {

constexpr WORD kChannelCount = 2;
constexpr int kLdacMtuForPoc = 679;
constexpr size_t kSamplesPerLdacChannel = LDACBT_ENC_LSU;
constexpr size_t kSamplesPerLdacBlock =
    kSamplesPerLdacChannel * kChannelCount;
constexpr UINT32 kPcmQueueCapacityFrames = 4096;
constexpr REFERENCE_TIME kWasapiBufferDuration = 200000; // 20 ms
constexpr DWORD kCaptureFlags =
    AUDCLNT_STREAMFLAGS_LOOPBACK |
    AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
    AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
    AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

// PKEY_Device_FriendlyName from functiondiscoverykeys_devpkey.h. Keeping the
// key local avoids requiring DEFINE_PROPERTYKEY/INITGUID or a GUID import lib.
constexpr PROPERTYKEY kPkeyDeviceFriendlyName{
    {0xA45C254E, 0xDF1C, 0x4EFD,
     {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}},
    14,
};

class HResultError final : public std::runtime_error {
public:
    HResultError(const char* operation, HRESULT result)
        : std::runtime_error(makeMessage(operation, result)), result_(result) {}

    HRESULT result() const noexcept { return result_; }

private:
    static std::string makeMessage(const char* operation, HRESULT result) {
        char message[256]{};
        std::snprintf(message, sizeof(message), "%s (HRESULT 0x%08lX)",
                      operation, static_cast<unsigned long>(result));
        return message;
    }

    HRESULT result_;
};

void checkHr(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        throw HResultError(operation, result);
    }
}

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class ComApartment final {
public:
    ComApartment() {
        checkHr(CoInitializeEx(nullptr, COINIT_MULTITHREADED),
                "CoInitializeEx");
    }

    ~ComApartment() { CoUninitialize(); }

    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;
};

class WinEvent final {
public:
    explicit WinEvent(bool manualReset = false)
        : handle_(CreateEventW(nullptr, manualReset ? TRUE : FALSE, FALSE,
                               nullptr)) {
        check(handle_ != nullptr, "CreateEventW failed");
    }

    ~WinEvent() {
        if (handle_) {
            CloseHandle(handle_);
        }
    }

    WinEvent(const WinEvent&) = delete;
    WinEvent& operator=(const WinEvent&) = delete;

    HANDLE get() const noexcept { return handle_; }

    void signal() const {
        check(SetEvent(handle_) != FALSE, "SetEvent failed");
    }

private:
    HANDLE handle_ = nullptr;
};

class MmcssScope final {
public:
    explicit MmcssScope(const wchar_t* taskName) {
        DWORD taskIndex = 0;
        handle_ = AvSetMmThreadCharacteristicsW(taskName, &taskIndex);
    }

    ~MmcssScope() {
        if (handle_) {
            AvRevertMmThreadCharacteristics(handle_);
        }
    }

    bool active() const noexcept { return handle_ != nullptr; }

    MmcssScope(const MmcssScope&) = delete;
    MmcssScope& operator=(const MmcssScope&) = delete;

private:
    HANDLE handle_ = nullptr;
};

struct Options {
    bool listDevices = false;
    std::wstring deviceId;
    std::filesystem::path outputPath = L"capture.oa2dpcap";
    int sampleRate = 48000;
    int durationSeconds = 30;
    int quality = LDACBT_EQMID_HQ;
};

std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t character) {
                       return static_cast<wchar_t>(std::towlower(character));
                   });
    return value;
}

unsigned long long parseUnsigned(const wchar_t* value, const wchar_t* option) {
    wchar_t* end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::wcstoull(value, &end, 10);
    if (value == end || *end != L'\0' || errno == ERANGE ||
        parsed > static_cast<unsigned long long>(
                     std::numeric_limits<int>::max())) {
        std::wstring message = L"Valor no válido para ";
        message += option;
        throw std::runtime_error(std::string(message.begin(), message.end()));
    }
    return parsed;
}

void printUsage() {
    std::wprintf(
        L"OpenA2DP 0.1.0 - PoC WASAPI Loopback + libldac\n\n"
        L"Uso:\n"
        L"  opena2dp.exe --list-devices\n"
        L"  opena2dp.exe [opciones]\n\n"
        L"Opciones:\n"
        L"  --device-id ID      Endpoint de renderizado. Por defecto: multimedia.\n"
        L"  --rate HZ           44100, 48000, 88200 o 96000 (por defecto 48000).\n"
        L"  --quality MODO      hq, sq o mq (por defecto hq).\n"
        L"  --duration SEG      Duración de captura, 1..3600 (por defecto 30).\n"
        L"  --output ARCHIVO    Salida .oa2dpcap (por defecto capture.oa2dpcap).\n"
        L"  --help              Mostrar esta ayuda.\n\n"
        L"La salida es una captura de paquetes LDAC de prueba; no es A2DP/RTP.\n");
}

Options parseArguments(int argc, wchar_t** argv) {
    Options options;

    auto requireValue = [&](int& index, const wchar_t* option) -> const wchar_t* {
        if (index + 1 >= argc) {
            std::wstring message = L"Falta valor para ";
            message += option;
            throw std::runtime_error(std::string(message.begin(), message.end()));
        }
        return argv[++index];
    };

    for (int i = 1; i < argc; ++i) {
        const std::wstring argument = lower(argv[i]);

        if (argument == L"--help" || argument == L"-h" || argument == L"/?") {
            printUsage();
            std::exit(0);
        } else if (argument == L"--list-devices") {
            options.listDevices = true;
        } else if (argument == L"--device-id") {
            options.deviceId = requireValue(i, L"--device-id");
        } else if (argument == L"--output") {
            options.outputPath = requireValue(i, L"--output");
        } else if (argument == L"--rate") {
            options.sampleRate = static_cast<int>(
                parseUnsigned(requireValue(i, L"--rate"), L"--rate"));
        } else if (argument == L"--duration") {
            options.durationSeconds = static_cast<int>(
                parseUnsigned(requireValue(i, L"--duration"), L"--duration"));
        } else if (argument == L"--quality") {
            const std::wstring quality = lower(requireValue(i, L"--quality"));
            if (quality == L"hq") {
                options.quality = LDACBT_EQMID_HQ;
            } else if (quality == L"sq") {
                options.quality = LDACBT_EQMID_SQ;
            } else if (quality == L"mq") {
                options.quality = LDACBT_EQMID_MQ;
            } else {
                throw std::runtime_error("--quality debe ser hq, sq o mq");
            }
        } else {
            throw std::runtime_error("Opcion de linea de comandos desconocida");
        }
    }

    check(options.sampleRate == 44100 || options.sampleRate == 48000 ||
              options.sampleRate == 88200 || options.sampleRate == 96000,
          "LDAC admite 44100, 48000, 88200 o 96000 Hz");
    check(options.durationSeconds >= 1 && options.durationSeconds <= 3600,
          "--duration debe estar entre 1 y 3600 segundos");
    check(!options.outputPath.empty(), "--output no puede estar vacío");
    return options;
}

std::wstring getFriendlyName(IMMDevice* device) {
    ComPtr<IPropertyStore> properties;
    checkHr(device->OpenPropertyStore(STGM_READ, properties.GetAddressOf()),
            "IMMDevice::OpenPropertyStore");

    PROPVARIANT value;
    PropVariantInit(&value);
    const HRESULT result = properties->GetValue(kPkeyDeviceFriendlyName, &value);

    std::wstring name = L"(sin nombre)";
    if (SUCCEEDED(result) && value.vt == VT_LPWSTR && value.pwszVal) {
        name = value.pwszVal;
    }

    PropVariantClear(&value);
    return name;
}

void listRenderDevices() {
    ComApartment apartment;
    ComPtr<IMMDeviceEnumerator> enumerator;
    checkHr(CoCreateInstance(
                __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                IID_PPV_ARGS(enumerator.GetAddressOf())),
            "CoCreateInstance(MMDeviceEnumerator)");

    ComPtr<IMMDeviceCollection> devices;
    checkHr(enumerator->EnumAudioEndpoints(
                eRender, DEVICE_STATE_ACTIVE, devices.GetAddressOf()),
            "EnumAudioEndpoints");

    UINT count = 0;
    checkHr(devices->GetCount(&count), "IMMDeviceCollection::GetCount");
    if (count == 0) {
        std::wprintf(L"No hay endpoints de reproducción activos.\n");
        return;
    }

    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        checkHr(devices->Item(i, device.GetAddressOf()),
                "IMMDeviceCollection::Item");

        LPWSTR id = nullptr;
        checkHr(device->GetId(&id), "IMMDevice::GetId");
        const std::wstring name = getFriendlyName(device.Get());

        std::wprintf(L"%u. %ls\n   ID: %ls\n\n", i + 1, name.c_str(), id);
        CoTaskMemFree(id);
    }
}

class PcmSpscQueue final {
public:
    PcmSpscQueue()
        : storage_(static_cast<size_t>(kPcmQueueCapacityFrames) *
                   kChannelCount) {
        static_assert((kPcmQueueCapacityFrames &
                       (kPcmQueueCapacityFrames - 1)) == 0,
                      "PCM queue capacity must be a power of two");
        static_assert(std::atomic<uint64_t>::is_always_lock_free,
                      "The real-time PCM queue requires lock-free 64-bit atomics");
    }

    // Called only by the WASAPI producer thread. It never allocates or waits.
    bool tryPush(const float* source, UINT32 frames, bool silent) noexcept {
        if (frames == 0) {
            return true;
        }

        const uint64_t write = writeFrame_.load(std::memory_order_relaxed);
        const uint64_t read = readFrame_.load(std::memory_order_acquire);
        const uint64_t queued = write - read;
        if (queued > kPcmQueueCapacityFrames ||
            frames > kPcmQueueCapacityFrames - queued) {
            return false;
        }

        const size_t mask = kPcmQueueCapacityFrames - 1;
        for (UINT32 frame = 0; frame < frames; ++frame) {
            const size_t slot =
                static_cast<size_t>((write + frame) & mask) * kChannelCount;
            const size_t sourceIndex = static_cast<size_t>(frame) * kChannelCount;
            storage_[slot] = silent ? 0.0f : source[sourceIndex];
            storage_[slot + 1] = silent ? 0.0f : source[sourceIndex + 1];
        }

        writeFrame_.store(write + frames, std::memory_order_release);
        return true;
    }

    bool tryPopFullBlock(float* destination) noexcept {
        const uint64_t read = readFrame_.load(std::memory_order_relaxed);
        const uint64_t write = writeFrame_.load(std::memory_order_acquire);
        if (write - read < kSamplesPerLdacChannel) {
            return false;
        }

        copyFrames(read, static_cast<UINT32>(kSamplesPerLdacChannel), destination);
        readFrame_.store(read + kSamplesPerLdacChannel,
                         std::memory_order_release);
        return true;
    }

    // At end of capture, pad the final partial LDAC input block with silence.
    bool tryPopFinalBlock(float* destination) noexcept {
        const uint64_t read = readFrame_.load(std::memory_order_relaxed);
        const uint64_t write = writeFrame_.load(std::memory_order_acquire);
        const uint64_t available = write - read;
        if (available == 0) {
            return false;
        }

        const UINT32 frames = static_cast<UINT32>(
            std::min<uint64_t>(available, kSamplesPerLdacChannel));
        copyFrames(read, frames, destination);

        std::fill(destination + static_cast<size_t>(frames) * kChannelCount,
                  destination + kSamplesPerLdacBlock, 0.0f);

        readFrame_.store(read + frames, std::memory_order_release);
        return true;
    }

    bool empty() const noexcept {
        return readFrame_.load(std::memory_order_acquire) ==
               writeFrame_.load(std::memory_order_acquire);
    }

private:
    void copyFrames(uint64_t firstFrame, UINT32 frames,
                    float* destination) noexcept {
        const size_t mask = kPcmQueueCapacityFrames - 1;
        for (UINT32 frame = 0; frame < frames; ++frame) {
            const size_t slot =
                static_cast<size_t>((firstFrame + frame) & mask) * kChannelCount;
            const size_t destinationIndex =
                static_cast<size_t>(frame) * kChannelCount;
            destination[destinationIndex] = storage_[slot];
            destination[destinationIndex + 1] = storage_[slot + 1];
        }
    }

    std::vector<float> storage_;
    alignas(64) std::atomic<uint64_t> writeFrame_{0};
    alignas(64) std::atomic<uint64_t> readFrame_{0};
};

struct LdacDeleter {
    bool initialized = false;

    void operator()(std::remove_pointer_t<HANDLE_LDAC_BT>* handle) const {
        if (handle) {
            if (initialized) {
                ldacBT_close_handle(handle);
            }
            ldacBT_free_handle(handle);
        }
    }
};

using LdacHandle =
    std::unique_ptr<std::remove_pointer_t<HANDLE_LDAC_BT>, LdacDeleter>;

LdacHandle createLdacEncoder(const Options& options) {
    LdacHandle handle(ldacBT_get_handle());
    check(static_cast<bool>(handle), "ldacBT_get_handle failed");

    const int result = ldacBT_init_handle_encode(
        handle.get(),
        kLdacMtuForPoc,
        options.quality,
        LDACBT_CHANNEL_MODE_STEREO,
        LDACBT_SMPL_FMT_F32,
        options.sampleRate);

    check(result == 0, "ldacBT_init_handle_encode failed");
    handle.get_deleter().initialized = true;
    return handle;
}

void writeU16(std::ofstream& output, uint16_t value) {
    const char bytes[2] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff)
    };
    output.write(bytes, sizeof(bytes));
    check(static_cast<bool>(output), "Error escribiendo la captura");
}

void writeU32(std::ofstream& output, uint32_t value) {
    const char bytes[4] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
        static_cast<char>((value >> 16) & 0xff),
        static_cast<char>((value >> 24) & 0xff)
    };
    output.write(bytes, sizeof(bytes));
    check(static_cast<bool>(output), "Error escribiendo la captura");
}

void writeCaptureHeader(std::ofstream& output, const Options& options) {
    // Little-endian format: OA2DPCAP, version, header size, sample rate,
    // channels, PCM format (1 = float32), EQMID and test MTU.
    constexpr char magic[8] = {'O', 'A', '2', 'D', 'P', 'C', 'A', 'P'};
    output.write(magic, sizeof(magic));
    writeU16(output, 1);
    writeU16(output, 28);
    writeU32(output, static_cast<uint32_t>(options.sampleRate));
    writeU16(output, kChannelCount);
    writeU16(output, 1);
    writeU32(output, static_cast<uint32_t>(options.quality));
    writeU32(output, kLdacMtuForPoc);
}

void writeLdacRecord(std::ofstream& output,
                     const std::array<unsigned char, LDACBT_MAX_NBYTE>& data,
                     int byteCount,
                     int frameCount) {
    check(byteCount >= 0 &&
              byteCount <= static_cast<int>(data.size()),
          "libldac produjo un tamaño de salida no válido");
    check(frameCount >= 0, "libldac produjo un número de frames no válido");

    if (byteCount == 0) {
        return;
    }

    writeU32(output, static_cast<uint32_t>(byteCount));
    writeU32(output, static_cast<uint32_t>(frameCount));
    output.write(reinterpret_cast<const char*>(data.data()), byteCount);
    check(static_cast<bool>(output), "Error escribiendo un paquete LDAC");
}

void encodeBlock(HANDLE_LDAC_BT encoder,
                 std::array<float, kSamplesPerLdacBlock>& pcm,
                 std::array<unsigned char, LDACBT_MAX_NBYTE>& encoded,
                 std::ofstream& output) {
    int pcmUsed = 0;
    int streamSize = 0;
    int frameCount = 0;

    const int result = ldacBT_encode(
        encoder, pcm.data(), &pcmUsed, encoded.data(), &streamSize, &frameCount);

    check(result == 0, "ldacBT_encode falló");
    check(pcmUsed == static_cast<int>(sizeof(pcm)),
          "libldac no consumió el bloque PCM completo");
    writeLdacRecord(output, encoded, streamSize, frameCount);
}

class EncoderWorker final {
public:
    EncoderWorker(PcmSpscQueue& queue,
                  std::ofstream& output,
                  LdacHandle encoder)
        : queue_(queue), output_(output),
          encoder_(std::move(encoder)), thread_([this] { run(); }) {}

    ~EncoderWorker() {
        stopAndJoin();
    }

    EncoderWorker(const EncoderWorker&) = delete;
    EncoderWorker& operator=(const EncoderWorker&) = delete;

    void notify() { dataReady_.signal(); }

    bool failed() const noexcept {
        return failed_.load(std::memory_order_acquire);
    }

    void stopAndJoin() noexcept {
        stopRequested_.store(true, std::memory_order_release);
        SetEvent(dataReady_.get());
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    void rethrowWorkerError() {
        std::exception_ptr error;
        {
            std::lock_guard<std::mutex> lock(errorMutex_);
            error = error_;
        }
        if (error) {
            std::rethrow_exception(error);
        }
    }

private:
    void run() noexcept {
        try {
            MmcssScope mmcss(L"Audio");
            if (!mmcss.active()) {
                std::fprintf(stderr,
                             "Aviso: no se pudo registrar el worker en MMCSS.\n");
            }

            std::array<float, kSamplesPerLdacBlock> pcm{};
            std::array<unsigned char, LDACBT_MAX_NBYTE> encoded{};

            for (;;) {
                const DWORD wait = WaitForSingleObject(dataReady_.get(), INFINITE);
                check(wait == WAIT_OBJECT_0,
                      "WaitForSingleObject del encoder falló");

                while (queue_.tryPopFullBlock(pcm.data())) {
                    encodeBlock(encoder_.get(), pcm, encoded, output_);
                }

                if (stopRequested_.load(std::memory_order_acquire)) {
                    while (queue_.tryPopFinalBlock(pcm.data())) {
                        encodeBlock(encoder_.get(), pcm, encoded, output_);
                    }

                    int pcmUsed = 0;
                    int streamSize = 0;
                    int frameCount = 0;
                    const int flushResult = ldacBT_encode(
                        encoder_.get(), nullptr, &pcmUsed, encoded.data(),
                        &streamSize, &frameCount);
                    check(flushResult == 0, "No se pudo vaciar el encoder LDAC");
                    writeLdacRecord(output_, encoded, streamSize, frameCount);

                    // A record with zero payload terminates the capture file.
                    writeU32(output_, 0);
                    writeU32(output_, 0);
                    break;
                }
            }
        } catch (...) {
            {
                std::lock_guard<std::mutex> lock(errorMutex_);
                error_ = std::current_exception();
            }
            failed_.store(true, std::memory_order_release);
        }
    }

    PcmSpscQueue& queue_;
    std::ofstream& output_;
    LdacHandle encoder_;
    WinEvent dataReady_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> failed_{false};
    std::mutex errorMutex_;
    std::exception_ptr error_;
    std::thread thread_;
};

ComPtr<IMMDevice> openRenderDevice(IMMDeviceEnumerator* enumerator,
                                   const std::wstring& deviceId) {
    ComPtr<IMMDevice> device;
    if (deviceId.empty()) {
        checkHr(enumerator->GetDefaultAudioEndpoint(
                    eRender, eMultimedia, device.GetAddressOf()),
                "GetDefaultAudioEndpoint");
    } else {
        checkHr(enumerator->GetDevice(deviceId.c_str(), device.GetAddressOf()),
                "IMMDeviceEnumerator::GetDevice");
    }
    return device;
}

void captureLoopback(const Options& options,
                     PcmSpscQueue& queue,
                     EncoderWorker& encoderWorker,
                     uint64_t& discontinuityCount) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    checkHr(CoCreateInstance(
                __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                IID_PPV_ARGS(enumerator.GetAddressOf())),
            "CoCreateInstance(MMDeviceEnumerator)");

    ComPtr<IMMDevice> device = openRenderDevice(enumerator.Get(), options.deviceId);
    ComPtr<IAudioClient> audioClient;
    checkHr(device->Activate(
                __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(audioClient.GetAddressOf())),
            "IMMDevice::Activate(IAudioClient)");

    WAVEFORMATEXTENSIBLE format{};
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = kChannelCount;
    format.Format.nSamplesPerSec = static_cast<DWORD>(options.sampleRate);
    format.Format.wBitsPerSample = 32;
    format.Format.nBlockAlign =
        static_cast<WORD>(kChannelCount * sizeof(float));
    format.Format.nAvgBytesPerSec =
        format.Format.nSamplesPerSec * format.Format.nBlockAlign;
    format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    format.Samples.wValidBitsPerSample = 32;
    format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

    checkHr(audioClient->Initialize(
                AUDCLNT_SHAREMODE_SHARED, kCaptureFlags,
                kWasapiBufferDuration, 0, &format.Format, nullptr),
            "IAudioClient::Initialize(loopback)");

    WinEvent captureReady;
    checkHr(audioClient->SetEventHandle(captureReady.get()),
            "IAudioClient::SetEventHandle");

    ComPtr<IAudioCaptureClient> captureClient;
    checkHr(audioClient->GetService(
                IID_PPV_ARGS(captureClient.GetAddressOf())),
            "IAudioClient::GetService(IAudioCaptureClient)");

    MmcssScope mmcss(L"Pro Audio");
    if (!mmcss.active()) {
        std::fprintf(stderr,
                     "Aviso: no se pudo registrar la captura en MMCSS.\n");
    }

    checkHr(audioClient->Start(), "IAudioClient::Start");
    std::wprintf(L"Capturando %d Hz, estéreo, durante %d s.\n",
                 options.sampleRate, options.durationSeconds);

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(options.durationSeconds);

    try {
        while (std::chrono::steady_clock::now() < deadline &&
               !encoderWorker.failed()) {
            const DWORD wait = WaitForSingleObject(captureReady.get(), 100);
            if (wait == WAIT_TIMEOUT) {
                continue;
            }
            check(wait == WAIT_OBJECT_0,
                  "WaitForSingleObject de WASAPI falló");

            for (;;) {
                UINT32 packetFrames = 0;
                checkHr(captureClient->GetNextPacketSize(&packetFrames),
                        "IAudioCaptureClient::GetNextPacketSize");
                if (packetFrames == 0) {
                    break;
                }

                BYTE* data = nullptr;
                DWORD flags = 0;
                checkHr(captureClient->GetBuffer(
                            &data, &packetFrames, &flags, nullptr, nullptr),
                        "IAudioCaptureClient::GetBuffer");

                const bool silent =
                    (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                const bool validData = silent || data != nullptr;
                if ((flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0) {
                    ++discontinuityCount;
                }

                const bool queued = validData && queue.tryPush(
                    reinterpret_cast<const float*>(data), packetFrames, silent);
                const HRESULT releaseResult =
                    captureClient->ReleaseBuffer(packetFrames);
                checkHr(releaseResult,
                        "IAudioCaptureClient::ReleaseBuffer");
                check(validData, "WASAPI devolvió un paquete sin datos válidos");
                check(queued,
                      "Desbordamiento del buffer PCM: el encoder o disco no alcanza tiempo real");
                encoderWorker.notify();
            }
        }
    } catch (...) {
        audioClient->Stop();
        throw;
    }

    checkHr(audioClient->Stop(), "IAudioClient::Stop");
}

void runCapture(const Options& options) {
    ComApartment apartment;

    PcmSpscQueue queue;
    LdacHandle ldac = createLdacEncoder(options);

    std::ofstream output(options.outputPath, std::ios::binary | std::ios::trunc);
    check(output.is_open(), "No se pudo crear el archivo de captura");
    writeCaptureHeader(output, options);

    EncoderWorker encoderWorker(queue, output, std::move(ldac));
    uint64_t discontinuityCount = 0;

    try {
        captureLoopback(options, queue, encoderWorker, discontinuityCount);
        encoderWorker.stopAndJoin();
        encoderWorker.rethrowWorkerError();
    } catch (...) {
        encoderWorker.stopAndJoin();
        throw;
    }

    output.flush();
    check(static_cast<bool>(output), "No se pudo finalizar el archivo de captura");

    std::wprintf(L"Captura terminada: %ls\n",
                 options.outputPath.c_str());
    std::wprintf(L"Discontinuidades WASAPI: %llu\n",
                 static_cast<unsigned long long>(discontinuityCount));
    std::wprintf(L"Formato: contenedor PoC OA2DPCAP; no es un stream Bluetooth.\n");
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        const Options options = parseArguments(argc, argv);
        if (options.listDevices) {
            listRenderDevices();
        } else {
            runCapture(options);
        }
        return 0;
    } catch (const HResultError& error) {
        std::fprintf(stderr, "Error: %s\n", error.what());
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Error: %s\n", error.what());
        return 2;
    }
}
