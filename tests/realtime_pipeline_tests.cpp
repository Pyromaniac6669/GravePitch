#include "LatestAnalysisWindowExchange.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace {
int failures = 0;

void expectTrue(bool condition, const char* message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void publishFilled(gravepitch::LatestAnalysisWindowExchange& exchange, float value, std::uint64_t endSample)
{
    auto write = exchange.tryBeginWrite();
    expectTrue(write.has_value(), "slot is available");
    if (!write) {
        return;
    }
    std::fill_n(write->samples, gravepitch::LatestAnalysisWindowExchange::windowSize, value);
    exchange.publish(*write, endSample);
}
}

int main()
{
    gravepitch::LatestAnalysisWindowExchange exchange;
    expectTrue(!exchange.tryAcquireLatest().has_value(), "empty exchange has no window");
    expectTrue(
        !gravepitch::analysis_window_detail::shouldReleaseReadySlot(false, 4096, 3072),
        "window published after the scan is not released");
    expectTrue(
        gravepitch::analysis_window_detail::shouldReleaseReadySlot(true, 2048, 4096),
        "visible older window is released");

    publishFilled(exchange, 1.0f, 1024);
    auto first = exchange.tryAcquireLatest();
    expectTrue(first.has_value(), "published window can be acquired");
    expectTrue(first && first->endSample == 1024, "window keeps sample sequence");
    bool allSamplesMatch = first.has_value();
    if (first) {
        for (int index = 0; index < gravepitch::LatestAnalysisWindowExchange::windowSize; ++index) {
            allSamplesMatch = allSamplesMatch && first->samples[index] == 1.0f;
        }
    }
    expectTrue(allSamplesMatch, "window keeps all samples");
    if (first) {
        exchange.release(*first);
    }

    publishFilled(exchange, 1.0f, 2048);
    publishFilled(exchange, 2.0f, 3072);
    publishFilled(exchange, 3.0f, 4096);
    expectTrue(!exchange.tryBeginWrite().has_value(), "full exchange rejects without blocking");

    auto latest = exchange.tryAcquireLatest();
    expectTrue(latest && latest->endSample == 4096, "consumer acquires newest sequence");
    expectTrue(latest && latest->samples[0] == 3.0f, "newest samples are intact");
    const auto reclaimedFirst = exchange.tryBeginWrite();
    const auto reclaimedSecond = exchange.tryBeginWrite();
    expectTrue(reclaimedFirst.has_value(), "first stale slot is reclaimed while latest is reading");
    expectTrue(reclaimedSecond.has_value(), "second stale slot is reclaimed while latest is reading");
    expectTrue(!exchange.tryBeginWrite().has_value(), "reading and writing slots remain protected");

    if (latest) {
        exchange.release(*latest);
    }
    exchange.reset();

    if (failures != 0) {
        return EXIT_FAILURE;
    }
    std::cout << "Realtime pipeline tests passed\n";
    return EXIT_SUCCESS;
}
