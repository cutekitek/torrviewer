#pragma once

#include "gl_api.hpp"
#include "app_settings.hpp"
#include "input_source.hpp"
#include "mpv_player.hpp"
#include "sdl_event_manager.hpp"
#include "settings.hpp"
#include "torrent_service.hpp"
#include "ui/app.hpp"
#include "ui/clay_renderer.hpp"
#include "ui/file_browser_page.hpp"
#include "ui/player_overlay.hpp"
#include "ui/settings_page.hpp"
#include "ui/title_page.hpp"
#include "window_state.hpp"

#include "clay.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
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

  template <typename PageClass, typename... Args> PageClass& navigate(Args&&... args) {
    return ui_app_.open<PageClass>(std::forward<Args>(args)...);
  }

  void show_source_dialog();
  void apply_settings();
  void update_window_title();
  void request_quit();
  void toggle_fullscreen();
  bool is_fullscreen() const;

private:
  using Clock = std::chrono::steady_clock;

  void window_metrics_changed() override;
  void key_down(const SDL_KeyboardEvent& key) override;
  void text_input(const SDL_TextInputEvent& text) override;
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
  void register_pages();
  TorrentRuntimeSettings runtime_settings_from_app_settings() const;
  void accept_input(ParsedInput input);
  void paste_clipboard();
  void consume_dialog_results();
  void update_window_metrics();
  void draw();
  void log_startup() const;

  bool running_ = false;
  bool sdl_initialized_ = false;
  bool drag_active_ = false;
  bool thorvg_initialized_ = false;
  float current_delta_time_ = 0.0F;
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
  std::unique_ptr<ui::ClayRenderer> clay_renderer_;
  std::unique_ptr<MpvPlayer> player_;
  TorrentService torrent_service_;
  ui::App ui_app_;
  SdlEventManager event_manager_;
  std::vector<char> clay_memory_;
  std::optional<ParsedInput> last_input_;
};

} // namespace torrview
