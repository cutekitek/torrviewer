#pragma once

#include "gl_api.hpp"
#include "input_source.hpp"
#include "mpv_player.hpp"
#include "sdl_event_manager.hpp"
#include "torrent_service.hpp"
#include "ui/clay_renderer.hpp"
#include "ui/file_browser_page.hpp"
#include "ui/player_overlay.hpp"
#include "ui/title_page.hpp"
#include "window_state.hpp"

#include "clay.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace torrview {

class Application final : private SdlEventHandler {
public:
  Application() = default;
  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;
  ~Application() override;

  void initialize();
  void run();

private:
  using Clock = std::chrono::steady_clock;

  void request_quit() override;
  void window_metrics_changed() override;
  void key_down(const SDL_KeyboardEvent& key) override;
  void mouse_motion(const SDL_MouseMotionEvent& motion) override;
  void mouse_button(const SDL_MouseButtonEvent& button) override;
  void mouse_wheel(const SDL_MouseWheelEvent& wheel) override;
  void drop_file(const char* path) override;
  void drop_text(const char* text) override;
  void drop_begin() override;
  void drop_complete() override;
  void drop_position(float x, float y) override;

  void set_gl_attribute(SDL_GLAttr attribute, int value);
  template <typename Function> Function load_gl_proc(const char* name);
  void load_basic_gl_api();
  void initialize_text_renderer();
  void destroy_text_renderer();
  void initialize_clay();
  void show_open_source_dialog();
  void paste_clipboard();
  void consume_dialog_results();
  void accept_input(ParsedInput input);
  void handle_file_browser_click();
  void handle_player_click();
  void handle_player_action(ui::PlayerOverlayAction action);
  void update_volume_from_pointer();
  void update_player_controls_visibility(Clock::time_point now);
  void return_to_torrent_file_browser_after_eof();
  void toggle_fullscreen();
  bool is_fullscreen() const;
  void update_window_metrics();
  void update_window_title();
  void render();
  void render_file_browser_screen(float delta_time);
  void render_title_screen(float delta_time);
  void render_player_screen(float delta_time);
  void log_startup() const;

  bool running_ = false;
  bool sdl_initialized_ = false;
  bool drag_active_ = false;
  bool dragging_volume_ = false;
  bool player_controls_visible_ = true;
  bool thorvg_initialized_ = false;
  int cache_limit_mib_ = 256;
  SDL_Window* window_ = nullptr;
  SDL_GLContext context_ = nullptr;
  const char* font_name_ = "TorrviewNotoSans";
  BasicGlApi basic_gl_;
  WindowMetrics metrics_;
  PointerState pointer_;
  Clay_Arena clay_arena_ = {};
  Clay_Context* clay_context_ = nullptr;
  ui::TextMeasureState text_measure_state_;
  Clock::time_point last_frame_time_;
  Clock::time_point player_controls_last_hover_;
  std::unique_ptr<ui::ClayRenderer> clay_renderer_;
  std::unique_ptr<MpvPlayer> player_;
  TorrentService torrent_service_;
  ui::TitlePage title_page_;
  ui::FileBrowserPage file_browser_page_;
  ui::PlayerOverlay player_overlay_;
  SdlEventManager event_manager_;
  std::vector<char> clay_memory_;
  std::optional<ParsedInput> last_input_;
  std::optional<int> selected_torrent_file_index_;
};

} // namespace torrview
