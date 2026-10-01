#include <rex/platform.h>
#include <rex/platform/dynlib.h>

static_assert(REX_PLATFORM_LINUX || REX_PLATFORM_MAC, "This file is POSIX-only");

#include <dlfcn.h>

#include <rex/filesystem.h>

namespace rex::platform {

DynamicLibrary::~DynamicLibrary() {
  Close();
}

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept : handle_(other.handle_) {
  other.handle_ = nullptr;
}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept {
  if (this != &other) {
    Close();
    handle_ = other.handle_;
    other.handle_ = nullptr;
  }
  return *this;
}

bool DynamicLibrary::Load(const std::filesystem::path& path, SymbolResolution mode) {
  Close();
  int flags = (mode == SymbolResolution::kImmediate) ? RTLD_NOW : RTLD_LAZY;
  handle_ = dlopen(path.c_str(), flags);
  if (handle_ || path.has_parent_path() || path.has_extension()) {
    return handle_ != nullptr;
  }
  // A bare module name, as the recompiled DLL modules are registered
  // ("reeot_GameLogic"): LoadLibrary appends .dll and searches beside the
  // executable, dlopen does neither. Try the library spelling beside the
  // executable, then on the default search path.
  const std::string stem = path.string();
  const std::filesystem::path exe_dir = rex::filesystem::GetExecutableFolder();
  const std::filesystem::path candidates[] = {
      exe_dir / ("lib" + stem + ".so"),
      exe_dir / (stem + ".so"),
      std::filesystem::path("lib" + stem + ".so"),
      std::filesystem::path(stem + ".so"),
  };
  for (const auto& candidate : candidates) {
    handle_ = dlopen(candidate.c_str(), flags);
    if (handle_) {
      return true;
    }
  }
  return false;
}

void DynamicLibrary::Close() {
  if (handle_) {
    dlclose(handle_);
    handle_ = nullptr;
  }
}

void* DynamicLibrary::GetRawSymbol(const char* name) const {
  if (!handle_)
    return nullptr;
  return dlsym(handle_, name);
}

}  // namespace rex::platform
