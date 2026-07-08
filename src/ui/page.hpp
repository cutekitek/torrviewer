#pragma once

#include <SDL3/SDL.h>

namespace torrview::ui {

struct RenderContext;

class Page {
public:
  virtual ~Page() = default;

  virtual void draw(RenderContext& render_context) = 0;
  virtual bool handle_event(const SDL_Event& event) = 0;
  virtual void close() {}
};

} // namespace torrview::ui
