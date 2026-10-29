#include "open_a2dp/avdtp.hpp"

#include <array>
#include <bit>
#include <utility>

namespace opena2dp::avdtp {
namespace {

constexpr std::size_t kHeaderSize = 2;
constexpr std::size_t kMaximumSinglePacketSize = 0xFFFF;
constexpr Byte kMaximumTransactionLabel = 0x0F;
constexpr Byte kMaximumSignalId = 0x3F;
constexpr Byte kMaximumSeid = 0x3E;
constexpr Byte kReservedHeaderMask = 0xC0;
constexpr Byte kDiscoverReservedSeidMask = 0x80;
constexpr Byte kDiscoverInUseMask = 0x40;
constexpr Byte kDiscoverSeidValueMask = 0x3F;
constexpr Byte kDiscoverReservedSepInfoMask = 0xE0;
constexpr Byte kDiscoverSepTypeMask = 0x10;
constexpr Byte kDiscoverMediaTypeMask = 0x0F;
constexpr Byte kMediaTypeAudio = 0x00;
constexpr Byte kSbcCodecType = 0x00;
constexpr Byte kSbcFrequencyMask = 0xF0;
constexpr Byte kSbcChannelModeMask = 0x0F;
constexpr Byte kSbcBlockLengthMask = 0xF0;
constexpr Byte kSbcSubbandMask = 0x0C;
constexpr Byte kSbcAllocationMask = 0x03;
constexpr Byte kSbcMinimumBitpool = 2;
constexpr Byte kSbcMaximumBitpool = 250;

bool validSeid(Byte seid) noexcept {
    return seid >= 1 && seid <= kMaximumSeid;
}

Byte encodeSeid(Byte seid) noexcept {
    return static_cast<Byte>(seid << 2);
}

bool validCategory(Byte category) noexcept {
    return category >= static_cast<Byte>(ServiceCategory::MediaTransport) &&
           category <= static_cast<Byte>(ServiceCategory::DelayReporting);
}

bool validSbcInfo(const SbcCodecInfo& info) noexcept {
    return info.samplingFrequencies != 0 &&
           (info.samplingFrequencies & ~kSbcFrequencyMask) == 0 &&
           info.channelModes != 0 &&
           (info.channelModes & ~kSbcChannelModeMask) == 0 &&
           info.blockLengths != 0 &&
           (info.blockLengths & ~kSbcBlockLengthMask) == 0 &&
           info.subbands != 0 && (info.subbands & ~kSbcSubbandMask) == 0 &&
           info.allocationMethods != 0 &&
           (info.allocationMethods & ~kSbcAllocationMask) == 0 &&
           info.minimumBitpool >= kSbcMinimumBitpool &&
           info.maximumBitpool <= kSbcMaximumBitpool &&
           info.minimumBitpool <= info.maximumBitpool;
}

Result<std::vector<Byte>> makePacket(
    Byte transactionLabel,
    MessageType messageType,
    Byte signalId,
    std::span<const Byte> payload) {
    if (transactionLabel > kMaximumTransactionLabel) {
        return {{}, Error::InvalidTransactionLabel};
    }
    if (signalId == 0 || signalId > kMaximumSignalId) {
        return {{}, Error::InvalidSignalId};
    }
    if (payload.size() > kMaximumSinglePacketSize - kHeaderSize) {
        return {{}, Error::PacketTooLarge};
    }

    const Byte first = static_cast<Byte>(
        (transactionLabel << 4) | static_cast<Byte>(messageType));
    std::vector<Byte> packet;
    packet.reserve(kHeaderSize + payload.size());
    packet.push_back(first);
    packet.push_back(signalId);
    packet.insert(packet.end(), payload.begin(), payload.end());
    return {std::move(packet), Error::None};
}

Result<std::vector<Byte>> makeSeidCommand(
    Byte transactionLabel, SignalId signalId, Byte remoteSeid) {
    if (!validSeid(remoteSeid)) {
        return {{}, Error::InvalidSeid};
    }
    const std::array<Byte, 1> payload{encodeSeid(remoteSeid)};
    return makeCommandPacket(transactionLabel, signalId, payload);
}

Result<std::vector<Byte>> makeSeidListCommand(
    Byte transactionLabel,
    SignalId signalId,
    std::span<const Byte> remoteSeids) {
    if (remoteSeids.empty()) {
        return {{}, Error::EmptySeidList};
    }
    if (remoteSeids.size() > kMaximumSinglePacketSize - kHeaderSize) {
        return {{}, Error::PacketTooLarge};
    }

    std::array<bool, kMaximumSeid + 1> seen{};
    std::vector<Byte> payload;
    payload.reserve(remoteSeids.size());
    for (const Byte seid : remoteSeids) {
        if (!validSeid(seid) || seen[seid]) {
            return {{}, Error::InvalidSeid};
        }
        seen[seid] = true;
        payload.push_back(encodeSeid(seid));
    }
    return makeCommandPacket(transactionLabel, signalId, payload);
}

} // namespace

Result<PacketView> parseSinglePacket(std::span<const Byte> packet) noexcept {
    if (packet.size() > kMaximumSinglePacketSize) {
        return {{}, Error::PacketTooLarge};
    }
    if (packet.size() < kHeaderSize) {
        return {{}, Error::TruncatedPacket};
    }
    if ((packet[1] & kReservedHeaderMask) != 0) {
        return {{}, Error::ReservedBitsSet};
    }

    const Byte signalId = static_cast<Byte>(packet[1] & kMaximumSignalId);
    if (signalId == 0) {
        return {{}, Error::InvalidSignalId};
    }

    const Byte first = packet[0];
    const auto packetType = static_cast<PacketType>((first >> 2) & 0x03);
    if (packetType != PacketType::Single) {
        return {{}, Error::FragmentationUnsupported};
    }

    Header header;
    header.transactionLabel = static_cast<Byte>(first >> 4);
    header.packetType = packetType;
    header.messageType = static_cast<MessageType>(first & 0x03);
    header.signalId = signalId;

    return {{header, packet.subspan(kHeaderSize)}, Error::None};
}

Result<std::vector<Byte>> makeCommandPacket(
    Byte transactionLabel,
    SignalId signalId,
    std::span<const Byte> payload) {
    return makePacket(transactionLabel, MessageType::Command,
                      static_cast<Byte>(signalId), payload);
}

Result<std::vector<Byte>> makeDiscoverCommand(Byte transactionLabel) {
    return makeCommandPacket(transactionLabel, SignalId::Discover);
}

Result<std::vector<Byte>> makeGetCapabilitiesCommand(
    Byte transactionLabel, Byte remoteSeid, bool allCapabilities) {
    const SignalId signal = allCapabilities ? SignalId::GetAllCapabilities
                                            : SignalId::GetCapabilities;
    return makeSeidCommand(transactionLabel, signal, remoteSeid);
}

Result<std::vector<Byte>> makeGetConfigurationCommand(
    Byte transactionLabel, Byte remoteSeid) {
    return makeSeidCommand(transactionLabel, SignalId::GetConfiguration,
                           remoteSeid);
}

Result<std::vector<Byte>> makeSetConfigurationCommand(
    Byte transactionLabel,
    Byte remoteSeid,
    Byte localSeid,
    std::span<const ServiceCapabilityView> capabilities) {
    if (!validSeid(remoteSeid) || !validSeid(localSeid)) {
        return {{}, Error::InvalidSeid};
    }

    bool hasMediaTransport = false;
    bool hasMediaCodec = false;
    for (const auto& capability : capabilities) {
        if (capability.category ==
            static_cast<Byte>(ServiceCategory::MediaTransport)) {
            hasMediaTransport = capability.data.empty();
        } else if (capability.category ==
                   static_cast<Byte>(ServiceCategory::MediaCodec)) {
            hasMediaCodec = capability.data.size() >= 2;
        }
    }
    if (!hasMediaTransport || !hasMediaCodec) {
        return {{}, Error::InvalidCapabilityList};
    }

    auto serializedCapabilities = serializeServiceCapabilities(capabilities);
    if (!serializedCapabilities) {
        return {{}, serializedCapabilities.error};
    }

    std::vector<Byte> payload;
    payload.reserve(2 + serializedCapabilities.value.size());
    payload.push_back(encodeSeid(remoteSeid));
    payload.push_back(encodeSeid(localSeid));
    payload.insert(payload.end(), serializedCapabilities.value.begin(),
                   serializedCapabilities.value.end());
    return makeCommandPacket(transactionLabel, SignalId::SetConfiguration,
                             payload);
}

Result<std::vector<Byte>> makeOpenCommand(Byte transactionLabel, Byte remoteSeid) {
    return makeSeidCommand(transactionLabel, SignalId::Open, remoteSeid);
}

Result<std::vector<Byte>> makeCloseCommand(Byte transactionLabel, Byte remoteSeid) {
    return makeSeidCommand(transactionLabel, SignalId::Close, remoteSeid);
}

Result<std::vector<Byte>> makeStartCommand(
    Byte transactionLabel, std::span<const Byte> remoteSeids) {
    return makeSeidListCommand(transactionLabel, SignalId::Start, remoteSeids);
}

Result<std::vector<Byte>> makeSuspendCommand(
    Byte transactionLabel, std::span<const Byte> remoteSeids) {
    return makeSeidListCommand(transactionLabel, SignalId::Suspend, remoteSeids);
}

Result<std::vector<Byte>> makeAbortCommand(Byte transactionLabel, Byte remoteSeid) {
    return makeSeidCommand(transactionLabel, SignalId::Abort, remoteSeid);
}

Result<std::vector<StreamEndpoint>> parseDiscoverAccept(
    std::span<const Byte> packet, Byte expectedTransactionLabel) {
    auto parsed = parseSinglePacket(packet);
    if (!parsed) {
        return {{}, parsed.error};
    }
    if (parsed.value.header.transactionLabel != expectedTransactionLabel) {
        return {{}, Error::TransactionLabelMismatch};
    }
    if (parsed.value.header.signalId != static_cast<Byte>(SignalId::Discover)) {
        return {{}, Error::UnexpectedSignal};
    }
    if (parsed.value.header.messageType != MessageType::Accept) {
        return {{}, Error::UnexpectedMessage};
    }

    if ((parsed.value.payload.size() % 2) != 0) {
        return {{}, Error::InvalidDiscoverPayload};
    }

    std::vector<StreamEndpoint> endpoints;
    endpoints.reserve(parsed.value.payload.size() / 2);
    std::array<bool, kMaximumSeid + 1> seen{};
    // Discover SEP records are two octets and encode SEID in the low six bits;
    // this differs from command parameters, which encode SEID in bits 7..2.
    for (std::size_t offset = 0; offset < parsed.value.payload.size(); offset += 2) {
        const Byte seidInfo = parsed.value.payload[offset];
        const Byte sepInfo = parsed.value.payload[offset + 1];
        if ((seidInfo & kDiscoverReservedSeidMask) != 0 ||
            (sepInfo & kDiscoverReservedSepInfoMask) != 0) {
            return {{}, Error::ReservedBitsSet};
        }

        const Byte seid =
            static_cast<Byte>(seidInfo & kDiscoverSeidValueMask);
        if (!validSeid(seid) || seen[seid]) {
            return {{}, Error::InvalidSeid};
        }
        seen[seid] = true;

        const Byte mediaType =
            static_cast<Byte>(sepInfo & kDiscoverMediaTypeMask);
        if (mediaType > static_cast<Byte>(MediaType::Multimedia)) {
            return {{}, Error::InvalidDiscoverPayload};
        }

        const auto type = (sepInfo & kDiscoverSepTypeMask) == 0
                              ? SepType::Source
                              : SepType::Sink;
        endpoints.push_back({
            seid,
            (seidInfo & kDiscoverInUseMask) != 0,
            static_cast<MediaType>(mediaType),
            type,
        });
    }

    return {std::move(endpoints), Error::None};
}

Result<std::vector<ServiceCapabilityView>> parseServiceCapabilities(
    std::span<const Byte> payload) {
    if (payload.size() > kMaximumSinglePacketSize - kHeaderSize) {
        return {{}, Error::PacketTooLarge};
    }
    std::vector<ServiceCapabilityView> capabilities;
    std::array<bool, 9> seen{};

    std::size_t offset = 0;
    while (offset < payload.size()) {
        if (payload.size() - offset < 2) {
            return {{}, Error::InvalidCapabilityList};
        }

        const Byte category = payload[offset];
        const Byte length = payload[offset + 1];
        offset += 2;

        if (!validCategory(category) || length > payload.size() - offset) {
            return {{}, Error::InvalidCapabilityList};
        }
        if ((category == static_cast<Byte>(ServiceCategory::MediaTransport) &&
             length != 0) ||
            (category == static_cast<Byte>(ServiceCategory::MediaCodec) &&
             length < 2)) {
            return {{}, Error::InvalidCapabilityList};
        }
        if (seen[category]) {
            return {{}, Error::DuplicateCapabilityCategory};
        }
        seen[category] = true;

        capabilities.push_back({category, payload.subspan(offset, length)});
        offset += length;
    }

    return {std::move(capabilities), Error::None};
}

Result<std::vector<Byte>> serializeServiceCapabilities(
    std::span<const ServiceCapabilityView> capabilities) {
    std::array<bool, 9> seen{};
    std::size_t totalSize = 0;
    for (const auto& capability : capabilities) {
        if (!validCategory(capability.category) || capability.data.size() > 0xFF) {
            return {{}, Error::InvalidCapabilityList};
        }
        if ((capability.category ==
                 static_cast<Byte>(ServiceCategory::MediaTransport) &&
             !capability.data.empty()) ||
            (capability.category ==
                 static_cast<Byte>(ServiceCategory::MediaCodec) &&
             capability.data.size() < 2)) {
            return {{}, Error::InvalidCapabilityList};
        }
        if (seen[capability.category]) {
            return {{}, Error::DuplicateCapabilityCategory};
        }
        seen[capability.category] = true;
        const std::size_t recordSize = 2 + capability.data.size();
        const std::size_t maximumPayload =
            kMaximumSinglePacketSize - kHeaderSize;
        if (recordSize > maximumPayload - totalSize) {
            return {{}, Error::PacketTooLarge};
        }
        totalSize += recordSize;
    }

    std::vector<Byte> payload;
    payload.reserve(totalSize);
    for (const auto& capability : capabilities) {
        payload.push_back(capability.category);
        payload.push_back(static_cast<Byte>(capability.data.size()));
        payload.insert(payload.end(), capability.data.begin(),
                       capability.data.end());
    }
    return {std::move(payload), Error::None};
}

Result<SbcCodecInfo> parseSbcCodecCapability(
    const ServiceCapabilityView& capability) noexcept {
    if (capability.category != static_cast<Byte>(ServiceCategory::MediaCodec) ||
        capability.data.size() != 6) {
        return {{}, Error::UnsupportedCodec};
    }

    const auto data = capability.data;
    if (data[0] != kMediaTypeAudio || data[1] != kSbcCodecType) {
        return {{}, Error::UnsupportedCodec};
    }

    SbcCodecInfo info{
        static_cast<Byte>(data[2] & kSbcFrequencyMask),
        static_cast<Byte>(data[2] & kSbcChannelModeMask),
        static_cast<Byte>(data[3] & kSbcBlockLengthMask),
        static_cast<Byte>(data[3] & kSbcSubbandMask),
        static_cast<Byte>(data[3] & kSbcAllocationMask),
        data[4],
        data[5],
    };
    if (!validSbcInfo(info)) {
        return {{}, Error::InvalidSbcConfiguration};
    }
    return {info, Error::None};
}

Result<std::array<Byte, 6>> serializeSbcCodecInfo(
    const SbcCodecInfo& info) noexcept {
    if (!validSbcInfo(info)) {
        return {{}, Error::InvalidSbcConfiguration};
    }

    return {{
        kMediaTypeAudio,
        kSbcCodecType,
        static_cast<Byte>(info.samplingFrequencies | info.channelModes),
        static_cast<Byte>(info.blockLengths | info.subbands |
                          info.allocationMethods),
        info.minimumBitpool,
        info.maximumBitpool,
    }, Error::None};
}

Error validateSbcConfiguration(
    const SbcCodecInfo& remoteCapabilities,
    const SbcCodecInfo& selectedConfiguration) noexcept {
    if (!validSbcInfo(remoteCapabilities) ||
        !validSbcInfo(selectedConfiguration)) {
        return Error::InvalidSbcConfiguration;
    }

    const bool oneFrequency = std::has_single_bit(
        selectedConfiguration.samplingFrequencies);
    const bool oneChannelMode = std::has_single_bit(
        selectedConfiguration.channelModes);
    const bool oneBlockLength = std::has_single_bit(
        selectedConfiguration.blockLengths);
    const bool oneSubband = std::has_single_bit(
        selectedConfiguration.subbands);
    const bool oneAllocationMethod = std::has_single_bit(
        selectedConfiguration.allocationMethods);

    if (!oneFrequency || !oneChannelMode || !oneBlockLength || !oneSubband ||
        !oneAllocationMethod) {
        return Error::InvalidSbcConfiguration;
    }

    if ((selectedConfiguration.samplingFrequencies &
         remoteCapabilities.samplingFrequencies) == 0 ||
        (selectedConfiguration.channelModes & remoteCapabilities.channelModes) ==
            0 ||
        (selectedConfiguration.blockLengths & remoteCapabilities.blockLengths) ==
            0 ||
        (selectedConfiguration.subbands & remoteCapabilities.subbands) == 0 ||
        (selectedConfiguration.allocationMethods &
         remoteCapabilities.allocationMethods) == 0 ||
        selectedConfiguration.minimumBitpool < remoteCapabilities.minimumBitpool ||
        selectedConfiguration.maximumBitpool > remoteCapabilities.maximumBitpool) {
        return Error::InvalidSbcConfiguration;
    }

    return Error::None;
}

} // namespace opena2dp::avdtp
