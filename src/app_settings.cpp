#include "app_settings.hpp"

#include "logger.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string_view>

namespace torrview {
namespace {

std::string trim_copy(std::string_view value) {
  std::size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }

  std::size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }

  return std::string(value.substr(begin, end - begin));
}

bool parse_bool(std::string value, bool fallback) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  if (value == "1" || value == "true" || value == "yes" || value == "on") {
    return true;
  }
  if (value == "0" || value == "false" || value == "no" || value == "off") {
    return false;
  }
  return fallback;
}

int parse_int(std::string_view value, int fallback) {
  try {
    std::size_t consumed = 0;
    const int parsed = std::stoi(std::string(value), &consumed);
    return consumed == value.size() ? parsed : fallback;
  } catch (const std::exception&) {
    return fallback;
  }
}

void apply_setting(AppSettings& settings, std::string key, std::string value) {
  std::transform(key.begin(), key.end(), key.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

  if (key == "font_size") {
    settings.font_size = parse_int(value, settings.font_size);
  } else if (key == "buffer_size_mib") {
    settings.buffer_size_mib = parse_int(value, settings.buffer_size_mib);
  } else if (key == "upload_speed_kib") {
    settings.upload_speed_kib = parse_int(value, settings.upload_speed_kib);
  } else if (key == "max_connections") {
    settings.max_connections = parse_int(value, settings.max_connections);
  } else if (key == "proxy_url") {
    settings.proxy_url = std::move(value);
  } else if (key == "proxy_peer_connections") {
    settings.proxy_peer_connections = parse_bool(std::move(value), settings.proxy_peer_connections);
  } else if (key == "proxy_tracker_connections") {
    settings.proxy_tracker_connections =
        parse_bool(std::move(value), settings.proxy_tracker_connections);
  } else if (key == "proxy_dns") {
    settings.proxy_dns = parse_bool(std::move(value), settings.proxy_dns);
  }
}

const char* bool_text(bool value) { return value ? "true" : "false"; }

} // namespace

AppSettings clamp_settings(AppSettings settings) {
  settings.font_size = std::clamp(settings.font_size, 9, 24);
  settings.buffer_size_mib = std::clamp(settings.buffer_size_mib, 1, 1024 * 1024);
  settings.upload_speed_kib = std::clamp(settings.upload_speed_kib, 0, 1024 * 1024);
  settings.max_connections = std::clamp(settings.max_connections, 1, 100000);
  return settings;
}

std::filesystem::path settings_path_for_executable_dir(const char* base_path) {
  std::filesystem::path directory =
      base_path != nullptr && base_path[0] != '\0' ? std::filesystem::path(base_path)
                                                   : std::filesystem::current_path();
  return directory / "torrview.ini";
}

AppSettings load_app_settings(const std::filesystem::path& path) {
  AppSettings settings;
  std::ifstream input(path);
  if (!input) {
    save_app_settings(path, settings);
    return settings;
  }

  std::string line;
  while (std::getline(input, line)) {
    const std::string trimmed = trim_copy(line);
    if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';' || trimmed[0] == '[') {
      continue;
    }

    const std::size_t equals = trimmed.find('=');
    if (equals == std::string::npos) {
      continue;
    }

    apply_setting(settings, trim_copy(std::string_view(trimmed).substr(0, equals)),
                  trim_copy(std::string_view(trimmed).substr(equals + 1)));
  }

  settings = clamp_settings(std::move(settings));
  save_app_settings(path, settings);
  return settings;
}

void save_app_settings(const std::filesystem::path& path, const AppSettings& settings) {
  const AppSettings clamped = clamp_settings(settings);
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);

  std::ofstream output(path, std::ios::trunc);
  if (!output) {
    TORRVIEW_LOG_WARNING("Failed to write settings file: " << path.string());
    return;
  }

  output << "[torrview]\n";
  output << "font_size=" << clamped.font_size << '\n';
  output << "buffer_size_mib=" << clamped.buffer_size_mib << '\n';
  output << "upload_speed_kib=" << clamped.upload_speed_kib << '\n';
  output << "max_connections=" << clamped.max_connections << '\n';
  output << "proxy_url=" << clamped.proxy_url << '\n';
  output << "proxy_peer_connections=" << bool_text(clamped.proxy_peer_connections) << '\n';
  output << "proxy_tracker_connections=" << bool_text(clamped.proxy_tracker_connections) << '\n';
  output << "proxy_dns=" << bool_text(clamped.proxy_dns) << '\n';
}

} // namespace torrview
