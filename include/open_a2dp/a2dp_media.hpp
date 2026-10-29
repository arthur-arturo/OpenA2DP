#pragma once

#include "open_a2dp/avdtp.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace opena2dp::a2dp {

using avdtp::Byte;

struct RtpHeader {
    Byte payloadType = 96;
    bool marker = false;
    std::uint16_t sequenceNumber = 0;
    std::uint32_t timestamp = 0;
    std::uint32_t synchronizationSource = 0;
};

// Serializes one RTP media packet containing one or more complete SBC frames.
// The caller supplies the negotiated L2CAP media-channel MTU and keeps RTP
// sequence/timestamp advancement synchronized with its audio clock.
avdtp::Result<std::vector<Byte>> makeSbcRtpPacket(
    const RtpHeader& header,
    Byte sbcFrameCount,
    std::span<const Byte> encodedSbcFrames,
    std::size_t l2capMtu);

} // namespace opena2dp::a2dp
