#include "open_a2dp/avdtp.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

using opena2dp::avdtp::Byte;
using opena2dp::avdtp::Error;
using opena2dp::avdtp::MessageType;
using opena2dp::avdtp::MediaType;
using opena2dp::avdtp::PacketType;
using opena2dp::avdtp::ServiceCapabilityView;
using opena2dp::avdtp::ServiceCategory;
using opena2dp::avdtp::SignalId;
using opena2dp::avdtp::SepType;
using opena2dp::avdtp::SbcCodecInfo;

namespace {

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void testCommandBuildAndParse() {
    const auto command = opena2dp::avdtp::makeDiscoverCommand(0x0A);
    expect(static_cast<bool>(command), "build AVDTP Discover");
    expect(command.value == std::vector<Byte>({0xA0, 0x01}),
           "Discover packet bytes match AVDTP header format");

    const auto parsed = opena2dp::avdtp::parseSinglePacket(command.value);
    expect(static_cast<bool>(parsed), "parse Discover packet");
    expect(parsed.value.header.transactionLabel == 0x0A,
           "extract transaction label");
    expect(parsed.value.header.packetType == PacketType::Single,
           "extract single packet type");
    expect(parsed.value.header.messageType == MessageType::Command,
           "extract command message type");
    expect(parsed.value.header.signalId ==
               static_cast<Byte>(SignalId::Discover),
           "extract Discover signal identifier");
    expect(parsed.value.payload.empty(), "Discover has no parameters");

    const auto getAll =
        opena2dp::avdtp::makeGetCapabilitiesCommand(0x03, 1, true);
    expect(static_cast<bool>(getAll), "build GetAllCapabilities command");
    const auto getAllPacket =
        opena2dp::avdtp::parseSinglePacket(getAll.value);
    expect(static_cast<bool>(getAllPacket) &&
               getAllPacket.value.header.signalId == 0x0C,
           "GetAllCapabilities uses AVDTP signal identifier 0x0C");
}

void testMalformedPacketsAreRejected() {
    const std::array<Byte, 1> truncated{0x00};
    expect(opena2dp::avdtp::parseSinglePacket(truncated).error ==
               Error::TruncatedPacket,
           "reject truncated packet header");

    const std::array<Byte, 2> reservedBits{0x00, 0x41};
    expect(opena2dp::avdtp::parseSinglePacket(reservedBits).error ==
               Error::ReservedBitsSet,
           "reject reserved header bits");

    const std::array<Byte, 2> fragmented{0x04, 0x01};
    expect(opena2dp::avdtp::parseSinglePacket(fragmented).error ==
               Error::FragmentationUnsupported,
           "do not misparse fragmented AVDTP packets as single packets");

    expect(opena2dp::avdtp::makeDiscoverCommand(0x10).error ==
               Error::InvalidTransactionLabel,
           "reject transaction labels wider than four bits");
}

void testDiscoverAccept() {
    // Each SEP information record is two octets: SEID/in-use, then media/TSEP.
    // Label 5, single packet, Accept, Discover; remote SEID 1 is a Sink and
    // remote SEID 2 is an in-use Source.
    const std::array<Byte, 6> response{
        0x52, 0x01, 0x01, 0x10, 0x42, 0x00,
    };
    const auto parsed = opena2dp::avdtp::parseDiscoverAccept(response, 5);
    expect(static_cast<bool>(parsed), "parse Discover Accept response");
    expect(parsed.value.size() == 2, "Discover returns two endpoints");
    expect(parsed.value[0].seid == 1 && !parsed.value[0].inUse &&
               parsed.value[0].mediaType == MediaType::Audio &&
               parsed.value[0].sepType == SepType::Sink,
           "decode free Audio Sink SEP");
    expect(parsed.value[1].seid == 2 && parsed.value[1].inUse &&
               parsed.value[1].mediaType == MediaType::Audio &&
               parsed.value[1].sepType == SepType::Source,
           "decode in-use Audio Source SEP");
    expect(opena2dp::avdtp::parseDiscoverAccept(response, 4).error ==
               Error::TransactionLabelMismatch,
           "match response with the outstanding transaction");

    const std::array<Byte, 5> oddPayload{
        0x52, 0x01, 0x01, 0x10, 0x42,
    };
    expect(opena2dp::avdtp::parseDiscoverAccept(oddPayload, 5).error ==
               Error::InvalidDiscoverPayload,
           "reject incomplete SEP information records");
}

void testServiceCapabilitiesAndSbc() {
    // Media Transport (empty) followed by SBC Media Codec capabilities.
    const std::array<Byte, 10> serviceCapabilities{
        0x01, 0x00,
        0x07, 0x06, 0x00, 0x00, 0x33, 0xFF, 0x02, 0x35,
    };
    const auto parsed =
        opena2dp::avdtp::parseServiceCapabilities(serviceCapabilities);
    expect(static_cast<bool>(parsed), "parse service-capability TLVs");
    expect(parsed.value.size() == 2, "parse both service capabilities");
    expect(parsed.value[0].category ==
               static_cast<Byte>(ServiceCategory::MediaTransport) &&
               parsed.value[0].data.empty(),
           "parse empty Media Transport capability");

    const auto sbc =
        opena2dp::avdtp::parseSbcCodecCapability(parsed.value[1]);
    expect(static_cast<bool>(sbc), "parse SBC codec capability");
    expect(sbc.value.samplingFrequencies == 0x30 &&
               sbc.value.channelModes == 0x03 &&
               sbc.value.blockLengths == 0xF0 && sbc.value.subbands == 0x0C &&
               sbc.value.allocationMethods == 0x03 &&
               sbc.value.minimumBitpool == 2 && sbc.value.maximumBitpool == 53,
           "decode SBC capability fields");

    const SbcCodecInfo selected{
        0x10, // 48 kHz
        0x01, // joint stereo
        0x10, // 16 blocks
        0x04, // 8 subbands
        0x01, // loudness allocation
        2,
        53,
    };
    expect(opena2dp::avdtp::validateSbcConfiguration(sbc.value, selected) ==
               Error::None,
           "accept SBC configuration within remote capabilities");

    auto encodedSbc = opena2dp::avdtp::serializeSbcCodecInfo(selected);
    expect(static_cast<bool>(encodedSbc), "serialize selected SBC config");
    expect(encodedSbc.value ==
               std::array<Byte, 6>{0x00, 0x00, 0x11, 0x15, 0x02, 0x35},
           "encode SBC information element");

    const SbcCodecInfo unsupportedRate{
        0x80, 0x01, 0x10, 0x04, 0x01, 2, 53,
    };
    expect(opena2dp::avdtp::validateSbcConfiguration(sbc.value, unsupportedRate) ==
               Error::InvalidSbcConfiguration,
           "reject an SBC sample rate absent from remote capabilities");
}

void testMalformedCapabilitiesAndSetConfiguration() {
    const std::array<Byte, 3> truncatedTlv{0x01, 0x02, 0x00};
    expect(opena2dp::avdtp::parseServiceCapabilities(truncatedTlv).error ==
               Error::InvalidCapabilityList,
           "reject truncated service-capability data");

    const std::array<Byte, 4> duplicateCategory{0x01, 0x00, 0x01, 0x00};
    expect(opena2dp::avdtp::parseServiceCapabilities(duplicateCategory).error ==
               Error::DuplicateCapabilityCategory,
           "reject duplicate service categories");

    const std::array<Byte, 3> invalidTransportLength{0x01, 0x01, 0x00};
    expect(opena2dp::avdtp::parseServiceCapabilities(invalidTransportLength).error ==
               Error::InvalidCapabilityList,
           "reject non-empty Media Transport capability");

    const std::array<Byte, 0> noTransportData{};
    const std::array<Byte, 6> sbcData{0x00, 0x00, 0x33, 0xFF, 0x02, 0x35};
    const std::array<ServiceCapabilityView, 2> capabilities{{
        {static_cast<Byte>(ServiceCategory::MediaTransport), noTransportData},
        {static_cast<Byte>(ServiceCategory::MediaCodec), sbcData},
    }};

    const auto command = opena2dp::avdtp::makeSetConfigurationCommand(
        2, 3, 4, capabilities);
    expect(static_cast<bool>(command), "build SetConfiguration command");

    const auto packet = opena2dp::avdtp::parseSinglePacket(command.value);
    expect(static_cast<bool>(packet), "parse generated SetConfiguration");
    expect(packet.value.header.signalId ==
               static_cast<Byte>(SignalId::SetConfiguration),
           "SetConfiguration signal identifier");
    expect(packet.value.payload.size() == 12 &&
               packet.value.payload[0] == 0x0C &&
               packet.value.payload[1] == 0x10,
           "SetConfiguration carries encoded ACP and INT SEIDs");
    const std::array<Byte, 10> expectedCapabilities{
        0x01, 0x00, 0x07, 0x06, 0x00, 0x00, 0x33, 0xFF, 0x02, 0x35,
    };
    expect(std::equal(packet.value.payload.begin() + 2,
                      packet.value.payload.end(),
                      expectedCapabilities.begin(), expectedCapabilities.end()),
           "SetConfiguration serializes negotiated service capabilities");
}

void testStartCommand() {
    const std::array<Byte, 2> seids{3, 4};
    const auto command = opena2dp::avdtp::makeStartCommand(7, seids);
    expect(static_cast<bool>(command), "build Start command");
    const auto packet = opena2dp::avdtp::parseSinglePacket(command.value);
    expect(static_cast<bool>(packet), "parse Start command");
    expect(packet.value.header.signalId == static_cast<Byte>(SignalId::Start),
           "Start signal identifier");
    const std::array<Byte, 2> expectedSeidList{0x0C, 0x10};
    expect(std::equal(packet.value.payload.begin(), packet.value.payload.end(),
                      expectedSeidList.begin(), expectedSeidList.end()),
           "Start command contains the ACP SEID list");

    const std::array<Byte, 2> duplicateSeids{3, 3};
    expect(opena2dp::avdtp::makeStartCommand(7, duplicateSeids).error ==
               Error::InvalidSeid,
           "reject duplicate SEIDs in one Start command");

    const auto suspend = opena2dp::avdtp::makeSuspendCommand(8, seids);
    expect(static_cast<bool>(suspend), "build Suspend command");
    const auto suspendPacket =
        opena2dp::avdtp::parseSinglePacket(suspend.value);
    expect(static_cast<bool>(suspendPacket) &&
               suspendPacket.value.header.signalId ==
                   static_cast<Byte>(SignalId::Suspend),
           "Suspend uses the AVDTP Suspend signal identifier");

    const auto getConfiguration =
        opena2dp::avdtp::makeGetConfigurationCommand(9, 3);
    expect(static_cast<bool>(getConfiguration), "build GetConfiguration command");
    const auto configurationPacket =
        opena2dp::avdtp::parseSinglePacket(getConfiguration.value);
    expect(static_cast<bool>(configurationPacket) &&
               configurationPacket.value.header.signalId ==
                   static_cast<Byte>(SignalId::GetConfiguration) &&
               configurationPacket.value.payload[0] == 0x0C,
           "GetConfiguration encodes its ACP SEID");

    const auto close = opena2dp::avdtp::makeCloseCommand(10, 3);
    expect(static_cast<bool>(close), "build Close command");
    const auto closePacket = opena2dp::avdtp::parseSinglePacket(close.value);
    expect(static_cast<bool>(closePacket) &&
               closePacket.value.header.signalId ==
                   static_cast<Byte>(SignalId::Close),
           "Close uses the AVDTP Close signal identifier");
}

} // namespace

int main() {
    testCommandBuildAndParse();
    testMalformedPacketsAreRejected();
    testDiscoverAccept();
    testServiceCapabilitiesAndSbc();
    testMalformedCapabilitiesAndSetConfiguration();
    testStartCommand();
    std::cout << "AVDTP/SBC protocol tests passed\n";
    return EXIT_SUCCESS;
}
