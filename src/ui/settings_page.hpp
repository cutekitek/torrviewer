#pragma once

#include "app_settings.hpp"
#include "window_state.hpp"

#include "clay.h"

#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace torrview::ui {

enum class SettingsPageAction {
  none,
  close,
  save,
};

class SettingsPage {
public:
  void initialize_ids();
  void begin_frame();
  void open(const AppSettings& settings);
  void set_font_size(int font_size);
  void build(const WindowMetrics& metrics);
  SettingsPageAction hit_test_click();
  void append_text(std::string_view value);
  void backspace();
  void focus_next();
  void write_settings(AppSettings& settings) const;
  bool has_focus() const;

private:
  enum class Field {
    none,
    font_size,
    upload_speed,
    max_connections,
    proxy_url,
  };

  std::string_view retain_frame_text(std::string value);
  void text(std::string_view value, uint16_t font_size, Clay_Color color,
            Clay_TextElementConfigWrapMode wrap = CLAY_TEXT_WRAP_NONE);
  void input_row(std::string_view label, Clay_ElementId id, Field field,
                 std::string_view value, std::string_view suffix = {});
  void toggle_row(std::string_view label, Clay_ElementId id, bool value);
  [[nodiscard]] uint16_t scaled_font_size(uint16_t font_size) const;
  [[nodiscard]] float line_height(uint16_t font_size) const;
  [[nodiscard]] int int_field(std::string_view value, int fallback) const;

  Clay_ElementId close_id_ = {};
  Clay_ElementId save_id_ = {};
  Clay_ElementId font_size_id_ = {};
  Clay_ElementId upload_speed_id_ = {};
  Clay_ElementId max_connections_id_ = {};
  Clay_ElementId proxy_url_id_ = {};
  Clay_ElementId proxy_peer_id_ = {};
  Clay_ElementId proxy_tracker_id_ = {};
  Clay_ElementId proxy_dns_id_ = {};
  Field focused_field_ = Field::none;
  int font_size_ = 13;
  std::string font_size_text_;
  std::string upload_speed_text_;
  std::string max_connections_text_;
  std::string proxy_url_text_;
  bool proxy_peer_connections_ = true;
  bool proxy_tracker_connections_ = true;
  bool proxy_dns_ = true;
  std::deque<std::string> frame_strings_;
};

} // namespace torrview::ui
