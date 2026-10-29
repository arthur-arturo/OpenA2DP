#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace opena2dp::avdtp {

using Byte = std::uint8_t;

enum class PacketType : Byte {
    Single = 0,
    Start = 1,
    Continue = 2,
    End = 3,
};

enum class MessageType : Byte {
    Command = 0,
    GeneralReject = 1,
    Accept = 2,
    Reject = 3,
};

enum class SignalId : Byte {
    Discover = 0x01,
    GetCapabilities = 0x02,
    SetConfiguration = 0x03,
    GetConfiguration = 0x04,
    Reconfigure = 0x05,
    Open = 0x06,
    Start = 0x07,
    Close = 0x08,
    Suspend = 0x09,
    Abort = 0x0A,
    SecurityControl = 0x0B,
    GetAllCapabilities = 0x0C,
    DelayReport = 0x0D,
};

enum class Error {
    None,
    TruncatedPacket,
    ReservedBitsSet,
    InvalidSignalId,
    InvalidTransactionLabel,
    FragmentationUnsupported,
    InvalidSeid,
    InvalidDiscoverPayload,
    EmptySeidList,
    TransactionLabelMismatch,
    UnexpectedSignal,
    UnexpectedMessage,
    InvalidCapabilityList,
    DuplicateCapabilityCategory,
    UnsupportedCodec,
    InvalidSbcConfiguration,
    PacketTooLarge,
};

template <typename T>
struct Result {
    T value{};
    Error error = Error::None;

    explicit operator bool() const noexcept { return error == Error::None; }
};

struct Header {
    Byte transactionLabel = 0;
    PacketType packetType = PacketType::Single;
    MessageType messageType = MessageType::Command;
    Byte signalId = 0;
};

struct PacketView {
    Header header{};
    std::span<const Byte> payload{};
};

enum class MediaType : Byte {
    Audio = 0,
    Video = 1,
    Multimedia = 2,
};

enum class SepType : Byte {
    // TSEP bit values from AVDTP: Source=0, Sink=1.
    Source = 0,
    Sink = 1,
};

struct StreamEndpoint {
    Byte seid = 0;
    bool inUse = false;
    MediaType mediaType = MediaType::Audio;
    SepType sepType = SepType::Sink;
};

enum class ServiceCategory : Byte {
    MediaTransport = 0x01,
    Reporting = 0x02,
    Recovery = 0x03,
    ContentProtection = 0x04,
    HeaderCompression = 0x05,
    Multiplexing = 0x06,
    MediaCodec = 0x07,
    DelayReporting = 0x08,
};

struct ServiceCapabilityView {
    Byte category = 0;
    std::span<const Byte> data{};
};

// SBC media codec information element, with each bitfield kept separate.
struct SbcCodecInfo {
    Byte samplingFrequencies = 0;
    Byte channelModes = 0;
    Byte blockLengths = 0;
    Byte subbands = 0;
    Byte allocationMethods = 0;
    Byte minimumBitpool = 0;
    Byte maximumBitpool = 0;
};

Result<PacketView> parseSinglePacket(std::span<const Byte> packet) noexcept;
Result<std::vector<Byte>> makeCommandPacket(
    Byte transactionLabel,
    SignalId signalId,
    std::span<const Byte> payload = {});

Result<std::vector<Byte>> makeDiscoverCommand(Byte transactionLabel);
Result<std::vector<Byte>> makeGetCapabilitiesCommand(
    Byte transactionLabel, Byte remoteSeid, bool allCapabilities = false);
Result<std::vector<Byte>> makeGetConfigurationCommand(
    Byte transactionLabel, Byte remoteSeid);
Result<std::vector<Byte>> makeSetConfigurationCommand(
    Byte transactionLabel,
    Byte remoteSeid,
    Byte localSeid,
    std::span<const ServiceCapabilityView> capabilities);
Result<std::vector<Byte>> makeOpenCommand(Byte transactionLabel, Byte remoteSeid);
Result<std::vector<Byte>> makeCloseCommand(Byte transactionLabel, Byte remoteSeid);
Result<std::vector<Byte>> makeStartCommand(
    Byte transactionLabel, std::span<const Byte> remoteSeids);
Result<std::vector<Byte>> makeSuspendCommand(
    Byte transactionLabel, std::span<const Byte> remoteSeids);
Result<std::vector<Byte>> makeAbortCommand(Byte transactionLabel, Byte remoteSeid);

Result<std::vector<StreamEndpoint>> parseDiscoverAccept(
    std::span<const Byte> packet, Byte expectedTransactionLabel);
Result<std::vector<ServiceCapabilityView>> parseServiceCapabilities(
    std::span<const Byte> payload);
Result<std::vector<Byte>> serializeServiceCapabilities(
    std::span<const ServiceCapabilityView> capabilities);

Result<SbcCodecInfo> parseSbcCodecCapability(
    const ServiceCapabilityView& capability) noexcept;
Result<std::array<Byte, 6>> serializeSbcCodecInfo(
    const SbcCodecInfo& info) noexcept;
Error validateSbcConfiguration(
    const SbcCodecInfo& remoteCapabilities,
    const SbcCodecInfo& selectedConfiguration) noexcept;

} // namespace opena2dp::avdtp
