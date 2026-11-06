#include "../serviceProv.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char* argv[]) {
    constexpr std::size_t contextCount = 50000;
    constexpr std::size_t workerCount = 4;
    constexpr std::size_t samplesPerWorker = 10000;
    const int collectionIntervalMs = argc > 1 ? std::stoi(argv[1]) : 1;

    service::ServiceProvider<int> provider(collectionIntervalMs);
    std::vector<std::string> contextNames;
    std::vector<CONTEXT> contextHandles;
    contextNames.reserve(contextCount);
    contextHandles.reserve(contextCount);

    for (std::size_t index = 0; index < contextCount; ++index) {
        contextNames.push_back("context-" + std::to_string(index));
        contextHandles.push_back(provider.registerContext(contextNames.back()));
    }

    std::vector<std::vector<long long>> samples(workerCount);
    std::vector<std::thread> workers;
    workers.reserve(workerCount);

    for (std::size_t worker = 0; worker < workerCount; ++worker) {
        workers.emplace_back([&, worker] {
            auto& workerSamples = samples[worker];
            workerSamples.reserve(samplesPerWorker);
            for (std::size_t sample = 0; sample < samplesPerWorker; ++sample) {
                const std::size_t index = (sample * 37 + worker * 101) % contextCount;
                const auto started = std::chrono::steady_clock::now();
                const auto context = provider.findContext(contextNames[index]);
                const auto finished = std::chrono::steady_clock::now();
                if (!context) {
                    std::terminate();
                }
                workerSamples.push_back(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count());
            }
        });
    }

    for (std::thread& worker : workers) {
        worker.join();
    }

    std::vector<long long> allSamples;
    allSamples.reserve(workerCount * samplesPerWorker);
    for (const auto& workerSamples : samples) {
        allSamples.insert(allSamples.end(), workerSamples.begin(), workerSamples.end());
    }
    std::sort(allSamples.begin(), allSamples.end());

    const auto percentile = [&allSamples](double fraction) {
        const std::size_t index = static_cast<std::size_t>(fraction * (allSamples.size() - 1));
        return allSamples[index];
    };

    std::cout << "contexts=" << contextCount
              << " threads=" << workerCount
              << " collection_interval_ms=" << collectionIntervalMs
              << " findContext_ns_p50=" << percentile(0.50)
              << " p95=" << percentile(0.95)
              << " p99=" << percentile(0.99)
              << " max=" << allSamples.back() << '\n';
}
