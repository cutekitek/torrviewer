#pragma once

#include <SDL3/SDL.h>

namespace torrview {

class SdlEventHandler {
public:
  virtual ~SdlEventHandler() = default;

  virtual void request_quit() = 0;
  virtual void window_metrics_changed() = 0;
  virtual void key_down(const SDL_KeyboardEvent& key) = 0;
  virtual void text_input(const SDL_TextInputEvent& text) = 0;
  virtual void mouse_motion(const SDL_MouseMotionEvent& motion) = 0;
  virtual void mouse_button(const SDL_MouseButtonEvent& button) = 0;
  virtual void mouse_wheel(const SDL_MouseWheelEvent& wheel) = 0;
  virtual void drop_file(const char* path) = 0;
  virtual void drop_text(const char* text) = 0;
  virtual void drop_begin() = 0;
  virtual void drop_complete() = 0;
  virtual void drop_position(float x, float y) = 0;
};

class SdlEventManager {
public:
  void pump(SdlEventHandler& handler);
};

} // namespace torrview
