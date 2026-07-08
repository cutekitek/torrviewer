#pragma once

#include "app_settings.hpp"
#include "mpv_player.hpp"
#include "settings.hpp"
#include "torrent_service.hpp"
#include "ui/page.hpp"
#include "window_state.hpp"

#include "clay.h"

#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace torrview {
class Application;
}

namespace torrview::ui {
enum class SettingsPageAction {
  none,
  close,
  save,
};

class SettingsPage : public Page {
public:
  SettingsPage(Application& app, Settings& settings, MpvPlayer* player,
               TorrentService& torrent_service);
  void initialize_ids();
  void begin_frame();
  void open(const AppSettings& settings);
  void draw(RenderContext& render_context) override;
  bool handle_event(const SDL_Event& event) override;
  void close() override;
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
  void handle_click_action(SettingsPageAction action);
  void save_settings();
  void return_to_main_page();
  [[nodiscard]] uint16_t scaled_font_size(uint16_t font_size) const;
  [[nodiscard]] float line_height(uint16_t font_size) const;
  [[nodiscard]] int int_field(std::string_view value, int fallback) const;

  Application& app_;
  Settings& settings_;
  MpvPlayer* player_ = nullptr;
  TorrentService& torrent_service_;
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
