#include "dynamic_library.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace rouen::helpers::dynamic_library {

    void* load(std::filesystem::path const& path) {
#if defined(_WIN32)
        // LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR lets a plugin's own runtime
        // dependencies (if any) resolve from beside the plugin file
        // rather than only from the process's own search path.
        return static_cast<void*>(LoadLibraryExW(
            path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR));
#else
        return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    }

    void* get_symbol(void* handle, std::string_view name) {
        std::string const name_str{name};
#if defined(_WIN32)
        return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name_str.c_str())); // NOLINT
#else
        return dlsym(handle, name_str.c_str());
#endif
    }

    std::string last_error() {
#if defined(_WIN32)
        DWORD const error = GetLastError();
        if (error == 0) {
            return {};
        }
        LPSTR buffer = nullptr;
        DWORD const size = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR>(&buffer), 0, nullptr); // NOLINT
        std::string message{buffer, size};
        HeapFree(GetProcessHeap(), 0, buffer);
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
            message.pop_back();
        }
        return message;
#else
        char const* message = dlerror();
        return message ? std::string{message} : std::string{};
#endif
    }

    std::string_view platform_extension() {
#if defined(_WIN32)
        return ".dll";
#elif defined(__APPLE__)
        return ".dylib";
#else
        return ".so";
#endif
    }

} // namespace rouen::helpers::dynamic_library
