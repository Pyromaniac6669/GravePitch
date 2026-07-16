#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

namespace gravepitch {

namespace analysis_window_detail {

constexpr bool shouldReleaseReadySlot(
    bool readyAtScan,
    std::uint64_t endSampleAtScan,
    std::uint64_t latestEndSample) noexcept
{
    return readyAtScan && endSampleAtScan < latestEndSample;
}

} // namespace analysis_window_detail

class LatestAnalysisWindowExchange {
public:
    static constexpr int windowSize = 4096;
    static constexpr int slotCount = 3;

    struct WriteHandle {
        int slotIndex = -1;
        float* samples = nullptr;
    };

    struct ReadHandle {
        int slotIndex = -1;
        const float* samples = nullptr;
        std::uint64_t endSample = 0;
    };

    std::optional<WriteHandle> tryBeginWrite() noexcept;
    void publish(const WriteHandle& handle, std::uint64_t endSample) noexcept;
    std::optional<ReadHandle> tryAcquireLatest() noexcept;
    void release(const ReadHandle& handle) noexcept;
    void reset() noexcept;

private:
    enum class SlotState {
        free,
        writing,
        ready,
        reading
    };

    struct Slot {
        std::array<float, windowSize> samples {};
        std::atomic<SlotState> state {SlotState::free};
        std::uint64_t endSample = 0;
    };

    std::array<Slot, slotCount> slots_;
};

} // namespace gravepitch
