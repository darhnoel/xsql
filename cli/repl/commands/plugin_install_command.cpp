#include "plugin_install_command.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "plugin_registry.h"

namespace xsql::cli {
namespace {

int run_command(const std::string& command,
                bool verbose,
                const std::filesystem::path& log_path) {
  if (verbose) {
    return std::system(command.c_str());
  }
#if defined(_WIN32)
  return std::system(command.c_str());
#else
  std::string quiet =
      command + " > \"" + log_path.string() + "\" 2>&1";
  return std::system(quiet.c_str());
#endif
}

}  // namespace

CommandHandler make_plugin_install_command() {
  return [](const std::string& line, CommandContext&) -> bool {
    if (line.rfind(".plugin_install", 0) != 0) {
      return false;
    }
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;
    std::string arg;
    bool verbose = false;
    while (iss >> arg) {
      if (arg == "--verbose") {
        verbose = true;
        continue;
      }
      break;
    }
    if (arg.empty()) {
      std::cerr << "Usage: .plugin_install <name> [--verbose] | .plugin_install list" << std::endl;
      return true;
    }

    std::vector<PluginRegistryEntry> entries;
    std::string error;
    if (!load_plugin_registry(entries, error)) {
      std::cerr << "Error: " << error << std::endl;
      return true;
    }

    if (arg == "list") {
      if (entries.empty()) {
        std::cout << "No plugins registered." << std::endl;
        return true;
      }
      std::cout << "Available plugins:" << std::endl;
      for (const auto& entry : entries) {
        std::cout << "  " << entry.name << " (" << entry.repo << ")" << std::endl;
      }
      return true;
    }

    const PluginRegistryEntry* match = find_plugin_entry(entries, arg);
    if (!match) {
      std::cerr << "Error: plugin not found in registry: " << arg << std::endl;
      return true;
    }

    bool use_local_source = match->repo == "local";
    std::filesystem::path plugin_root =
        use_local_source ? std::filesystem::current_path()
                         : std::filesystem::path("plugins") / "src" / match->name;
    std::filesystem::path source_root = plugin_root / match->path;
    std::filesystem::path wrapper_root =
        std::filesystem::path("plugins") / "cmake" / match->name;
    std::filesystem::path cmake_root = source_root;
    if (std::filesystem::exists(wrapper_root / "CMakeLists.txt")) {
      cmake_root = wrapper_root;
    }
    std::filesystem::path build_dir = cmake_root / "build";
    std::filesystem::path bin_root = std::filesystem::path("plugins") / "bin";
    std::filesystem::create_directories(bin_root);

    std::filesystem::path log_dir =
        std::filesystem::path("plugins") / ".xsql_logs" / match->name;
    std::filesystem::create_directories(log_dir);
    std::filesystem::path git_log = log_dir / "git.log";
    std::filesystem::path cmake_log = log_dir / "cmake.log";

    if (!use_local_source) {
      if (std::filesystem::exists(plugin_root) && !std::filesystem::exists(plugin_root / ".git")) {
        std::cerr << "Error: plugin source already exists but is not a git repo: "
                  << plugin_root << std::endl;
        std::cerr << "Run: .plugin remove " << match->name << " (or delete the folder) and try again."
                  << std::endl;
        return true;
      }

      if (std::filesystem::exists(plugin_root / ".git")) {
        std::cout << "Step 1/2: Updating source..." << std::endl;
        std::string pull_cmd = "git -C \"" + plugin_root.string() + "\" pull --ff-only";
        if (run_command(pull_cmd, verbose, git_log) != 0) {
          std::cerr << "Error: git pull failed for " << match->name << std::endl;
          if (!verbose && std::filesystem::exists(git_log)) {
            std::ifstream in(git_log);
            std::cerr << "Details:\n" << in.rdbuf() << std::endl;
          }
          return true;
        }
      } else {
        std::cout << "Step 1/2: Cloning source..." << std::endl;
        std::filesystem::create_directories(plugin_root.parent_path());
        std::string clone_cmd =
            "git clone \"" + match->repo + "\" \"" + plugin_root.string() + "\"";
        if (run_command(clone_cmd, verbose, git_log) != 0) {
          std::cerr << "Error: git clone failed for " << match->name << std::endl;
          if (!verbose && std::filesystem::exists(git_log)) {
            std::ifstream in(git_log);
            std::cerr << "Details:\n" << in.rdbuf() << std::endl;
          }
          return true;
        }
      }
    } else {
      if (!std::filesystem::exists(source_root)) {
        std::cerr << "Error: local plugin source not found: " << source_root << std::endl;
        return true;
      }
      std::cout << "Step 1/2: Using local source..." << std::endl;
    }

    std::cout << "Step 2/2: Building plugin..." << std::endl;
    std::string xsql_root = std::filesystem::current_path().string();
    std::string plugin_source = std::filesystem::absolute(plugin_root).string();
    std::string cmake_config =
        "cmake -S \"" + cmake_root.string() + "\" -B \"" + build_dir.string() +
        "\" -DXSQL_ROOT=\"" + xsql_root + "\" -DPLUGIN_SOURCE=\"" +
        plugin_source + "\"";
    if (run_command(cmake_config, verbose, cmake_log) != 0) {
      std::cerr << "Error: CMake configure failed for " << match->name << std::endl;
      if (!verbose && std::filesystem::exists(cmake_log)) {
        std::ifstream in(cmake_log);
        std::cerr << "Details:\n" << in.rdbuf() << std::endl;
      }
      return true;
    }
    std::string cmake_build =
        "cmake --build \"" + build_dir.string() + "\"";
    if (run_command(cmake_build, verbose, cmake_log) != 0) {
      std::cerr << "Error: CMake build failed for " << match->name << std::endl;
      if (!verbose && std::filesystem::exists(cmake_log)) {
        std::ifstream in(cmake_log);
        std::cerr << "Details:\n" << in.rdbuf() << std::endl;
      }
      return true;
    }

    if (!match->artifact.empty()) {
      std::string artifact = match->artifact;
      size_t pos = artifact.find("{ext}");
      if (pos != std::string::npos) {
#if defined(_WIN32)
        const char* ext = ".dll";
#elif defined(__APPLE__)
        const char* ext = ".dylib";
#else
        const char* ext = ".so";
#endif
        artifact.replace(pos, 5, ext);
      }
      std::filesystem::path artifact_path = build_dir / artifact;
      if (!std::filesystem::exists(artifact_path)) {
        std::cerr << "Error: artifact not found: " << artifact_path << std::endl;
        return true;
      }
      std::filesystem::path dest_path = bin_root / artifact_path.filename();
      std::filesystem::copy_file(artifact_path, dest_path,
                                 std::filesystem::copy_options::overwrite_existing);
      std::cout << "Installed plugin binary: " << dest_path << std::endl;
    } else {
      std::cout << "Build completed. Configure plugin binary path in registry if needed." << std::endl;
    }

    std::cout << "Plugin installed: " << match->name << std::endl;
    std::cout << "Load with: .plugin load " << match->name << std::endl;
    return true;
  };
}

}  // namespace xsql::cli
