#include "ui/clay_renderer.hpp"

#include "logger.hpp"
#include "resources.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace torrview::ui {
namespace {

uint8_t clay_channel(Clay_Color color, float Clay_Color::* member) {
  return static_cast<uint8_t>(std::lround(std::clamp(color.*member, 0.0F, 255.0F)));
}

uint8_t clay_red(Clay_Color color) { return clay_channel(color, &Clay_Color::r); }
uint8_t clay_green(Clay_Color color) { return clay_channel(color, &Clay_Color::g); }
uint8_t clay_blue(Clay_Color color) { return clay_channel(color, &Clay_Color::b); }
uint8_t clay_alpha(Clay_Color color) { return clay_channel(color, &Clay_Color::a); }

bool ok(tvg::Result result) { return result == tvg::Result::Success; }

std::size_t utf8_codepoint_length(unsigned char leading) {
  if ((leading & 0x80U) == 0U) {
    return 1;
  }
  if ((leading & 0xE0U) == 0xC0U) {
    return 2;
  }
  if ((leading & 0xF0U) == 0xE0U) {
    return 3;
  }
  if ((leading & 0xF8U) == 0xF0U) {
    return 4;
  }
  return 1;
}

} // namespace

Clay_Dimensions measure_text(Clay_StringSlice text, Clay_TextElementConfig* config,
                             void* user_data) {
  const auto* state = static_cast<TextMeasureState*>(user_data);
  if (state == nullptr || state->font_name == nullptr || config == nullptr || text.length <= 0 ||
      text.chars == nullptr) {
    return {0.0F, 0.0F};
  }

  std::unique_ptr<tvg::Text, decltype(&tvg::Paint::rel)> measure(tvg::Text::gen(),
                                                                 &tvg::Paint::rel);
  if (!measure || !ok(measure->font(state->font_name)) ||
      !ok(measure->size(static_cast<float>(std::max<uint16_t>(config->fontSize, 1))))) {
    const float fallback_width =
        static_cast<float>(text.length) * static_cast<float>(config->fontSize) * 0.56F;
    return {fallback_width, static_cast<float>(config->fontSize) * 1.25F};
  }

  tvg::TextMetrics line_metrics = {};
  float line_height = static_cast<float>(config->lineHeight);
  if (line_height <= 0.0F && ok(measure->metrics(line_metrics)) && line_metrics.advance > 0.0F) {
    line_height = line_metrics.advance;
  }
  if (line_height <= 0.0F) {
    line_height = static_cast<float>(config->fontSize) * 1.25F;
  }

  const char* cursor = text.chars;
  const char* end = text.chars + text.length;
  float width = 0.0F;
  int32_t glyph_count = 0;

  while (cursor < end) {
    const auto remaining = static_cast<std::size_t>(end - cursor);
    const std::size_t cp_length =
        std::min(utf8_codepoint_length(static_cast<unsigned char>(*cursor)), remaining);
    char glyph_text[5] = {};
    std::copy_n(cursor, cp_length, glyph_text);

    tvg::GlyphMetrics glyph_metrics = {};
    if (ok(measure->metrics(glyph_text, glyph_metrics)) && glyph_metrics.advance > 0.0F) {
      width += glyph_metrics.advance;
    } else {
      width += static_cast<float>(config->fontSize) * 0.56F;
    }

    cursor += cp_length;
    ++glyph_count;
  }

  if (glyph_count > 1 && config->letterSpacing > 0) {
    width += static_cast<float>(glyph_count - 1) * static_cast<float>(config->letterSpacing);
  }

  return {std::ceil(width), std::ceil(line_height)};
}

void handle_clay_error(Clay_ErrorData error) {
  if (error.errorText.chars != nullptr && error.errorText.length > 0) {
    TORRVIEW_LOG_ERROR("Clay error: " << std::string_view(error.errorText.chars,
                                                          static_cast<std::size_t>(
                                                              error.errorText.length)));
  } else {
    TORRVIEW_LOG_ERROR("Clay error: unknown error");
  }
}

ClayRenderer::ClayRenderer(SDL_GLContext context, const BasicGlApi& gl_api,
                           const WindowMetrics& metrics, const char* font_name)
    : context_(context), gl_api_(gl_api), metrics_(metrics), font_name_(font_name),
      canvas_(tvg::GlCanvas::gen()), svg_cache_() {
  if (!canvas_) {
    throw std::runtime_error("ThorVG GL canvas creation failed");
  }
}

float ClayRenderer::x_scale() const {
  return metrics_.logical_width > 0
             ? static_cast<float>(metrics_.pixel_width) / static_cast<float>(metrics_.logical_width)
             : metrics_.display_scale;
}

float ClayRenderer::y_scale() const {
  return metrics_.logical_height > 0 ? static_cast<float>(metrics_.pixel_height) /
                                           static_cast<float>(metrics_.logical_height)
                                     : metrics_.display_scale;
}

bool ClayRenderer::has_area(Rect rect) { return rect.width > 0.0F && rect.height > 0.0F; }

Rect ClayRenderer::intersect_rect(Rect left, Rect right) {
  const float x1 = std::max(left.x, right.x);
  const float y1 = std::max(left.y, right.y);
  const float x2 = std::min(left.x + left.width, right.x + right.width);
  const float y2 = std::min(left.y + left.height, right.y + right.height);
  return {
      .x = x1,
      .y = y1,
      .width = std::max(0.0F, x2 - x1),
      .height = std::max(0.0F, y2 - y1),
  };
}

Rect ClayRenderer::to_pixels(Rect logical_rect) const {
  return {
      .x = logical_rect.x * x_scale(),
      .y = logical_rect.y * y_scale(),
      .width = logical_rect.width * x_scale(),
      .height = logical_rect.height * y_scale(),
  };
}

Rect ClayRenderer::to_pixels(Clay_BoundingBox box) const {
  return to_pixels(Rect{.x = box.x, .y = box.y, .width = box.width, .height = box.height});
}

void ClayRenderer::push_clip(Rect rect) {
  if (!clip_stack_.empty()) {
    rect = intersect_rect(clip_stack_.back(), rect);
  }
  clip_stack_.push_back(rect);
  if (has_area(rect)) {
    set_scissor(rect);
  } else {
    gl_api_.disable(GL_SCISSOR_TEST);
  }
}

void ClayRenderer::pop_clip() {
  if (!clip_stack_.empty()) {
    clip_stack_.pop_back();
  }

  if (clip_stack_.empty()) {
    gl_api_.disable(GL_SCISSOR_TEST);
    return;
  }

  if (has_area(clip_stack_.back())) {
    set_scissor(clip_stack_.back());
  } else {
    gl_api_.disable(GL_SCISSOR_TEST);
  }
}

void ClayRenderer::set_scissor(Rect rect) const {
  const int left = std::max(0, static_cast<int>(std::floor(rect.x)));
  const int top = std::max(0, static_cast<int>(std::floor(rect.y)));
  const int width = std::max(0, static_cast<int>(std::ceil(rect.width)));
  const int height = std::max(0, static_cast<int>(std::ceil(rect.height)));
  const int clamped_width = std::max(0, std::min(width, metrics_.pixel_width - left));
  const int clamped_height = std::max(0, std::min(height, metrics_.pixel_height - top));

  gl_api_.enable(GL_SCISSOR_TEST);
  gl_api_.scissor(left, metrics_.pixel_height - top - clamped_height, clamped_width,
                  clamped_height);
}

void ClayRenderer::draw_paint(tvg::Paint* paint) {
  if (paint == nullptr) {
    throw std::runtime_error("ThorVG failed to create paint");
  }

  if (!clip_stack_.empty()) {
    const Rect clip = clip_stack_.back();
    if (!has_area(clip)) {
      tvg::Paint::rel(paint);
      return;
    }

    auto* clipper = tvg::Shape::gen();
    if (clipper == nullptr) {
      tvg::Paint::rel(paint);
      throw std::runtime_error("ThorVG failed to create clip shape");
    }

    if (!ok(clipper->appendRect(clip.x, clip.y, clip.width, clip.height)) ||
        !ok(paint->clip(clipper))) {
      tvg::Paint::rel(clipper);
      tvg::Paint::rel(paint);
      throw std::runtime_error("ThorVG failed to configure clip shape");
    }
  }

  if (!ok(canvas_->add(paint)) || !ok(canvas_->update()) || !ok(canvas_->draw(false)) ||
      !ok(canvas_->sync())) {
    canvas_->remove();
    throw std::runtime_error("ThorVG GL draw failed");
  }

  canvas_->remove();
}

void ClayRenderer::draw_shape_rect(Rect rect, Clay_Color color, Clay_CornerRadius radius) {
  auto* shape = tvg::Shape::gen();
  if (shape == nullptr) {
    throw std::runtime_error("ThorVG failed to create shape");
  }

  const float corner_radius = std::clamp(
      std::max({radius.topLeft, radius.topRight, radius.bottomLeft, radius.bottomRight}) *
          std::min(x_scale(), y_scale()),
      0.0F, std::min(rect.width, rect.height) * 0.5F);
  if (!ok(shape->appendRect(rect.x, rect.y, rect.width, rect.height, corner_radius,
                            corner_radius)) ||
      !ok(shape->fill(clay_red(color), clay_green(color), clay_blue(color), clay_alpha(color)))) {
    tvg::Paint::rel(shape);
    throw std::runtime_error("ThorVG failed to configure rectangle");
  }

  draw_paint(shape);
}

void ClayRenderer::draw_border(Rect rect, Clay_BorderRenderData border) {
  const float width = static_cast<float>(
      std::max({border.width.left, border.width.right, border.width.top, border.width.bottom}));
  if (width <= 0.0F) {
    return;
  }

  auto* shape = tvg::Shape::gen();
  if (shape == nullptr) {
    throw std::runtime_error("ThorVG failed to create border shape");
  }

  const float scaled_width = width * std::min(x_scale(), y_scale());
  const float inset = scaled_width * 0.5F;
  const float corner_radius =
      std::clamp(std::max({border.cornerRadius.topLeft, border.cornerRadius.topRight,
                           border.cornerRadius.bottomLeft, border.cornerRadius.bottomRight}) *
                     std::min(x_scale(), y_scale()),
                 0.0F, std::min(rect.width, rect.height) * 0.5F);

  if (!ok(shape->appendRect(
          rect.x + inset, rect.y + inset, std::max(0.0F, rect.width - scaled_width),
          std::max(0.0F, rect.height - scaled_width), corner_radius, corner_radius)) ||
      !ok(shape->strokeWidth(scaled_width)) ||
      !ok(shape->strokeFill(clay_red(border.color), clay_green(border.color),
                            clay_blue(border.color), clay_alpha(border.color))) ||
      !ok(shape->strokeJoin(tvg::StrokeJoin::Round))) {
    tvg::Paint::rel(shape);
    throw std::runtime_error("ThorVG failed to configure border");
  }

  draw_paint(shape);
}

void ClayRenderer::draw_text_command(Rect rect, Clay_TextRenderData text_data) {
  const std::string text =
      std::string(text_data.stringContents.chars,
                  static_cast<std::size_t>(std::max(0, text_data.stringContents.length)));
  auto* text_paint = tvg::Text::gen();
  if (text_paint == nullptr) {
    throw std::runtime_error("ThorVG failed to create text paint");
  }

  if (!ok(text_paint->font(font_name_)) ||
      !ok(text_paint->size(static_cast<float>(text_data.fontSize) * y_scale())) ||
      !ok(text_paint->text(text.c_str())) ||
      !ok(text_paint->fill(clay_red(text_data.textColor), clay_green(text_data.textColor),
                           clay_blue(text_data.textColor))) ||
      !ok(text_paint->opacity(clay_alpha(text_data.textColor))) ||
      !ok(text_paint->layout(rect.width, rect.height)) || !ok(text_paint->align(0.0F, 0.0F)) ||
      !ok(text_paint->translate(rect.x, rect.y))) {
    tvg::Paint::rel(text_paint);
    throw std::runtime_error("ThorVG failed to configure text");
  }

  draw_paint(text_paint);
}

void ClayRenderer::draw_image_command(Rect rect, Clay_ImageRenderData image_data) {
  auto* resource_name = static_cast<const char*>(image_data.imageData);
  if (resource_name == nullptr || rect.width <= 0.0F || rect.height <= 0.0F) {
    return;
  }

  auto cache_item = svg_cache_.find(resource_name);
  if (cache_item == svg_cache_.end()) {
    const auto* resource = resources::find(resource_name);
    if (resource == nullptr ||
        resource->size > static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())) {
      throw std::runtime_error(std::string("Missing bundled SVG icon resource: ") + resource_name);
    }

    std::unique_ptr<tvg::Picture, decltype(&tvg::Paint::rel)> picture(tvg::Picture::gen(),
                                                                      &tvg::Paint::rel);
    if (!picture || !ok(picture->load(reinterpret_cast<const char*>(resource->data),
                                      static_cast<uint32_t>(resource->size),
                                      resource->mime_type.data(), nullptr, true))) {
      throw std::runtime_error(std::string("ThorVG failed to load SVG icon resource: ") +
                               resource_name);
    }

    cache_item = svg_cache_.emplace(resource_name, std::move(picture)).first;
  }

  auto* picture = static_cast<tvg::Picture*>(cache_item->second->duplicate());
  if (picture == nullptr) {
    throw std::runtime_error("ThorVG failed to duplicate SVG icon");
  }

  if (!ok(picture->size(rect.width, rect.height)) || !ok(picture->translate(rect.x, rect.y))) {
    tvg::Paint::rel(picture);
    throw std::runtime_error("ThorVG failed to configure SVG icon");
  }

  draw_paint(picture);
}

void ClayRenderer::render(Clay_RenderCommandArray commands) {
  if (!ok(canvas_->target(
          nullptr, nullptr, context_, 0, static_cast<uint32_t>(metrics_.pixel_width),
          static_cast<uint32_t>(metrics_.pixel_height), tvg::ColorSpace::ABGR8888S))) {
    throw std::runtime_error("ThorVG GL canvas target setup failed");
  }

  gl_api_.disable(GL_SCISSOR_TEST);
  clip_stack_.clear();
  for (int32_t index = 0; index < commands.length; ++index) {
    Clay_RenderCommand* command = Clay_RenderCommandArray_Get(&commands, index);
    if (command == nullptr) {
      continue;
    }

    const Rect rect = to_pixels(command->boundingBox);
    switch (command->commandType) {
    case CLAY_RENDER_COMMAND_TYPE_RECTANGLE:
      draw_shape_rect(rect, command->renderData.rectangle.backgroundColor,
                      command->renderData.rectangle.cornerRadius);
      break;
    case CLAY_RENDER_COMMAND_TYPE_BORDER:
      draw_border(rect, command->renderData.border);
      break;
    case CLAY_RENDER_COMMAND_TYPE_TEXT:
      draw_text_command(rect, command->renderData.text);
      break;
    case CLAY_RENDER_COMMAND_TYPE_IMAGE:
      draw_image_command(rect, command->renderData.image);
      break;
    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
      push_clip(rect);
      break;
    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
      pop_clip();
      break;
    case CLAY_RENDER_COMMAND_TYPE_NONE:
    case CLAY_RENDER_COMMAND_TYPE_OVERLAY_COLOR_START:
    case CLAY_RENDER_COMMAND_TYPE_OVERLAY_COLOR_END:
    case CLAY_RENDER_COMMAND_TYPE_CUSTOM:
      break;
    }
  }
  clip_stack_.clear();
  gl_api_.disable(GL_SCISSOR_TEST);
}

} // namespace torrview::ui
