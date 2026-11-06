#ifndef SERVICE_PROV_H
#define SERVICE_PROV_H

#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>
#include <unordered_map>

#ifndef SERVICE_PROVIDER_SINGLE_THREAD
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <thread>
#endif

namespace service {

namespace detail {

#ifdef SERVICE_PROVIDER_SINGLE_THREAD
class ProviderMutex {
public:
    void lock() noexcept {}
    void unlock() noexcept {}
};
#else
using ProviderMutex = std::mutex;
#endif

} // namespace detail

class Context {
private:
    std::string name;
    template <class>
    friend class ServiceProvider;

    const std::string& getContextRef() const noexcept {
        return name;
    }

public:
    explicit Context(std::string contextName) : name(std::move(contextName)) {}
    Context() : name("Default") {}

    std::string getContext() const {
        return name;
    }

    std::string getName() const {
        return name;
    }

};

} // namespace service

#define CONTEXT std::shared_ptr<service::Context>

namespace service {

template <class T>
class ServiceProvider {
private:
    using InstanceList = std::vector<std::shared_ptr<T>>;
    std::unordered_map<std::string, std::shared_ptr<const InstanceList>> objs;
    std::unordered_map<std::string, CONTEXT> contextMap;
    mutable detail::ProviderMutex contextsMutex;
#ifndef SERVICE_PROVIDER_SINGLE_THREAD
    std::atomic_bool collect{true};
    std::condition_variable garbageCollectorWake;
    int garbageWait;
    std::thread garbageCollectorThread;
    std::string garbageCursor;
    bool hasGarbageCursor = false;

    static constexpr std::size_t garbageCollectionBatchSize = 4;
#endif

    CONTEXT registerContextLocked(const CONTEXT& context) {
        const std::string& contextId = context->getContextRef();
        const auto it = contextMap.find(contextId);
        if (it != contextMap.end()) {
            return it->second;
        }

        contextMap.emplace(contextId, context);
        return context;
    }

    template <class InstanceFactory>
    CONTEXT addInstanceImpl(Context& context, InstanceFactory&& factory) {
        std::shared_ptr<T> instance = std::forward<InstanceFactory>(factory)();
        if (!instance) {
            throw std::logic_error("ServiceProvider factory returned a null instance");
        }

        std::lock_guard<detail::ProviderMutex> lock(contextsMutex);
        CONTEXT registeredContext = registerContextLocked(std::make_shared<Context>(context));
        auto& currentInstances = objs[registeredContext->getContextRef()];
        auto updatedInstances = currentInstances
            ? std::make_shared<InstanceList>(*currentInstances)
            : std::make_shared<InstanceList>();
        updatedInstances->push_back(std::move(instance));
        currentInstances = std::move(updatedInstances);
        return registeredContext;
    }

    std::shared_ptr<const InstanceList> snapshotInstances(Context& context) const {
        std::shared_ptr<const InstanceList> snapshot;
        {
            std::lock_guard<detail::ProviderMutex> lock(contextsMutex);
            const auto it = objs.find(context.getContextRef());
            if (it == objs.end() || !it->second || it->second->empty()) {
                throw std::out_of_range("No service instances for context: " + context.getName());
            }
            snapshot = it->second;
        }
        return snapshot;
    }

    void eraseContextById(const std::string& contextId) {
        std::shared_ptr<const InstanceList> removedInstances;
        CONTEXT removedContext;
        {
            std::lock_guard<detail::ProviderMutex> lock(contextsMutex);
            const auto instancesIt = objs.find(contextId);
            if (instancesIt != objs.end()) {
                removedInstances = std::move(instancesIt->second);
                objs.erase(instancesIt);
            }
            const auto contextIt = contextMap.find(contextId);
            if (contextIt != contextMap.end()) {
                removedContext = std::move(contextIt->second);
                contextMap.erase(contextIt);
            }
        }
    }

#ifndef SERVICE_PROVIDER_SINGLE_THREAD
    void checkContextsThread() {
        while (collect.load()) {
            {
                std::unique_lock<detail::ProviderMutex> lock(contextsMutex);
                garbageCollectorWake.wait_for(
                    lock,
                    std::chrono::milliseconds(garbageWait),
                    [this] { return !collect.load(); });
            }
            if (!collect.load()) {
                break;
            }

            std::size_t remaining = 0;
            {
                std::lock_guard<detail::ProviderMutex> lock(contextsMutex);
                remaining = contextMap.size();
            }

            while (remaining > 0 && collect.load()) {
                std::vector<std::shared_ptr<const InstanceList>> removedInstances;
                std::vector<CONTEXT> removedContexts;
                std::size_t processed = 0;
                {
                    std::lock_guard<detail::ProviderMutex> lock(contextsMutex);
                    if (contextMap.empty()) {
                        hasGarbageCursor = false;
                        garbageCursor.clear();
                        break;
                    }

                    auto it = contextMap.begin();
                    if (hasGarbageCursor) {
                        const auto cursorIt = contextMap.find(garbageCursor);
                        if (cursorIt != contextMap.end()) {
                            it = cursorIt;
                        } else {
                            hasGarbageCursor = false;
                        }
                    }

                    const std::size_t batchLimit = std::min(
                        garbageCollectionBatchSize,
                        std::min(remaining, contextMap.size()));
                    while (processed < batchLimit && !contextMap.empty()) {
                        const auto current = it;
                        ++it;
                        if (it == contextMap.end()) {
                            it = contextMap.begin();
                        }

                        if (current->second.use_count() == 1) {
                            const auto instancesIt = objs.find(current->first);
                            if (instancesIt != objs.end()) {
                                removedInstances.push_back(std::move(instancesIt->second));
                                objs.erase(instancesIt);
                            }
                            removedContexts.push_back(std::move(current->second));
                            it = contextMap.erase(current);
                            if (it == contextMap.end() && !contextMap.empty()) {
                                it = contextMap.begin();
                            }
                        }

                        ++processed;
                        if (!contextMap.empty()) {
                            garbageCursor = it->first;
                            hasGarbageCursor = true;
                        } else {
                            garbageCursor.clear();
                            hasGarbageCursor = false;
                        }
                    }
                }

                if (processed >= remaining) {
                    remaining = 0;
                } else {
                    remaining -= processed;
                }
                if (remaining > 0) {
                    removedContexts.clear();
                    removedInstances.clear();
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                }
            }
        }
    }
#endif

public:
    ServiceProvider() : ServiceProvider(1000) {}

#ifdef SERVICE_PROVIDER_SINGLE_THREAD
    explicit ServiceProvider(int) {}
#else
    explicit ServiceProvider(int garbageWaitMilliseconds)
        : garbageWait(std::max(garbageWaitMilliseconds, 1)),
          garbageCollectorThread(&ServiceProvider::checkContextsThread, this) {}
#endif

    ServiceProvider(const ServiceProvider&) = delete;
    ServiceProvider& operator=(const ServiceProvider&) = delete;

    virtual ~ServiceProvider() {
#ifndef SERVICE_PROVIDER_SINGLE_THREAD
        collect.store(false);
        garbageCollectorWake.notify_all();
        if (garbageCollectorThread.joinable()) {
            garbageCollectorThread.join();
        }
#endif
        clear();
    }

    CONTEXT registerContext(std::string contextID) {
        Context context(std::move(contextID));
        return registerContext(context);
    }

    CONTEXT registerContext(Context& context) {
        return registerContext(std::make_shared<Context>(context));
    }

    CONTEXT registerContext(CONTEXT context) {
        if (!context) {
            throw std::invalid_argument("ServiceProvider context must not be null");
        }

        std::lock_guard<detail::ProviderMutex> lock(contextsMutex);
        return registerContextLocked(context);
    }

    CONTEXT findContext(const std::string& contextID) const {
        std::lock_guard<detail::ProviderMutex> lock(contextsMutex);
        const auto it = contextMap.find(contextID);
        return it == contextMap.end() ? CONTEXT{} : it->second;
    }

    template <class CallClass>
    CONTEXT addInstance(Context& context, CallClass& callClass) {
        return addInstanceImpl(context, [this, &callClass] {
            return getNewInstance(callClass);
        });
    }

    template <class CallClass>
    CONTEXT addInstance(CONTEXT context, CallClass& callClass) {
        if (!context) {
            throw std::invalid_argument("ServiceProvider context must not be null");
        }
        return addInstance(*context, callClass);
    }

    template <typename AlternativeClass>
    CONTEXT addInstance(Context& context) {
        return addInstanceImpl(context, [this] {
            return getNewInstance<AlternativeClass>();
        });
    }

    template <typename AlternativeClass>
    CONTEXT addInstance(CONTEXT context) {
        if (!context) {
            throw std::invalid_argument("ServiceProvider context must not be null");
        }
        return addInstance<AlternativeClass>(*context);
    }

    CONTEXT addInstance(Context& context, T& obj) {
        return addInstanceImpl(context, [&obj] {
            return std::make_shared<T>(obj);
        });
    }

    CONTEXT addInstance(Context& context, std::shared_ptr<T> obj) {
        if (!obj) {
            throw std::invalid_argument("ServiceProvider instance must not be null");
        }
        return addInstanceImpl(context, [instance = std::move(obj)] { return instance; });
    }

    CONTEXT addInstance(CONTEXT context, T& obj) {
        if (!context) {
            throw std::invalid_argument("ServiceProvider context must not be null");
        }
        return addInstance(*context, obj);
    }

    CONTEXT addInstance(CONTEXT context, std::shared_ptr<T> obj) {
        if (!context) {
            throw std::invalid_argument("ServiceProvider context must not be null");
        }
        return addInstance(*context, std::move(obj));
    }

    template <typename Method, typename... Args>
    void callMethod(Context& context, Method method, Args... args) const {
        const auto instances = snapshotInstances(context);
        for (const std::shared_ptr<T>& instance : *instances) {
            std::invoke(method, *instance, args...);
        }
    }

    template <typename Method, typename... Args>
    void callMethod(CONTEXT context, Method method, Args... args) const {
        if (!context) {
            throw std::invalid_argument("ServiceProvider context must not be null");
        }
        callMethod(*context, method, args...);
    }

    template <typename Method, typename... Args>
    std::vector<std::invoke_result_t<Method, T&, Args...>> callMethodGet(
        Context& context,
        Method method,
        Args... args) const {
        const auto instances = snapshotInstances(context);

        using Result = std::invoke_result_t<Method, T&, Args...>;
        static_assert(!std::is_void_v<Result>, "callMethodGet requires a non-void method");
        std::vector<Result> results;
        results.reserve(instances->size());
        for (const std::shared_ptr<T>& instance : *instances) {
            results.push_back(std::invoke(method, *instance, args...));
        }
        return results;
    }

    template <typename Method, typename... Args>
    std::vector<std::invoke_result_t<Method, T&, Args...>> callMethodGet(
        CONTEXT context,
        Method method,
        Args... args) const {
        if (!context) {
            throw std::invalid_argument("ServiceProvider context must not be null");
        }
        return callMethodGet(*context, method, args...);
    }

    template <class CallClass>
    std::shared_ptr<T> getNewInstance(CallClass& callClass) {
        return getNewInstance(typeid(callClass));
    }

    template <typename CallClassType>
    std::shared_ptr<T> getNewInstance() {
        return getNewInstance(typeid(CallClassType));
    }

    virtual std::shared_ptr<T> getNewInstance(const std::type_info& type) {
        return getNewInstance(const_cast<std::type_info&>(type));
    }

    virtual std::shared_ptr<T> getNewInstance(std::type_info& type) {
        (void)type;
        if constexpr (!std::is_abstract_v<T> && std::is_default_constructible_v<T>) {
            return std::make_shared<T>();
        } else {
            throw std::logic_error("ServiceProvider requires an overriding factory for this service type");
        }
    }

    void erase(Context& context) {
        eraseContextById(context.getContextRef());
    }

    void clear() {
        decltype(objs) removedInstances;
        decltype(contextMap) removedContexts;
        {
            std::lock_guard<detail::ProviderMutex> lock(contextsMutex);
            removedInstances.swap(objs);
            removedContexts.swap(contextMap);
        }
    }

    void erase(std::string contextId) {
        eraseContextById(contextId);
    }

#ifdef SERVICE_PROVIDER_SINGLE_THREAD
    void collectGarbage() {
        decltype(objs) removedInstances;
        decltype(contextMap) removedContexts;
        {
            std::lock_guard<detail::ProviderMutex> lock(contextsMutex);
            for (auto it = contextMap.begin(); it != contextMap.end();) {
                if (it->second.use_count() == 1) {
                    const auto instancesIt = objs.find(it->first);
                    if (instancesIt != objs.end()) {
                        auto instances = std::move(instancesIt->second);
                        objs.erase(instancesIt);
                        removedInstances.emplace(it->first, std::move(instances));
                    }
                    removedContexts.emplace(it->first, std::move(it->second));
                    it = contextMap.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }
#endif
};

} // namespace service

// Declares a provider subclass with the standard requester-based factory hook.
#define SERVICE_PROVIDER(ServiceName, InterfaceType) \
    class ServiceName final : public service::ServiceProvider<InterfaceType> { \
    public: \
        std::shared_ptr<InterfaceType> getNewInstance(std::type_info& requester) override; \
    }

#endif