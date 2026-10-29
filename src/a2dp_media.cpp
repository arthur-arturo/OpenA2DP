#include "open_a2dp/a2dp_media.hpp"

#include <utility>

namespace opena2dp::a2dp {
namespace {

constexpr std::size_t kRtpHeaderSize = 12;
constexpr std::size_t kSbcPayloadHeaderSize = 1;
constexpr Byte kMaximumPayloadType = 0x7F;
constexpr Byte kMaximumSbcFramesPerPacket = 0x0F;
constexpr Byte kRtpVersionTwo = 0x80;
constexpr Byte kRtpMarkerMask = 0x80;

void appendBigEndian16(std::vector<Byte>& output, std::uint16_t value) {
    output.push_back(static_cast<Byte>(value >> 8));
    output.push_back(static_cast<Byte>(value & 0xFF));
}

void appendBigEndian32(std::vector<Byte>& output, std::uint32_t value) {
    output.push_back(static_cast<Byte>(value >> 24));
    output.push_back(static_cast<Byte>((value >> 16) & 0xFF));
    output.push_back(static_cast<Byte>((value >> 8) & 0xFF));
    output.push_back(static_cast<Byte>(value & 0xFF));
}

} // namespace

avdtp::Result<std::vector<Byte>> makeSbcRtpPacket(
    const RtpHeader& header,
    Byte sbcFrameCount,
    std::span<const Byte> encodedSbcFrames,
    std::size_t l2capMtu) {
    if (header.payloadType > kMaximumPayloadType) {
        return {{}, avdtp::Error::InvalidRtpHeader};
    }
    if (sbcFrameCount == 0 || sbcFrameCount > kMaximumSbcFramesPerPacket ||
        encodedSbcFrames.empty()) {
        return {{}, avdtp::Error::InvalidMediaPayload};
    }

    const std::size_t packetHeaderSize =
        kRtpHeaderSize + kSbcPayloadHeaderSize;
    if (l2capMtu > 0xFFFF || l2capMtu < packetHeaderSize ||
        encodedSbcFrames.size() > l2capMtu - packetHeaderSize) {
        return {{}, avdtp::Error::PacketTooLarge};
    }

    std::vector<Byte> packet;
    packet.reserve(packetHeaderSize + encodedSbcFrames.size());
    packet.push_back(kRtpVersionTwo); // V=2, P=0, X=0, CC=0.
    packet.push_back(static_cast<Byte>(
        (header.marker ? kRtpMarkerMask : 0) | header.payloadType));
    appendBigEndian16(packet, header.sequenceNumber);
    appendBigEndian32(packet, header.timestamp);
    appendBigEndian32(packet, header.synchronizationSource);
    packet.push_back(sbcFrameCount); // F/S/L/RFA=0; frame count in low nibble.
    packet.insert(packet.end(), encodedSbcFrames.begin(), encodedSbcFrames.end());
    return {std::move(packet), avdtp::Error::None};
}

} // namespace opena2dp::a2dp
