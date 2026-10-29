#pragma once

#include "open_a2dp/avdtp.hpp"

#include <array>
#include <cstddef>

namespace opena2dp::avdtp {

struct CompletedTransaction {
    Byte transactionLabel = 0;
    SignalId requestSignal = SignalId::Discover;
    MessageType responseType = MessageType::Accept;
    Byte responseSignalId = 0;
};

// One AVDTP signaling channel supports 16 outstanding transaction labels.
// Methods are intended to be called serially by the channel's owner thread.
class TransactionTable final {
public:
    Result<Byte> begin(SignalId requestSignal) noexcept;
    Result<CompletedTransaction> complete(const PacketView& response) noexcept;
    Error cancel(Byte transactionLabel) noexcept;
    std::size_t outstanding() const noexcept;

private:
    struct PendingTransaction {
        bool active = false;
        SignalId signal = SignalId::Discover;
    };

    std::array<PendingTransaction, 16> pending_{};
    Byte nextLabel_ = 0;
};

enum class StreamState : Byte {
    Idle,
    Configured,
    Open,
    Streaming,
};

// Tracks state changes only after a matching successful response is received.
class SourceStreamState final {
public:
    Error apply(const CompletedTransaction& transaction) noexcept;
    StreamState state() const noexcept { return state_; }

private:
    StreamState state_ = StreamState::Idle;
};

} // namespace opena2dp::avdtp
