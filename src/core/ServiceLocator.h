#ifndef SERVICELOCATOR_H
#define SERVICELOCATOR_H

#include <memory>
#include <map>
#include <typeindex>
#include <mutex>

class ServiceLocator {
public:
    template<typename T>
    static void registerService(std::shared_ptr<T> service) {
        std::lock_guard<std::mutex> lock(instance().m_mutex);
        instance().m_services[typeid(T)] = service;
    }

    template<typename T>
    static std::shared_ptr<T> get() {
        std::lock_guard<std::mutex> lock(instance().m_mutex);
        auto it = instance().m_services.find(typeid(T));
        if (it != instance().m_services.end()) {
            return std::static_pointer_cast<T>(it->second);
        }
        return nullptr;
    }

private:
    ServiceLocator() = default;
    static ServiceLocator& instance() {
        static ServiceLocator inst;
        return inst;
    }

    std::map<std::type_index, std::shared_ptr<void>> m_services;
    std::mutex m_mutex;
};

#endif // SERVICELOCATOR_H
