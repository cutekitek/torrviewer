#pragma once

#include "mpv_player.hpp"
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
  set_volume,
  toggle_audio_menu,
  select_audio_track,
  set_cache_size,
  fullscreen,
  stop,
};

class PlayerOverlay {
public:
  void initialize_ids();
  void begin_frame();
  void build(const WindowMetrics& metrics, const PlaybackSnapshot& snapshot, bool fullscreen,
             bool controls_visible, int cache_limit_mib);
  PlayerOverlayAction hit_test_click();
  bool pointer_over_controls() const;
  bool pointer_over_volume_bar() const;
  double volume_from_pointer(float pointer_x) const;
  std::size_t selected_audio_track_index() const;
  int selected_cache_size_mib() const;
  void close_audio_menu();

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
  void cache_preset_button(Clay_ElementId id, int mib, bool active);

  Clay_ElementId play_id_ = {};
  Clay_ElementId control_panel_id_ = {};
  Clay_ElementId seek_back_id_ = {};
  Clay_ElementId seek_forward_id_ = {};
  Clay_ElementId volume_bar_id_ = {};
  Clay_ElementId audio_button_id_ = {};
  Clay_ElementId audio_menu_id_ = {};
  Clay_ElementId fullscreen_id_ = {};
  Clay_ElementId stop_id_ = {};
  bool audio_menu_open_ = false;
  std::size_t selected_audio_track_index_ = 0;
  int selected_cache_size_mib_ = 512;
  std::vector<Clay_ElementId> audio_track_row_ids_;
  std::vector<Clay_ElementId> cache_preset_ids_;
  std::deque<std::string> frame_strings_;
};

} // namespace torrview::ui
