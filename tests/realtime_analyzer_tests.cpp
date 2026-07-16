#include "RealtimePitchAnalyzer.h"
#include "gravepitch/core/Tuning.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {

using AnalysisConfigurationSeqlock = gravepitch::realtime_analyzer_detail::AnalysisConfigurationSeqlock;
using MidiNotes = std::array<int, AnalysisConfigurationSeqlock::stringCount>;

struct TestGate {
    std::atomic<bool> armed {false};
    std::atomic<bool> entered {false};
    std::atomic<bool> release {false};
    std::atomic<bool> timedOut {false};
    std::chrono::steady_clock::time_point deadline;
};

bool waitForSignal(
    const std::atomic<bool>& signal,
    std::chrono::steady_clock::time_point deadline)
{
    while (!signal.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

void blockAtStoreMidpoint(void* context) noexcept
{
    auto& gate = *static_cast<TestGate*>(context);
    gate.entered.store(true, std::memory_order_release);
    while (!gate.release.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() >= gate.deadline) {
            gate.timedOut.store(true, std::memory_order_release);
            break;
        }
        std::this_thread::yield();
    }
}

void blockOneWorkerIteration(void* context) noexcept
{
    auto& gate = *static_cast<TestGate*>(context);
    if (!gate.armed.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    gate.entered.store(true, std::memory_order_release);
    while (!gate.release.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() >= gate.deadline) {
            gate.timedOut.store(true, std::memory_order_release);
            break;
        }
        std::this_thread::yield();
    }
}

bool testConfigurationSnapshot()
{
    AnalysisConfigurationSeqlock request;
    const gravepitch::Tuning standard {"standard", "Standard", {40, 45, 50, 55, 59, 64}};
    const MidiNotes expected {40, 45, 50, 55, 59, 64};
    double actualA4Hz = 0.0;
    MidiNotes actual {};
    unsigned int appliedSequence = 0;

    request.store(432.0, standard);
    if (!request.loadIfChanged(appliedSequence, actualA4Hz, actual)
        || actualA4Hz != 432.0
        || actual != expected) {
        std::cerr << "FAIL: configuration request did not return one complete snapshot\n";
        return false;
    }
    if (request.loadIfChanged(appliedSequence, actualA4Hz, actual)) {
        std::cerr << "FAIL: unchanged configuration request was reported twice\n";
        return false;
    }

    return true;
}

bool testConfigurationWriteInProgressIsNotReadable()
{
    constexpr auto timeout = std::chrono::seconds(2);
    AnalysisConfigurationSeqlock request;
    const gravepitch::Tuning standard {"standard", "Standard", {40, 45, 50, 55, 59, 64}};
    const gravepitch::Tuning dropC {"drop_c", "Drop C", {36, 43, 48, 53, 57, 62}};
    const MidiNotes dropCNotes {36, 43, 48, 53, 57, 62};
    const MidiNotes mixedNotes {36, 43, 48, 55, 59, 64};
    TestGate gate;
    gate.deadline = std::chrono::steady_clock::now() + timeout;

    request.store(432.0, standard);
    request.setStoreMidpointHook(blockAtStoreMidpoint, &gate);

    std::thread writer([&] {
        request.store(448.0, dropC);
    });

    if (!waitForSignal(gate.entered, gate.deadline)) {
        gate.release.store(true, std::memory_order_release);
        writer.join();
        std::cerr << "FAIL: configuration writer did not reach the midpoint\n";
        return false;
    }

    unsigned int appliedSequence = 0;
    double actualA4Hz = 0.0;
    MidiNotes actual {};
    const bool readDuringWrite = request.loadIfChanged(appliedSequence, actualA4Hz, actual);

    gate.release.store(true, std::memory_order_release);
    writer.join();

    if (gate.timedOut.load(std::memory_order_acquire)) {
        std::cerr << "FAIL: configuration midpoint gate timed out\n";
        return false;
    }
    if (readDuringWrite) {
        if (actualA4Hz == 448.0 && actual == mixedNotes) {
            std::cerr << "FAIL: configuration reader exposed the known mixed midpoint payload\n";
        } else {
            std::cerr << "FAIL: configuration reader exposed an in-progress payload\n";
        }
        return false;
    }
    if (!request.loadIfChanged(appliedSequence, actualA4Hz, actual)
        || actualA4Hz != 448.0
        || actual != dropCNotes) {
        std::cerr << "FAIL: completed configuration was not readable after midpoint release\n";
        return false;
    }

    return true;
}

bool testConcurrentConfigurationSnapshots()
{
    constexpr int requiredSnapshots = 1000;
    constexpr auto timeout = std::chrono::seconds(5);
    AnalysisConfigurationSeqlock request;
    const gravepitch::Tuning standard {"standard", "Standard", {40, 45, 50, 55, 59, 64}};
    const gravepitch::Tuning dropC {"drop_c", "Drop C", {36, 43, 48, 53, 57, 62}};
    const MidiNotes standardNotes {40, 45, 50, 55, 59, 64};
    const MidiNotes dropCNotes {36, 43, 48, 53, 57, 62};
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::atomic<int> publishedTransactions {0};
    std::atomic<int> acknowledgedTransactions {0};
    std::atomic<bool> readerDone {false};
    std::atomic<bool> writerDone {false};
    std::atomic<bool> invalidSnapshot {false};
    std::atomic<int> observedSnapshots {0};

    std::thread writer([&] {
        for (int transaction = 1; transaction <= requiredSnapshots; ++transaction) {
            if (readerDone.load(std::memory_order_acquire)
                || std::chrono::steady_clock::now() >= deadline) {
                readerDone.store(true, std::memory_order_release);
                break;
            }

            if ((transaction & 1) != 0) {
                request.store(432.0, standard);
            } else {
                request.store(448.0, dropC);
            }
            publishedTransactions.store(transaction, std::memory_order_release);

            while (acknowledgedTransactions.load(std::memory_order_acquire) < transaction) {
                if (readerDone.load(std::memory_order_acquire)
                    || std::chrono::steady_clock::now() >= deadline) {
                    readerDone.store(true, std::memory_order_release);
                    break;
                }
                std::this_thread::yield();
            }
            if (acknowledgedTransactions.load(std::memory_order_acquire) < transaction) {
                break;
            }
        }
        writerDone.store(true, std::memory_order_release);
    });

    std::thread reader([&] {
        unsigned int appliedSequence = 0;
        double actualA4Hz = 0.0;
        MidiNotes actual {};
        for (int transaction = 1; transaction <= requiredSnapshots; ++transaction) {
            while (publishedTransactions.load(std::memory_order_acquire) < transaction) {
                if (writerDone.load(std::memory_order_acquire)
                    || std::chrono::steady_clock::now() >= deadline) {
                    readerDone.store(true, std::memory_order_release);
                    break;
                }
                std::this_thread::yield();
            }
            if (publishedTransactions.load(std::memory_order_acquire) < transaction) {
                break;
            }

            if (!request.loadIfChanged(appliedSequence, actualA4Hz, actual)) {
                invalidSnapshot.store(true, std::memory_order_relaxed);
                readerDone.store(true, std::memory_order_release);
                break;
            }

            const bool expectedStandard = (transaction & 1) != 0;
            const bool completeSnapshot = expectedStandard
                ? actualA4Hz == 432.0 && actual == standardNotes
                : actualA4Hz == 448.0 && actual == dropCNotes;
            if (!completeSnapshot) {
                invalidSnapshot.store(true, std::memory_order_relaxed);
                readerDone.store(true, std::memory_order_release);
                break;
            }

            observedSnapshots.store(transaction, std::memory_order_relaxed);
            acknowledgedTransactions.store(transaction, std::memory_order_release);
        }
        readerDone.store(true, std::memory_order_release);
    });

    writer.join();
    reader.join();

    if (invalidSnapshot.load(std::memory_order_relaxed)) {
        std::cerr << "FAIL: configuration request returned a mixed snapshot\n";
        return false;
    }
    if (!writerDone.load(std::memory_order_acquire)) {
        std::cerr << "FAIL: configuration request writer did not stop\n";
        return false;
    }
    if (observedSnapshots.load(std::memory_order_relaxed) < requiredSnapshots) {
        std::cerr << "FAIL: configuration request reader observed too few overlapping snapshots\n";
        return false;
    }

    return true;
}

bool testRealtimeAnalyzer()
{
    constexpr double sampleRate = 48000.0;
    constexpr double frequency = 110.0;
    constexpr double pi = 3.14159265358979323846;

    gravepitch::RealtimeReadout realtimeReadout;
    RealtimePitchAnalyzer analyzer(realtimeReadout);
    const auto standard = gravepitch::tuningById("standard");
    if (!standard) {
        std::cerr << "FAIL: standard tuning is unavailable\n";
        return false;
    }

    analyzer.requestConfiguration(440.0, *standard);
    if (!analyzer.prepare(sampleRate)) {
        std::cerr << "FAIL: analyzer thread did not start\n";
        return false;
    }

    std::uint64_t sampleIndex = 0;
    for (int hop = 0; hop < 10; ++hop) {
        for (int i = 0; i < 1024; ++i, ++sampleIndex) {
            const auto value = static_cast<float>(std::sin(2.0 * pi * frequency * sampleIndex / sampleRate));
            analyzer.pushMonoSample(value);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }

    const auto detected = analyzer.loadCurrentReadout();
    analyzer.release();

    if (!detected.hasPitch || std::abs(detected.frequencyHz - frequency) > 0.8) {
        std::cerr << "FAIL: analyzer did not publish A2\n";
        return false;
    }
    if (detected.displayPhase != gravepitch::DisplayPhase::tracking) {
        std::cerr << "FAIL: valid pitch is not tracking\n";
        return false;
    }
    if (realtimeReadout.load().hasPitch) {
        std::cerr << "FAIL: release did not clear readout\n";
        return false;
    }

    return true;
}

bool testRuntimeConfigurationChangeClearsReadoutWithoutAnotherWindow()
{
    constexpr double sampleRate = 48000.0;
    constexpr double frequency = 110.0;
    constexpr double pi = 3.14159265358979323846;
    constexpr auto clearTimeout = std::chrono::milliseconds(500);

    gravepitch::RealtimeReadout realtimeReadout;
    RealtimePitchAnalyzer analyzer(realtimeReadout);
    TestGate workerGate;
    analyzer.setWorkerIterationHook(blockOneWorkerIteration, &workerGate);
    const auto standard = gravepitch::tuningById("standard");
    const auto dropC = gravepitch::tuningById("drop_c");
    if (!standard || !dropC) {
        std::cerr << "FAIL: runtime-change tunings are unavailable\n";
        return false;
    }

    analyzer.requestConfiguration(440.0, *standard);
    if (!analyzer.prepare(sampleRate)) {
        std::cerr << "FAIL: runtime-change analyzer thread did not start\n";
        return false;
    }

    std::uint64_t sampleIndex = 0;
    const auto pushHop = [&] {
        for (int i = 0; i < 1024; ++i, ++sampleIndex) {
            const auto value = static_cast<float>(std::sin(2.0 * pi * frequency * sampleIndex / sampleRate));
            analyzer.pushMonoSample(value);
        }
    };

    const auto detectionDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!analyzer.loadCurrentReadout().hasPitch
        && std::chrono::steady_clock::now() < detectionDeadline) {
        pushHop();
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    if (!analyzer.loadCurrentReadout().hasPitch) {
        analyzer.release();
        std::cerr << "FAIL: runtime-change test did not establish A2\n";
        return false;
    }

    workerGate.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    workerGate.armed.store(true, std::memory_order_release);
    if (!waitForSignal(workerGate.entered, workerGate.deadline)) {
        workerGate.release.store(true, std::memory_order_release);
        analyzer.release();
        std::cerr << "FAIL: worker did not stop before applying the runtime configuration\n";
        return false;
    }

    pushHop();
    analyzer.requestConfiguration(432.0, *dropC);
    const bool underlyingOldReadoutStillPresent = realtimeReadout.load().hasPitch;
    const bool filteredReadoutImmediatelyIdle = !analyzer.loadCurrentReadout().hasPitch;
    workerGate.release.store(true, std::memory_order_release);

    if (!underlyingOldReadoutStillPresent) {
        analyzer.release();
        std::cerr << "FAIL: worker gate did not preserve the old underlying readout\n";
        return false;
    }
    if (!filteredReadoutImmediatelyIdle) {
        analyzer.release();
        std::cerr << "FAIL: generation filter did not immediately hide the old readout\n";
        return false;
    }

    bool filteredReadoutStayedIdle = true;
    bool underlyingReadoutCleared = false;
    const auto clearDeadline = std::chrono::steady_clock::now() + clearTimeout;
    while (std::chrono::steady_clock::now() < clearDeadline) {
        filteredReadoutStayedIdle = filteredReadoutStayedIdle && !analyzer.loadCurrentReadout().hasPitch;
        underlyingReadoutCleared = underlyingReadoutCleared || !realtimeReadout.load().hasPitch;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const bool underlyingReadoutStayedCleared = !realtimeReadout.load().hasPitch;
    analyzer.release();

    if (!filteredReadoutStayedIdle) {
        std::cerr << "FAIL: stale readout became visible after configuration change\n";
        return false;
    }
    if (workerGate.timedOut.load(std::memory_order_acquire)) {
        std::cerr << "FAIL: worker iteration gate timed out\n";
        return false;
    }
    if (!underlyingReadoutCleared) {
        std::cerr << "FAIL: worker did not clear the underlying readout without another window\n";
        return false;
    }
    if (!underlyingReadoutStayedCleared) {
        std::cerr << "FAIL: stale in-flight result republished after the underlying readout cleared\n";
        return false;
    }

    return true;
}

} // namespace

int main()
{
    if (!testConfigurationSnapshot()
        || !testConfigurationWriteInProgressIsNotReadable()
        || !testConcurrentConfigurationSnapshots()
        || !testRealtimeAnalyzer()
        || !testRuntimeConfigurationChangeClearsReadoutWithoutAnotherWindow()) {
        return EXIT_FAILURE;
    }

    std::cout << "Realtime analyzer tests passed\n";
    return EXIT_SUCCESS;
}
