#pragma once

#include "gravepitch/core/ReadoutContinuity.h"

#include <atomic>

namespace gravepitch {

struct RealtimeReadoutData {
    bool hasPitch = false;
    bool stable = false;
    double frequencyHz = 0.0;
    double cents = 0.0;
    double confidence = 0.0;
    double rms = 0.0;
    int stringNumber = 0;
    int midiNote = 0;
    int targetMidiNote = 0;
    DisplayPhase displayPhase = DisplayPhase::idle;
    double displayOpacity = 1.0;
};

class RealtimeReadout {
public:
    void publish(const RealtimeReadoutData& data) noexcept
    {
        const auto nextSequence = sequence_.load(std::memory_order_relaxed) + 1;
        sequence_.store(nextSequence, std::memory_order_release);

        hasPitch_.store(data.hasPitch, std::memory_order_relaxed);
        stable_.store(data.stable, std::memory_order_relaxed);
        frequencyHz_.store(data.frequencyHz, std::memory_order_relaxed);
        cents_.store(data.cents, std::memory_order_relaxed);
        confidence_.store(data.confidence, std::memory_order_relaxed);
        rms_.store(data.rms, std::memory_order_relaxed);
        stringNumber_.store(data.stringNumber, std::memory_order_relaxed);
        midiNote_.store(data.midiNote, std::memory_order_relaxed);
        targetMidiNote_.store(data.targetMidiNote, std::memory_order_relaxed);
        displayPhase_.store(data.displayPhase, std::memory_order_relaxed);
        displayOpacity_.store(data.displayOpacity, std::memory_order_relaxed);
        sequence_.store(nextSequence + 1, std::memory_order_release);
    }

    RealtimeReadoutData load() const noexcept
    {
        for (;;) {
            const auto before = sequence_.load(std::memory_order_acquire);
            if ((before & 1U) != 0U) {
                continue;
            }

            RealtimeReadoutData data;
            data.hasPitch = hasPitch_.load(std::memory_order_relaxed);
            data.stable = stable_.load(std::memory_order_relaxed);
            data.frequencyHz = frequencyHz_.load(std::memory_order_relaxed);
            data.cents = cents_.load(std::memory_order_relaxed);
            data.confidence = confidence_.load(std::memory_order_relaxed);
            data.rms = rms_.load(std::memory_order_relaxed);
            data.stringNumber = stringNumber_.load(std::memory_order_relaxed);
            data.midiNote = midiNote_.load(std::memory_order_relaxed);
            data.targetMidiNote = targetMidiNote_.load(std::memory_order_relaxed);
            data.displayPhase = displayPhase_.load(std::memory_order_relaxed);
            data.displayOpacity = displayOpacity_.load(std::memory_order_relaxed);

            const auto after = sequence_.load(std::memory_order_acquire);
            if (before == after) {
                return data;
            }
        }
    }

private:
    std::atomic<unsigned int> sequence_ {0};
    std::atomic<bool> hasPitch_ {false};
    std::atomic<bool> stable_ {false};
    std::atomic<double> frequencyHz_ {0.0};
    std::atomic<double> cents_ {0.0};
    std::atomic<double> confidence_ {0.0};
    std::atomic<double> rms_ {0.0};
    std::atomic<int> stringNumber_ {0};
    std::atomic<int> midiNote_ {0};
    std::atomic<int> targetMidiNote_ {0};
    std::atomic<DisplayPhase> displayPhase_ {DisplayPhase::idle};
    std::atomic<double> displayOpacity_ {1.0};
};

} // namespace gravepitch
