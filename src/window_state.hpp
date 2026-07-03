#pragma once

#include <SDL3/SDL.h>

namespace torrview {

struct WindowMetrics {
  int logical_width = 0;
  int logical_height = 0;
  int pixel_width = 0;
  int pixel_height = 0;
  float display_scale = 1.0F;
};

struct PointerState {
  float x = 0.0F;
  float y = 0.0F;
  float wheel_x = 0.0F;
  float wheel_y = 0.0F;
  SDL_MouseButtonFlags buttons = 0;
};

struct Rect {
  float x = 0.0F;
  float y = 0.0F;
  float width = 0.0F;
  float height = 0.0F;
};

} // namespace torrview
