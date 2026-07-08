#include "settings.hpp"

#include <utility>

namespace torrview {

Settings& Settings::instance() {
  static Settings settings;
  return settings;
}

void Settings::load(const char* executable_dir) {
  path_ = settings_path_for_executable_dir(executable_dir);
  settings_ = load_app_settings(path_);
}

void Settings::save() const { save_app_settings(path_, settings_); }

AppSettings& Settings::values() { return settings_; }

const AppSettings& Settings::values() const { return settings_; }

void Settings::replace(AppSettings settings) { settings_ = clamp_settings(std::move(settings)); }

const std::filesystem::path& Settings::path() const { return path_; }

} // namespace torrview
