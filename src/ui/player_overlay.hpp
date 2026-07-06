#pragma once

#include "mpv_player.hpp"
#include "torrent_service.hpp"
#include "window_state.hpp"

#include "clay.h"

#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace torrview::ui {

enum class PlayerOverlayAction {
  none,
  toggle_pause,
  seek_back,
  seek_forward,
  seek_absolute,
  set_volume,
  toggle_audio_menu,
  toggle_buffer_menu,
  focus_cache_size,
  select_audio_track,
  set_cache_size,
  fullscreen,
  stop,
};

class PlayerOverlay {
public:
  void initialize_ids();
  void begin_frame();
  void build(const WindowMetrics& metrics, const PlaybackSnapshot& snapshot,
             const TorrentSnapshot& torrent, bool fullscreen, bool controls_visible);
  PlayerOverlayAction hit_test_click();
  bool pointer_over_controls() const;
  bool pointer_over_volume_bar() const;
  double volume_from_pointer(float pointer_x) const;
  double seek_seconds_from_pointer(float pointer_x) const;
  double selected_seek_seconds() const;
  std::size_t selected_audio_track_index() const;
  int selected_cache_size_mib() const;
  bool cache_size_input_focused() const;
  void append_cache_size_text(std::string_view value);
  void backspace_cache_size_text();
  void commit_cache_size_text();
  void set_font_size(int font_size);
  void close_audio_menu();
  void close_menus();

private:
  enum class Icon {
    play,
    pause,
    rewind,
    forward,
    volume,
    fullscreen,
    window,
    close,
    audio,
  };

  std::string_view retain_frame_text(std::string value);
  void text(std::string_view value, uint16_t font_size, Clay_Color color,
            Clay_TextElementConfigWrapMode wrap = CLAY_TEXT_WRAP_NONE);
  const char* icon_path(Icon icon) const;
  void icon(Icon icon, float size = 18.0F);
  void icon_button(Clay_ElementId id, Icon icon, bool emphasized = false);
  void audio_track_row(Clay_ElementId id, std::string_view label, bool active);
  void cache_size_input(std::string_view value, bool focused);
  void piece_rail(const TorrentSnapshot& torrent, float width);
  void status_overlay(const PlaybackSnapshot& snapshot, const TorrentSnapshot& torrent);
  [[nodiscard]] uint16_t scaled_font_size(uint16_t font_size) const;
  [[nodiscard]] float line_height(uint16_t font_size) const;

  Clay_ElementId root_id_ = {};
  Clay_ElementId play_id_ = {};
  Clay_ElementId control_panel_id_ = {};
  Clay_ElementId timeline_id_ = {};
  Clay_ElementId seek_back_id_ = {};
  Clay_ElementId seek_forward_id_ = {};
  Clay_ElementId volume_bar_id_ = {};
  Clay_ElementId audio_button_id_ = {};
  Clay_ElementId audio_menu_id_ = {};
  Clay_ElementId buffer_button_id_ = {};
  Clay_ElementId buffer_menu_id_ = {};
  Clay_ElementId buffer_input_id_ = {};
  Clay_ElementId buffer_apply_id_ = {};
  Clay_ElementId fullscreen_id_ = {};
  Clay_ElementId stop_id_ = {};
  bool audio_menu_open_ = false;
  bool buffer_menu_open_ = false;
  double last_duration_ = 0.0;
  double selected_seek_seconds_ = 0.0;
  std::size_t selected_audio_track_index_ = 0;
  int selected_cache_size_mib_ = 256;
  int last_rendered_cache_size_mib_ = 256;
  int font_size_ = 13;
  bool buffer_input_focused_ = false;
  std::string buffer_input_text_;
  std::vector<Clay_ElementId> audio_track_row_ids_;
  std::deque<std::string> frame_strings_;
};

} // namespace torrview::ui
