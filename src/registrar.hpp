#pragma once

#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

class registrar {
public:
    // Adds or updates a service of a specific type and key
    template <typename T>
    static void add(const std::string& key, std::shared_ptr<T> service) {
        std::lock_guard<std::mutex> lock(getMutex());
        getTypeMap<T>()[key] = service;
    }

    // Retrieves a service of a specific type and key
    template <typename T>
    static std::shared_ptr<T> get(const std::string& key) {
        std::lock_guard<std::mutex> lock(getMutex());

        auto& typeMap = getTypeMap<T>();
        auto it = typeMap.find(key);
        if (it == typeMap.end()) {
            throw std::runtime_error("Service not found for the given key");
        }
        return it->second;
    }

    // Retrieves all keys for a given type
    template <typename T>
    static void keys(std::function<void(std::string const&)> sink) {
        std::lock_guard<std::mutex> lock(getMutex());

        auto& typeMap = getTypeMap<T>();
        for (const auto& [key, _] : typeMap) {
            sink(key);
        }
    }

    template<typename T>
    static void all(std::function<void(std::string const&, std::shared_ptr<T>)> sink) {
        std::lock_guard<std::mutex> lock(getMutex());

        auto& typeMap = getTypeMap<T>();
        for (const auto& [key, p] : typeMap) {
            sink(key, p);
        }
    }

    // Removes a service from the list
    template <typename T>
    static void remove(std::string const &key) {
        std::lock_guard<std::mutex> lock(getMutex());
        getTypeMap<T>().erase(key);
    }

    class call_fn_str {
    public:
        call_fn_str(std::string const &name) : svc_{registrar::get<std::function<void(std::string const&)>>(name)} {}
        void operator()(std::string const &p) const {
            (*svc_)(p);
        }
    private:
        std::shared_ptr<std::function<void(std::string const &)>> svc_;
    };

    template<typename result_t>
    class call_fn_ret {
    public:
        call_fn_ret(std::string const &name) : svc_{registrar::get<std::function<result_t()>>(name)} {}

        [[nodiscard]] result_t operator()() const {
            return (*svc_)();
        }
    private:
        std::shared_ptr<std::function<result_t()>> svc_;
    };
    
    // New class to support functions with two parameters (string and shared_ptr)
    class call_fn_str_ptr {
    public:
        call_fn_str_ptr(std::string const &name) : 
            svc_{registrar::get<std::function<void(std::string const&, std::shared_ptr<std::function<void(std::string)>>)>>(name)} {}
        
        void operator()(std::string const &cmd, std::shared_ptr<std::function<void(std::string)>> callback) const {
            (*svc_)(cmd, callback);
        }
    private:
        std::shared_ptr<std::function<void(std::string const&, std::shared_ptr<std::function<void(std::string)>>)>> svc_;
    };

private:
    // Type-erased storage for different service types
    template <typename T>
    using TypeMap = std::unordered_map<std::string, std::shared_ptr<T>>;

    // Access the type-specific map
    template <typename T>
    static TypeMap<T>& getTypeMap() {
        static TypeMap<T> typeMap;
        return typeMap;
    }

    // Provides a reference to the mutex
    static std::mutex& getMutex() {
        static std::mutex mutex;
        return mutex;
    }
};

inline registrar::call_fn_str operator ""_sfn(const char* str, size_t) {
    return registrar::call_fn_str{std::string{str}};
}

inline registrar::call_fn_ret<bool> operator ""_fnb(const char* str, size_t) {
    return registrar::call_fn_ret<bool>{std::string{str}};
}

inline registrar::call_fn_ret<std::string> operator""_fns(const char* str, size_t) {
    return registrar::call_fn_ret<std::string>{std::string{str}};
}

inline registrar::call_fn_str_ptr operator ""_sfn2(const char* str, size_t) {
    return registrar::call_fn_str_ptr{std::string{str}};
}
