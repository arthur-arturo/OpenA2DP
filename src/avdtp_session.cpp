#include "open_a2dp/avdtp_session.hpp"

namespace opena2dp::avdtp {
namespace {

constexpr Byte kTransactionLabelCount = 16;
constexpr Byte kLastTransactionLabel = kTransactionLabelCount - 1;

bool isRequestSignal(Byte signal) noexcept {
    return signal >= static_cast<Byte>(SignalId::Discover) &&
           signal <= static_cast<Byte>(SignalId::DelayReport);
}

} // namespace

Result<Byte> TransactionTable::begin(SignalId requestSignal) noexcept {
    const Byte signal = static_cast<Byte>(requestSignal);
    if (!isRequestSignal(signal)) {
        return {0, Error::InvalidSignalId};
    }

    for (Byte offset = 0; offset < kTransactionLabelCount; ++offset) {
        const Byte label = static_cast<Byte>(
            (nextLabel_ + offset) & kLastTransactionLabel);
        if (!pending_[label].active) {
            pending_[label] = {true, requestSignal};
            nextLabel_ = static_cast<Byte>((label + 1) & kLastTransactionLabel);
            return {label, Error::None};
        }
    }

    return {0, Error::NoTransactionAvailable};
}

Result<CompletedTransaction> TransactionTable::complete(
    const PacketView& response) noexcept {
    const Header& header = response.header;
    if (header.transactionLabel > kLastTransactionLabel) {
        return {{}, Error::InvalidTransactionLabel};
    }
    if (header.packetType != PacketType::Single) {
        return {{}, Error::FragmentationUnsupported};
    }
    if (header.messageType == MessageType::Command) {
        return {{}, Error::UnexpectedMessage};
    }

    auto& pending = pending_[header.transactionLabel];
    if (!pending.active) {
        return {{}, Error::TransactionNotPending};
    }
    if (header.messageType != MessageType::GeneralReject &&
        header.signalId != static_cast<Byte>(pending.signal)) {
        return {{}, Error::UnexpectedSignal};
    }

    const CompletedTransaction completed{
        header.transactionLabel,
        pending.signal,
        header.messageType,
        header.signalId,
    };
    pending.active = false;
    return {completed, Error::None};
}

Error TransactionTable::cancel(Byte transactionLabel) noexcept {
    if (transactionLabel > kLastTransactionLabel) {
        return Error::InvalidTransactionLabel;
    }
    if (!pending_[transactionLabel].active) {
        return Error::TransactionNotPending;
    }
    pending_[transactionLabel].active = false;
    return Error::None;
}

std::size_t TransactionTable::outstanding() const noexcept {
    std::size_t count = 0;
    for (const auto& transaction : pending_) {
        count += transaction.active ? 1 : 0;
    }
    return count;
}

Error SourceStreamState::apply(
    const CompletedTransaction& transaction) noexcept {
    if (transaction.responseType != MessageType::Accept) {
        return transaction.responseType == MessageType::Reject ||
                       transaction.responseType == MessageType::GeneralReject
                   ? Error::None
                   : Error::UnexpectedMessage;
    }

    switch (transaction.requestSignal) {
    case SignalId::SetConfiguration:
        if (state_ != StreamState::Idle) {
            return Error::InvalidStreamState;
        }
        state_ = StreamState::Configured;
        return Error::None;
    case SignalId::Open:
        if (state_ != StreamState::Configured) {
            return Error::InvalidStreamState;
        }
        state_ = StreamState::Open;
        return Error::None;
    case SignalId::Start:
        if (state_ != StreamState::Open) {
            return Error::InvalidStreamState;
        }
        state_ = StreamState::Streaming;
        return Error::None;
    case SignalId::Suspend:
        if (state_ != StreamState::Streaming) {
            return Error::InvalidStreamState;
        }
        state_ = StreamState::Open;
        return Error::None;
    case SignalId::Close:
        if (state_ != StreamState::Open && state_ != StreamState::Streaming) {
            return Error::InvalidStreamState;
        }
        state_ = StreamState::Configured;
        return Error::None;
    case SignalId::Abort:
        state_ = StreamState::Idle;
        return Error::None;
    default:
        // Discovery, capabilities, and configuration queries don't transition
        // the media stream state.
        return Error::None;
    }
}

} // namespace opena2dp::avdtp
