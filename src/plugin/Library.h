#pragma once

// Loads a shared library and finds functions in it (dlopen or LoadLibrary).
// Internal to kh_plugin.

#include <filesystem>
#include <memory>
#include <string>

namespace kh::plugin::detail {

class Library {
public:
    // Loads the library, or returns null and sets `error`.
    static std::unique_ptr<Library> open(const std::filesystem::path& path, std::string& error);

    // Takes ownership of a handle from dlopen or LoadLibrary. Use open().
    explicit Library(void* handle) : handle_(handle) {}
    ~Library();
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;

    // Finds an exported function, or returns null.
    template <typename Fn>
    Fn function(const char* name) const {
        return reinterpret_cast<Fn>(rawSymbol(name));
    }

private:
    using AnyFn = void (*)();
    AnyFn rawSymbol(const char* name) const;
    void* handle_;
};

}  // namespace kh::plugin::detail
