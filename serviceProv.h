#ifndef SERVICE_PROV_H // include guard
#define SERVICE_PROV_H

#include <vector>
#include <unordered_map>
#include <string>
#include <typeinfo>
#include <chrono>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <functional>
#include <type_traits>

//
namespace service{

    class Context{
private:
    std::string name;
public:

    Context(std::string _name) : name(_name) {}
    Context() : name("Default") {}

    std::string getContext(){
        return name;
    }
    std::string getName(){
        return name;
    }
};

#define CONTEXT std::shared_ptr<service::Context>

template <class T>
class ServiceProvider{
private:
    std::unordered_map<std::string,std::vector<T>> objs;
    std::unordered_map<std::string,CONTEXT> contextMap;
    std::thread garbageCollector_thread;
    std::atomic_bool collect;
    std::shared_mutex garbageMutex;
    std::mutex contextsMutex;
    int garbageWait;
public:
    
    ServiceProvider(){
       ServiceProvider(1000);
    }

    ServiceProvider(int _garbageWait){
        garbageWait = _garbageWait;
        garbageCollector_thread = std::thread(this, &ServiceProvider::checkContexts_thread);
        collect = true;
    }

    ~ServiceProvider(){
    }

    CONTEXT registerContext(std::string contextID){
        return registerContext(Context(contextID));
    }

    CONTEXT registerContext(Context& context){
        auto newContext = std::make_shared<Context>(context);
        return registerContext(newContext);
    }

    CONTEXT registerContext(CONTEXT context){
        std::string contextID = context.get().getContext();

        auto it = contextMap.find(contextID);
        if (it == contextMap.end()) {
            // Crie um novo contexto se ele não existir
            contextMap[contextID] = context;
            return context;
        }

        return it->second;
    }

    template <class CallClass>
    CONTEXT addInstance(Context& context, CallClass& callClass){
        garbageMutex.unlock_shared();
        std::lock_guard<std::mutex> slock(contextsMutex);

        CONTEXT resultRef = registerContext(context);

        if(objs.find(context.getContext()) == objs.end()){
            std::vector<T> classVect;
            classVect.push_back(getNewInstance(callClass));
            objs[context.getContext()] = classVect;
        } else {
            std::vector<T>& classT = objs[context.getContext()];
            classT.push_back(getNewInstance(callClass));
        }

        return resultRef;
    }

    template <class CallClass>
    CONTEXT addInstance(CONTEXT context, CallClass& callClass){
        return addInstance(*context.get(), callClass);
    }

    template <typename AlternativeClass>
    CONTEXT addInstance(Context& context){
        garbageMutex.unlock_shared();
        std::lock_guard<std::mutex> slock(contextsMutex);

        CONTEXT resultRef = registerContext(context);

        if(objs.find(context.getContext()) == objs.end()){
            std::vector<T> classVect;
            classVect.push_back(getNewInstance<AlternativeClass>());
            objs[context.getContext()] = classVect;
        } else {
            std::vector<T>& classT = objs[context.getContext()];
            classT.push_back(getNewInstance<AlternativeClass>());
        }

        return resultRef;
    }

    template <typename AlternativeClass>
    CONTEXT addInstance(CONTEXT context){
        return addInstance<AlternativeClass>(*context.get());
    }

    CONTEXT addInstance(Context& context, T& obj){
        garbageMutex.unlock_shared();
        std::lock_guard<std::mutex> slock(contextsMutex);

        CONTEXT resultRef = registerContext(context);

        if(objs.find(context.getContext()) == objs.end()){
            std::vector<T> classVect;
            classVect.push_back(obj);
            objs[context.getContext()] = classVect;
        } else {
            std::vector<T>& classT = objs[context.getContext()];
            classT.push_back(obj);
        }

        return resultRef;
    }

    CONTEXT addInstance(CONTEXT context, T& obj){
        return addInstance(*context.get(), obj);
    }

    template <typename Method, typename... Args>
    void callMethod(Context& context, Method method, Args... args) const {
        garbageMutex.unlock_shared();
        std::lock_guard<std::mutex> slock(contextsMutex);

        if(objs.find(context.getContext()) == objs.end()){
            std::__throw_logic_error("no instace class for context" + context.getName());
        } else {
            if(objs.find(context.getContext()) != objs.end()){
                for (T& instance : objs[context.getContext()]) {
                    (instance.*method)(args...);
                }
            }
        }
    }

    template <typename Method, typename... Args>
    void callMethod(CONTEXT context, Method method, Args... args) const {
        callMethod(context.get(), method, args...);
    }

    template <typename Method, typename... Args>
    std::vector<typename std::result_of<Method(T, Args...)>::type> callMethodGet(Context& context, Method method, Args... args) const {
        garbageMutex.unlock_shared();
        std::lock_guard<std::mutex> slock(contextsMutex);

        if(objs.find(context.getContext()) == objs.end()){
            std::__throw_logic_error("no instace class for context" + context.getName());
        } else {
            std::vector<typename std::result_of<Method(T, Args...)>::type> result;
            if(objs.find(context.getContext()) != objs.end()){
                for (T& instance : objs[context.getContext()]) {
                    result.push_back((instance.*method)(args...));
                }
            }
            return result;
        }
    }

    template <typename Method, typename... Args>
    std::vector<typename std::result_of<Method(T, Args...)>::type> callMethodGet(CONTEXT context, Method method, Args... args) const {
        return callMethodGet(*context.get(), method, args...);
    }

    //get new instance off service by call class type
    template <class CallClass>
    T getNewInstance(CallClass& callClass){
        return getNewInstance(typeid(callClass));
    }

    template <typename CallClassType>
    T getNewInstance(){
        return getNewInstance(typeid(CallClassType));
    }

    //get new instance off service by call class type
    virtual T getNewInstance(std::type_info& type){
        //Example
        if(type == typeid(T)){
            return T();
        } else {
            return T();
        }
    }

    void erase(Context& context){
        erase(context.getContext());
    }

    void erase(std::string contextId){
        auto it = objs.find(contextId);
        if (it != objs.end()) {
            objs.erase(it);
        }

        auto itc = contextMap.find(contextId);
        if (itc != contextMap.end()) {
            contextMap.erase(itc);
        }
    }

    private:

    void checkContexts_thread(){
        while(collect == true){
            garbageMutex.lock();
            for (const auto& pair : contextMap) {
                if(pair.second.use_count == 1){
                    erase(pair.first);
                }
            }
            garbageMutex.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(garbageWait));
        }
    }

};

}

#endif
