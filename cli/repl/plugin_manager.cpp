#include "plugin_manager.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace xsql::cli {
namespace {

std::string shared_library_suffix() {
#if defined(_WIN32)
  return ".dll";
#elif defined(__APPLE__)
  return ".dylib";
#else
  return ".so";
#endif
}

std::vector<std::string> split_paths(const char* env_value) {
  std::vector<std::string> paths;
  if (!env_value || !*env_value) {
    return paths;
  }
  std::stringstream ss(env_value);
  std::string item;
  while (std::getline(ss, item, ':')) {
    if (!item.empty()) {
      paths.push_back(item);
    }
  }
  return paths;
}

}  // namespace

PluginManager::PluginManager(CommandRegistry& registry) : registry_(registry) {
  host_context_.manager = this;
}

bool PluginManager::register_command(void* host_context,
                                     const char* name,
                                     const char* help,
                                     XsqlPluginCommandFn fn,
                                     void* user_data,
                                     char* out_error,
                                     size_t out_error_size) {
  if (!host_context || !name || !*name || !fn) {
    if (out_error && out_error_size > 0) {
      std::snprintf(out_error, out_error_size, "Invalid plugin command registration.");
    }
    return false;
  }
  auto* ctx = static_cast<HostContext*>(host_context);
  if (!ctx->manager) {
    if (out_error && out_error_size > 0) {
      std::snprintf(out_error, out_error_size, "Plugin host context is not available.");
    }
    return false;
  }
  std::string command_name = name;
  if (!command_name.empty() && command_name[0] == '.') {
    command_name.erase(0, 1);
  }
  if (command_name.empty()) {
    if (out_error && out_error_size > 0) {
      std::snprintf(out_error, out_error_size, "Plugin command name is empty.");
    }
    return false;
  }
  std::string plugin_name = ctx->current_plugin;
  for (const auto& info : ctx->manager->command_info_) {
    if (info.name == command_name && info.plugin_name == plugin_name) {
      return true;
    }
  }
  std::string prefix = "." + command_name;
  ctx->manager->registry_.add([prefix, fn, user_data, plugin_name, manager = ctx->manager](
                                  const std::string& line,
                                  CommandContext& ctx) -> bool {
    if (line.rfind(prefix, 0) != 0) {
      return false;
    }
    if (!plugin_name.empty() && !manager->is_loaded(plugin_name)) {
      std::cerr << "Error: plugin not loaded: " << plugin_name << std::endl;
      ctx.editor.reset_render_state();
      return true;
    }
    char err[256] = {0};
    bool ok = fn(line.c_str(), user_data, err, sizeof(err));
    if (!ok && err[0]) {
      std::cerr << "Error: " << err << std::endl;
    }
    ctx.editor.reset_render_state();
    return true;
  });
  ctx->manager->command_info_.push_back(
      PluginCommandInfo{command_name, help ? help : "", plugin_name});
  return true;
}

bool PluginManager::register_tokenizer(void* host_context,
                                       const char* lang,
                                       XsqlTokenizerFn fn,
                                       void* user_data,
                                       char* out_error,
                                       size_t out_error_size) {
  if (!host_context || !lang || !*lang || !fn) {
    if (out_error && out_error_size > 0) {
      std::snprintf(out_error, out_error_size, "Invalid tokenizer registration.");
    }
    return false;
  }
  auto* ctx = static_cast<HostContext*>(host_context);
  if (!ctx->manager) {
    if (out_error && out_error_size > 0) {
      std::snprintf(out_error, out_error_size, "Plugin host context is not available.");
    }
    return false;
  }
  ctx->manager->tokenizers_[lang] = TokenizerEntry{fn, user_data, ctx->current_plugin};
  return true;
}

void PluginManager::print_message(void*,
                                  const char* message,
                                  bool is_error) {
  if (!message) {
    return;
  }
  if (is_error) {
    std::cerr << message << std::endl;
  } else {
    std::cout << message << std::endl;
  }
}

bool PluginManager::tokenize(const std::string& lang,
                             const std::string& text,
                             std::vector<std::string>& tokens,
                             std::string& error) const {
  auto it = tokenizers_.find(lang);
  if (it == tokenizers_.end()) {
    error = "Tokenizer not available for language: " + lang;
    return false;
  }
  size_t cap = std::max<size_t>(4096, text.size() * 4 + 1024);
  cap = std::min<size_t>(cap, 4 * 1024 * 1024);
  std::string buffer;
  buffer.resize(cap);
  char err[256] = {0};
  if (!it->second.fn(text.c_str(),
                     it->second.user_data,
                     buffer.data(),
                     buffer.size(),
                     err,
                     sizeof(err))) {
    error = err[0] ? err : "Tokenizer failed.";
    return false;
  }
  tokens.clear();
  std::stringstream ss(buffer);
  std::string line;
  while (std::getline(ss, line)) {
    if (!line.empty()) {
      tokens.push_back(line);
    }
  }
  return true;
}

bool PluginManager::has_tokenizer(const std::string& lang) const {
  return tokenizers_.find(lang) != tokenizers_.end();
}

bool PluginManager::is_loaded(const std::string& name) const {
  if (name.empty()) {
    return false;
  }
  for (const auto& plugin : plugins_) {
    if (plugin.name == name || plugin.path == name) {
      return true;
    }
    std::filesystem::path path = plugin.path;
    if (!path.stem().empty() && path.stem() == name) {
      return true;
    }
  }
  return false;
}

std::string PluginManager::resolve_plugin_path(const std::string& name_or_path) const {
  if (looks_like_path(name_or_path)) {
    return name_or_path;
  }
  std::string suffix = shared_library_suffix();
  std::vector<std::string> search_paths;
  auto env_paths = split_paths(std::getenv("XSQL_PLUGIN_PATH"));
  search_paths.insert(search_paths.end(), env_paths.begin(), env_paths.end());
  search_paths.emplace_back("plugins");
  search_paths.emplace_back("plugins/bin");

  for (const auto& base : search_paths) {
    std::filesystem::path root = base;
    std::filesystem::path direct = root / (name_or_path + suffix);
    if (std::filesystem::exists(direct)) {
      return direct.string();
    }
    std::filesystem::path prefixed = root / ("lib" + name_or_path + suffix);
    if (std::filesystem::exists(prefixed)) {
      return prefixed.string();
    }
  }
  return name_or_path + suffix;
}

bool PluginManager::looks_like_path(const std::string& value) {
  return value.find('/') != std::string::npos || value.find('\\') != std::string::npos;
}

bool PluginManager::load(const std::string& name_or_path, std::string& error) {
  if (is_loaded(name_or_path)) {
    return true;
  }
  std::string path = resolve_plugin_path(name_or_path);
#if defined(_WIN32)
  HMODULE handle = LoadLibraryA(path.c_str());
  if (!handle) {
    error = "Failed to load plugin: " + path;
    return false;
  }
  auto register_fn =
      reinterpret_cast<XsqlRegisterPluginFn>(GetProcAddress(handle, "xsql_register_plugin"));
  if (!register_fn) {
    error = "Plugin missing xsql_register_plugin: " + path;
    FreeLibrary(handle);
    return false;
  }
#else
  void* handle = dlopen(path.c_str(), RTLD_NOW);
  if (!handle) {
    error = std::string("Failed to load plugin: ") + path + " (" + dlerror() + ")";
    return false;
  }
  auto register_fn =
      reinterpret_cast<XsqlRegisterPluginFn>(dlsym(handle, "xsql_register_plugin"));
  if (!register_fn) {
    error = std::string("Plugin missing xsql_register_plugin: ") + path;
    dlclose(handle);
    return false;
  }
#endif
  XsqlPluginHost host{};
  host.api_version = XSQL_PLUGIN_API_VERSION;
  host.host_context = &host_context_;
  host.register_command = &PluginManager::register_command;
  host.register_tokenizer = &PluginManager::register_tokenizer;
  host.print = &PluginManager::print_message;

  char err[256] = {0};
  host_context_.current_plugin = name_or_path;
  if (!register_fn(&host, err, sizeof(err))) {
    error = err[0] ? err : "Plugin registration failed.";
#if defined(_WIN32)
    FreeLibrary(handle);
#else
    dlclose(handle);
#endif
    host_context_.current_plugin.clear();
    return false;
  }
  host_context_.current_plugin.clear();

  plugins_.push_back(LoadedPlugin{name_or_path, path, handle});
  plugin_info_.push_back(PluginInfo{name_or_path, path});
  return true;
}

bool PluginManager::unload(const std::string& name, std::string& error) {
  auto it = std::find_if(plugins_.begin(), plugins_.end(),
                         [&name](const LoadedPlugin& plugin) {
                           if (plugin.name == name || plugin.path == name) {
                             return true;
                           }
                           std::filesystem::path path = plugin.path;
                           return !path.stem().empty() && path.stem() == name;
                         });
  if (it == plugins_.end()) {
    error = "Plugin not loaded: " + name;
    return false;
  }
  std::string plugin_name = it->name;
  for (auto iter = tokenizers_.begin(); iter != tokenizers_.end();) {
    if (iter->second.plugin_name == plugin_name) {
      iter = tokenizers_.erase(iter);
    } else {
      ++iter;
    }
  }
  plugin_info_.erase(std::remove_if(plugin_info_.begin(), plugin_info_.end(),
                                    [&plugin_name](const PluginInfo& info) {
                                      return info.name == plugin_name;
                                    }),
                     plugin_info_.end());
#if defined(_WIN32)
  if (it->handle) {
    FreeLibrary(static_cast<HMODULE>(it->handle));
  }
#else
  if (it->handle) {
    dlclose(it->handle);
  }
#endif
  plugins_.erase(it);
  return true;
}

const std::vector<PluginInfo>& PluginManager::plugins() const {
  return plugin_info_;
}

const std::vector<PluginCommandInfo>& PluginManager::commands() const {
  return command_info_;
}

}  // namespace xsql::cli
