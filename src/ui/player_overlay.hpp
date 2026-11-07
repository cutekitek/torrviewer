#pragma once

#include "gl_api.hpp"
#include "mpv_player.hpp"
#include "settings.hpp"
#include "torrent_service.hpp"
#include "ui/page.hpp"
#include "ui/widgets.hpp"
#include "window_state.hpp"

#include "clay.h"

#include <chrono>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace torrview {
class Application;
}

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
  toggle_debug_overlay,
  focus_cache_size,
  select_audio_track,
  set_cache_size,
  fullscreen,
  stop,
};

class PlayerOverlay : public Page {
public:
  PlayerOverlay(Application& app, Settings& settings, PointerState& pointer,
                Clay_Context* clay_context, MpvPlayer* player, TorrentService& torrent_service);
  void open();
  void draw(RenderContext& render_context) override;
  bool handle_event(const SDL_Event& event) override;
  void close() override;

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
  void close_audio_menu();
  void close_menus();

private:
  struct BitrateSample {
    double video_bps = 0.0;
    double audio_bps = 0.0;
  };

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

  const char* icon_path(Icon icon) const;
  void control_button(Clay_ElementId id, Icon icon, float glyph = 20.0F);
  void text_button(Clay_ElementId id, std::string_view label, bool active);
  void audio_track_row(Clay_ElementId id, std::string_view label, bool active);
  void timeline(const PlaybackSnapshot& snapshot, const TorrentSnapshot& torrent, float width);
  void piece_rail(const TorrentSnapshot& torrent, float width);
  void status_overlay(const PlaybackSnapshot& snapshot, const TorrentSnapshot& torrent);
  void debug_overlay(const PlaybackSnapshot& snapshot, const TorrentSnapshot& torrent,
                     float max_width);
  void stat_row(std::string_view key, std::string value, Clay_Color color);
  void bitrate_graph(float width);
  void update_bitrate_history(const PlaybackSnapshot& snapshot);
  bool key_down(const SDL_KeyboardEvent& key);
  void text_input(const SDL_TextInputEvent& text);
  void mouse_motion(const SDL_MouseMotionEvent& motion);
  void mouse_button(const SDL_MouseButtonEvent& button);
  void dispatch_action(PlayerOverlayAction action);
  void update_volume_from_pointer();
  void update_controls_visibility(const WindowMetrics& metrics);
  void return_to_file_browser_after_eof();
  [[nodiscard]] bool controls_visible() const;
  [[nodiscard]] bool has_player_file() const;

  Application& app_;
  Settings& settings_;
  PointerState& pointer_;
  Clay_Context* clay_context_ = nullptr;
  MpvPlayer* player_ = nullptr;
  TorrentService& torrent_service_;
  Widgets ui_;
  bool ids_initialized_ = false;
  bool controls_visible_ = true;
  bool dragging_volume_ = false;
  std::chrono::steady_clock::time_point controls_last_hover_;
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
  Clay_ElementId debug_button_id_ = {};
  Clay_ElementId fullscreen_id_ = {};
  Clay_ElementId stop_id_ = {};
  bool audio_menu_open_ = false;
  bool buffer_menu_open_ = false;
  bool debug_overlay_enabled_ = false;
  double last_duration_ = 0.0;
  double selected_seek_seconds_ = 0.0;
  std::size_t selected_audio_track_index_ = 0;
  int selected_cache_size_mib_ = 256;
  int last_rendered_cache_size_mib_ = 256;
  bool buffer_input_focused_ = false;
  std::string buffer_input_text_;
  std::vector<Clay_ElementId> audio_track_row_ids_;
  std::deque<BitrateSample> bitrate_history_;
  std::chrono::steady_clock::time_point last_bitrate_sample_;
  std::string bitrate_history_path_;
};

} // namespace torrview::ui
