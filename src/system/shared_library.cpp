/**
 * @file        system/shared_library.cpp
 * @brief       Platform-agnostic shared library loader
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <rex/system/shared_library.h>

#include <rex/logging.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace rex::system {

SharedLibrary::~SharedLibrary() {
  Close();
}

SharedLibrary::SharedLibrary(SharedLibrary&& other) noexcept : handle_(other.handle_) {
  other.handle_ = nullptr;
}

SharedLibrary& SharedLibrary::operator=(SharedLibrary&& other) noexcept {
  if (this != &other) {
    Close();
    handle_ = other.handle_;
    other.handle_ = nullptr;
  }
  return *this;
}

bool SharedLibrary::Load(const std::string& name) {
#ifdef _WIN32
  std::string full_name = name + ".dll";
  handle_ = LoadLibraryA(full_name.c_str());
#else
  std::string full_name = "lib" + name + ".so";
  handle_ = dlopen(full_name.c_str(), RTLD_NOW);
#endif
  if (!handle_) {
    REXSYS_ERROR("Failed to load shared library: {}", full_name);
  }
  return handle_ != nullptr;
}

void* SharedLibrary::GetSymbol(const char* name) {
  if (!handle_)
    return nullptr;
#ifdef _WIN32
  return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle_), name));
#else
  return dlsym(handle_, name);
#endif
}

void SharedLibrary::Close() {
  if (handle_) {
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(handle_));
#else
    dlclose(handle_);
#endif
    handle_ = nullptr;
  }
}

}  // namespace rex::system
