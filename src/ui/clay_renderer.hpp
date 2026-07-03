#pragma once

#include "gl_api.hpp"
#include "window_state.hpp"

#include "clay.h"

#include <SDL3/SDL.h>
#include <thorvg.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace torrview::ui {

struct TextMeasureState {
  const char* font_name = nullptr;
};

Clay_Dimensions measure_text(Clay_StringSlice text, Clay_TextElementConfig* config,
                             void* user_data);
void handle_clay_error(Clay_ErrorData error);

class ClayRenderer {
public:
  ClayRenderer(SDL_GLContext context, const BasicGlApi& gl_api, const WindowMetrics& metrics,
               const char* font_name);

  void render(Clay_RenderCommandArray commands);

private:
  float x_scale() const;
  float y_scale() const;
  Rect to_pixels(Rect logical_rect) const;
  Rect to_pixels(Clay_BoundingBox box) const;
  void set_scissor(Rect rect) const;
  void draw_paint(tvg::Paint* paint);
  void draw_shape_rect(Rect rect, Clay_Color color, Clay_CornerRadius radius);
  void draw_border(Rect rect, Clay_BorderRenderData border);
  void draw_text_command(Rect rect, Clay_TextRenderData text_data);
  void draw_image_command(Rect rect, Clay_ImageRenderData image_data);

  SDL_GLContext context_ = nullptr;
  const BasicGlApi& gl_api_;
  const WindowMetrics& metrics_;
  const char* font_name_ = nullptr;
  std::unique_ptr<tvg::GlCanvas> canvas_;
  std::unordered_map<std::string, std::unique_ptr<tvg::Picture, decltype(&tvg::Paint::rel)>>
      svg_cache_;
};

} // namespace torrview::ui
