#pragma once

#include "gl_api.hpp"
#include "window_state.hpp"

#include "clay.h"

#include <SDL3/SDL.h>

namespace torrview::ui {

struct RenderContext {
  RenderContext(Clay_Context* clay_context_value, const WindowMetrics& metrics_value,
                SDL_GLContext opengl_context_value, const BasicGlApi& gl_api_value)
      : clay_context(clay_context_value), metrics(metrics_value),
        opengl_context(opengl_context_value), gl_api(gl_api_value) {}

  Clay_Context* clay_context = nullptr;
  const WindowMetrics& metrics;
  SDL_GLContext opengl_context = nullptr;
  const BasicGlApi& gl_api;
};

} // namespace torrview::ui
