#include "serviceProv.h"

#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <vector>

struct BenchService {
    std::size_t calls = 0;

    void hit() noexcept {
        ++calls;
    }

    std::size_t count() const noexcept {
        return calls;
    }
};

int main() {
    constexpr std::size_t iterations = 100000;
    const std::vector<std::size_t> instanceCounts{1, 8, 64, 256};

    for (const std::size_t instanceCount : instanceCounts) {
        service::ServiceProvider<BenchService> provider;
        service::Context namedContext("benchmark-context-with-a-long-name");
        const auto context = provider.registerContext(namedContext);
        std::vector<std::shared_ptr<BenchService>> directInstances;
        directInstances.reserve(instanceCount);

        for (std::size_t index = 0; index < instanceCount; ++index) {
            BenchService instance;
            provider.addInstance(namedContext, instance);
            directInstances.push_back(std::make_shared<BenchService>());
        }

        const auto providerStart = std::chrono::steady_clock::now();
        for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
            provider.callMethod(context, &BenchService::hit);
        }
        const auto providerEnd = std::chrono::steady_clock::now();
        const auto providerCounts = provider.callMethodGet(context, &BenchService::count);
        std::size_t providerChecksum = 0;
        for (const std::size_t count : providerCounts) {
            providerChecksum += count;
        }

        const auto directStart = std::chrono::steady_clock::now();
        for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
            for (const auto& instance : directInstances) {
                (instance.get()->*(&BenchService::hit))();
            }
        }
        const auto directEnd = std::chrono::steady_clock::now();
        std::size_t directChecksum = 0;
        for (const auto& instance : directInstances) {
            directChecksum += instance->count();
        }

        const auto providerNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    providerEnd - providerStart)
                                    .count();
        const auto directNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  directEnd - directStart)
                                  .count();
        std::cout << "instances=" << instanceCount
                  << " provider_ns_per_dispatch="
                  << static_cast<double>(providerNs) / iterations
                  << " direct_ns_per_dispatch="
                  << static_cast<double>(directNs) / iterations
                  << " provider_to_direct="
                  << static_cast<double>(providerNs) / directNs
                  << " checksum=" << providerChecksum << ':' << directChecksum << '\n';
    }
}
