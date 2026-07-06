#pragma once

#include <filesystem>
#include <string>

namespace torrview {

struct AppSettings {
  int font_size = 13;
  int buffer_size_mib = 256;
  int upload_speed_kib = 1024;
  int max_connections = 200;
  std::string proxy_url;
  bool proxy_peer_connections = true;
  bool proxy_tracker_connections = true;
  bool proxy_dns = true;
};

[[nodiscard]] AppSettings clamp_settings(AppSettings settings);
[[nodiscard]] std::filesystem::path settings_path_for_executable_dir(const char* base_path);
[[nodiscard]] AppSettings load_app_settings(const std::filesystem::path& path);
void save_app_settings(const std::filesystem::path& path, const AppSettings& settings);

} // namespace torrview
