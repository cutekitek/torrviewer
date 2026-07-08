#pragma once

#include "app_settings.hpp"

#include <filesystem>

namespace torrview {

class Settings {
public:
  static Settings& instance();

  Settings(const Settings&) = delete;
  Settings& operator=(const Settings&) = delete;

  void load(const char* executable_dir);
  void save() const;

  AppSettings& values();
  const AppSettings& values() const;
  void replace(AppSettings settings);

  const std::filesystem::path& path() const;

private:
  Settings() = default;

  AppSettings settings_;
  std::filesystem::path path_;
};

} // namespace torrview
