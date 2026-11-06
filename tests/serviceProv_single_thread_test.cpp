#define SERVICE_PROVIDER_SINGLE_THREAD
#include "../serviceProv.h"

#include <cassert>
#include <memory>
#include <vector>

struct Counter {
    int value = 0;

    void add(int amount) {
        value += amount;
    }

    int get() const {
        return value;
    }
};

int main() {
    service::ServiceProvider<Counter> provider;
    service::Context context("single-thread");
    Counter initial{5};
    provider.addInstance(context, initial);

    provider.callMethod(context, &Counter::add, 3);
    assert((provider.callMethodGet(context, &Counter::get) == std::vector<int>{8}));

    std::weak_ptr<service::Context> expiredContext;
    {
        auto temporaryContext = provider.registerContext("manual-garbage-collection");
        expiredContext = temporaryContext;
    }
    assert(!expiredContext.expired());

    provider.collectGarbage();
    assert(expiredContext.expired());
    assert(!provider.findContext("manual-garbage-collection"));

    provider.clear();
    assert(!provider.findContext("single-thread"));
    assert(provider.registerContext("single-thread"));
}
