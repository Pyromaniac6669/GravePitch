#include "LatestAnalysisWindowExchange.h"

namespace gravepitch {

std::optional<LatestAnalysisWindowExchange::WriteHandle> LatestAnalysisWindowExchange::tryBeginWrite() noexcept
{
    for (int index = 0; index < slotCount; ++index) {
        auto expected = SlotState::free;
        if (slots_[static_cast<std::size_t>(index)].state.compare_exchange_strong(
                expected, SlotState::writing, std::memory_order_acq_rel)) {
            return WriteHandle {index, slots_[static_cast<std::size_t>(index)].samples.data()};
        }
    }
    return std::nullopt;
}

void LatestAnalysisWindowExchange::publish(const WriteHandle& handle, std::uint64_t endSample) noexcept
{
    auto& slot = slots_[static_cast<std::size_t>(handle.slotIndex)];
    slot.endSample = endSample;
    slot.state.store(SlotState::ready, std::memory_order_release);
}

std::optional<LatestAnalysisWindowExchange::ReadHandle> LatestAnalysisWindowExchange::tryAcquireLatest() noexcept
{
    int latestIndex = -1;
    std::uint64_t latestEndSample = 0;
    std::array<bool, slotCount> readyAtScan {};
    std::array<std::uint64_t, slotCount> endSampleAtScan {};

    for (int index = 0; index < slotCount; ++index) {
        const auto& slot = slots_[static_cast<std::size_t>(index)];
        if (slot.state.load(std::memory_order_acquire) != SlotState::ready) {
            continue;
        }

        readyAtScan[static_cast<std::size_t>(index)] = true;
        endSampleAtScan[static_cast<std::size_t>(index)] = slot.endSample;
        if (latestIndex < 0 || slot.endSample > latestEndSample) {
            latestIndex = index;
            latestEndSample = slot.endSample;
        }
    }

    if (latestIndex < 0) {
        return std::nullopt;
    }

    auto expected = SlotState::ready;
    auto& latest = slots_[static_cast<std::size_t>(latestIndex)];
    if (!latest.state.compare_exchange_strong(expected, SlotState::reading, std::memory_order_acq_rel)) {
        return std::nullopt;
    }

    for (int index = 0; index < slotCount; ++index) {
        if (index == latestIndex
            || !analysis_window_detail::shouldReleaseReadySlot(
                readyAtScan[static_cast<std::size_t>(index)],
                endSampleAtScan[static_cast<std::size_t>(index)],
                latestEndSample)) {
            continue;
        }
        expected = SlotState::ready;
        slots_[static_cast<std::size_t>(index)].state.compare_exchange_strong(
            expected, SlotState::free, std::memory_order_acq_rel);
    }

    return ReadHandle {latestIndex, latest.samples.data(), latest.endSample};
}

void LatestAnalysisWindowExchange::release(const ReadHandle& handle) noexcept
{
    slots_[static_cast<std::size_t>(handle.slotIndex)].state.store(SlotState::free, std::memory_order_release);
}

void LatestAnalysisWindowExchange::reset() noexcept
{
    for (auto& slot : slots_) {
        slot.endSample = 0;
        slot.state.store(SlotState::free, std::memory_order_relaxed);
    }
}

} // namespace gravepitch
