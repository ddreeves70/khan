#include "Library.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <cstring>

namespace kh::plugin::detail {

#if defined(_WIN32)

std::unique_ptr<Library> Library::open(const std::filesystem::path& path, std::string& error) {
    HMODULE h = LoadLibraryW(path.wstring().c_str());
    if (!h) {
        error = "LoadLibrary failed with error " + std::to_string(GetLastError());
        return nullptr;
    }
    return std::make_unique<Library>(static_cast<void*>(h));
}

Library::~Library() {
    FreeLibrary(static_cast<HMODULE>(handle_));
}

Library::AnyFn Library::rawSymbol(const char* name) const {
    FARPROC p = GetProcAddress(static_cast<HMODULE>(handle_), name);
    AnyFn fn = nullptr;
    static_assert(sizeof(fn) == sizeof(p));
    std::memcpy(&fn, &p, sizeof(fn));
    return fn;
}

#else

std::unique_ptr<Library> Library::open(const std::filesystem::path& path, std::string& error) {
    // RTLD_LOCAL keeps each plugin's own copy of Khan's libraries to itself.
    void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        const char* why = dlerror();
        error = why ? why : "dlopen failed";
        return nullptr;
    }
    return std::make_unique<Library>(h);
}

Library::~Library() {
    dlclose(handle_);
}

Library::AnyFn Library::rawSymbol(const char* name) const {
    void* p = dlsym(handle_, name);
    AnyFn fn = nullptr;
    static_assert(sizeof(fn) == sizeof(p));
    std::memcpy(&fn, &p, sizeof(fn));  // object pointer to function pointer, without a cast warning
    return fn;
}

#endif

}  // namespace kh::plugin::detail
