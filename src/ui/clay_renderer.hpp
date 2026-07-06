#pragma once

#include "gl_api.hpp"
#include "window_state.hpp"

#include "clay.h"

#include <SDL3/SDL.h>
#include <thorvg.h>

#include <memory>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace torrview::ui {

struct TextMeasureState {
  const char* font_name = nullptr;
  std::unique_ptr<tvg::Text, decltype(&tvg::Paint::rel)> text = {nullptr, &tvg::Paint::rel};
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
  static bool has_area(Rect rect);
  static Rect intersect_rect(Rect left, Rect right);
  float x_scale() const;
  float y_scale() const;
  Rect to_pixels(Rect logical_rect) const;
  Rect to_pixels(Clay_BoundingBox box) const;
  void push_clip(Rect rect);
  void pop_clip();
  void set_scissor(Rect rect) const;
  void queue_paint(tvg::Paint* paint);
  void flush_paints();
  void draw_shape_rect(Rect rect, Clay_Color color, Clay_CornerRadius radius);
  void draw_border(Rect rect, Clay_BorderRenderData border);
  void draw_text_command(Rect rect, Clay_TextRenderData text_data);
  void draw_image_command(Rect rect, Clay_ImageRenderData image_data);

  SDL_GLContext context_ = nullptr;
  const BasicGlApi& gl_api_;
  const WindowMetrics& metrics_;
  const char* font_name_ = nullptr;
  std::unique_ptr<tvg::GlCanvas> canvas_;
  std::vector<Rect> clip_stack_;
  std::size_t queued_paint_count_ = 0;
  std::unordered_map<std::string, std::unique_ptr<tvg::Picture, decltype(&tvg::Paint::rel)>>
      svg_cache_;
};

} // namespace torrview::ui
