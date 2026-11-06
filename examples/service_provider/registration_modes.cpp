#include "../../serviceProv.h"

#include <iostream>
#include <memory>
#include <vector>

class Counter {
private:
    int value;

public:
    explicit Counter(int initialValue) : value(initialValue) {}

    void increment(int amount) {
        value += amount;
    }

    int getValue() const {
        return value;
    }
};

int main() {
    service::ServiceProvider<Counter> provider;
    service::Context context("registration-modes");

    // This overload copies the concrete object into the provider.
    Counter prototype(3);
    provider.addInstance(context, prototype);

    // This overload shares ownership with the caller; no raw pointer is used.
    auto sharedCounter = std::make_shared<Counter>(10);
    provider.addInstance(context, sharedCounter);

    provider.callMethod(context, &Counter::increment, 1);
    const std::vector<int> values = provider.callMethodGet(context, &Counter::getValue);

    std::cout << "Provider copy: " << values.at(0) << '\n';
    std::cout << "Original object: " << prototype.getValue() << '\n';
    std::cout << "Shared object: " << sharedCounter->getValue() << '\n';

    // erase removes the provider's ownership; sharedCounter remains alive here
    // because this scope still owns its shared_ptr.
    provider.erase(context);
}
