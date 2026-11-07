#include "ui/widgets.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>

namespace torrview::ui {
namespace {

bool is_continuation(char value) { return (static_cast<unsigned char>(value) & 0xC0U) == 0x80U; }

// Byte offsets of each code point start, plus the end offset.
std::vector<std::size_t> code_point_offsets(std::string_view value) {
  std::vector<std::size_t> offsets;
  offsets.reserve(value.size() + 1);
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (!is_continuation(value[index])) {
      offsets.push_back(index);
    }
  }
  offsets.push_back(value.size());
  return offsets;
}

Clay_Color with_alpha(Clay_Color color, float alpha) {
  color.a = alpha;
  return color;
}

} // namespace

Clay_String clay_string(std::string_view value) {
  const std::size_t max_length = static_cast<std::size_t>(std::numeric_limits<int32_t>::max());
  const std::size_t length = std::min(value.size(), max_length);
  return {.isStaticallyAllocated = false,
          .length = static_cast<int32_t>(length),
          .chars = value.data()};
}

std::string truncate_middle(std::string_view value, std::size_t max_chars) {
  const std::vector<std::size_t> offsets = code_point_offsets(value);
  const std::size_t count = offsets.size() - 1;
  if (count <= max_chars || max_chars < 5) {
    return std::string(value);
  }
  const std::size_t keep = max_chars - 1;
  const std::size_t head = (keep + 1) / 2;
  const std::size_t tail = keep - head;
  std::string result(value.substr(0, offsets[head]));
  result += "\xE2\x80\xA6";
  result += value.substr(offsets[count - tail]);
  return result;
}

std::string truncate_left(std::string_view value, std::size_t max_chars) {
  const std::vector<std::size_t> offsets = code_point_offsets(value);
  const std::size_t count = offsets.size() - 1;
  if (count <= max_chars || max_chars < 2) {
    return std::string(value);
  }
  std::string result("\xE2\x80\xA6");
  result += value.substr(offsets[count - (max_chars - 1)]);
  return result;
}

std::string format_size(std::int64_t bytes) {
  constexpr double kib = 1024.0;
  constexpr double mib = kib * 1024.0;
  constexpr double gib = mib * 1024.0;
  const double value = static_cast<double>(std::max<std::int64_t>(0, bytes));
  std::ostringstream out;
  if (value >= gib) {
    out << std::fixed << std::setprecision(value >= 100.0 * gib ? 0 : 1) << value / gib << " GB";
  } else if (value >= mib) {
    out << std::fixed << std::setprecision(0) << value / mib << " MB";
  } else if (value >= kib) {
    out << std::fixed << std::setprecision(0) << value / kib << " KB";
  } else {
    out << static_cast<std::int64_t>(value) << " B";
  }
  return out.str();
}

std::string format_clock(double seconds) {
  if (seconds < 0.0 || !std::isfinite(seconds)) {
    seconds = 0.0;
  }
  const auto total = static_cast<long long>(std::llround(seconds));
  const long long hours = total / 3600;
  const long long minutes = (total / 60) % 60;
  const long long secs = total % 60;
  std::ostringstream out;
  if (hours > 0) {
    out << hours << ':' << std::setw(2) << std::setfill('0') << minutes << ':';
  } else {
    out << minutes << ':';
  }
  out << std::setw(2) << std::setfill('0') << secs;
  return out.str();
}

std::string_view Widgets::keep(std::string value) {
  strings_.push_back(std::move(value));
  return strings_.back();
}

uint16_t Widgets::size(uint16_t base) const {
  const float scale = static_cast<float>(std::clamp(settings_.values().font_size, 9, 24)) / 13.0F;
  return static_cast<uint16_t>(
      std::clamp<int>(static_cast<int>(std::lround(static_cast<float>(base) * scale)), 8, 56));
}

float Widgets::line(uint16_t base) const {
  return std::round(static_cast<float>(size(base)) * 1.3F);
}

std::size_t Widgets::chars_for_width(float width, uint16_t base) const {
  const float advance = std::max(4.0F, static_cast<float>(size(base)) * 0.56F);
  return static_cast<std::size_t>(std::max(6.0F, width / advance));
}

void Widgets::text(std::string_view value, uint16_t base_size, Clay_Color color, uint16_t font,
                   Clay_TextElementConfigWrapMode wrap) {
  const uint16_t scaled = size(base_size);
  CLAY_TEXT(clay_string(value),
            CLAY_TEXT_CONFIG({.textColor = color,
                              .fontId = font,
                              .fontSize = scaled,
                              .lineHeight = static_cast<uint16_t>(line(base_size)),
                              .wrapMode = wrap}));
}

void Widgets::icon(const char* path, float size_value) {
  CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(size_value),
                                      .height = CLAY_SIZING_FIXED(size_value)}},
                .image = {.imageData = const_cast<char*>(path)}}) {}
}

void Widgets::spacer() {
  CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                      .height = CLAY_SIZING_FIXED(1.0F)}}}) {}
}

void Widgets::gap(float width, float height) {
  CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(width),
                                      .height = CLAY_SIZING_FIXED(height)}}}) {}
}

void Widgets::rule(Clay_Color color) {
  CLAY_AUTO_ID(
      {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F), .height = CLAY_SIZING_FIXED(1.0F)}},
       .backgroundColor = color}) {}
}

void Widgets::button(Clay_ElementId id, std::string_view label, ButtonKind kind) {
  const bool hovered = Clay_PointerOver(id);
  Clay_Color fill = theme::clear;
  Clay_Color ink = theme::lamp;
  switch (kind) {
  case ButtonKind::primary:
    fill = hovered ? Clay_Color{255.0F, 251.0F, 243.0F, 255.0F} : theme::lamp;
    ink = theme::graphite;
    break;
  case ButtonKind::secondary:
    fill = hovered ? theme::rule : theme::lifted;
    break;
  case ButtonKind::quiet:
    fill = hovered ? theme::raised : theme::clear;
    break;
  case ButtonKind::danger:
    fill = hovered ? Clay_Color{232.0F, 104.0F, 82.0F, 255.0F} : theme::fault;
    ink = theme::graphite;
    break;
  }
  CLAY(id, {.layout = {.sizing = {.width = CLAY_SIZING_FIT(0.0F),
                                  .height = CLAY_SIZING_FIXED(line(theme::size_body) + 14.0F)},
                       .padding = {14, 14, 0, 0},
                       .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
            .backgroundColor = fill,
            .cornerRadius = CLAY_CORNER_RADIUS(theme::radius_control)}) {
    text(label, theme::size_body, ink,
         kind == ButtonKind::quiet ? theme::font_regular : theme::font_semibold);
  }
}

void Widgets::icon_button(Clay_ElementId id, const char* icon_path, bool active, float box,
                          float glyph) {
  const bool hovered = Clay_PointerOver(id);
  CLAY(id,
       {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(box), .height = CLAY_SIZING_FIXED(box)},
                   .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
        .backgroundColor = active ? theme::lifted : (hovered ? theme::raised : theme::clear),
        .cornerRadius = CLAY_CORNER_RADIUS(theme::radius_control)}) {
    icon(icon_path, glyph);
  }
}

void Widgets::field(Clay_ElementId id, std::string_view value, std::string_view placeholder,
                    std::string_view suffix, bool focused, float width) {
  const bool hovered = Clay_PointerOver(id);
  const float height = line(theme::size_body) + 16.0F;
  const float text_room = (width > 0.0F ? width : 640.0F) - 28.0F -
                          static_cast<float>(suffix.size()) * static_cast<float>(size(12)) * 0.6F;
  const bool caret_on = focused && SDL_GetTicks() % 1000U < 600U;
  CLAY(id, {.layout = {.sizing = {.width = width > 0.0F ? CLAY_SIZING_FIXED(width)
                                                        : CLAY_SIZING_GROW(0.0F),
                                  .height = CLAY_SIZING_FIXED(height)},
                       .padding = {12, 12, 0, 0},
                       .childGap = 1,
                       .childAlignment = {.x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER}},
            .backgroundColor = focused || hovered ? theme::lifted : theme::raised,
            .cornerRadius = CLAY_CORNER_RADIUS(theme::radius_control),
            .border = {.color = focused ? theme::lamp : theme::clear,
                       .width = {.left = 1, .right = 1, .top = 1, .bottom = 1}}}) {
    if (value.empty() && !placeholder.empty() && !focused) {
      text(placeholder, theme::size_body, theme::smoke);
    } else if (!value.empty()) {
      text(keep(truncate_left(value, chars_for_width(text_room, theme::size_body))),
           theme::size_body, theme::lamp);
    }
    if (focused) {
      CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(1.5F),
                                          .height = CLAY_SIZING_FIXED(line(theme::size_body))}},
                    .backgroundColor = caret_on ? theme::lamp : theme::clear}) {}
    }
    if (!suffix.empty()) {
      spacer();
      text(suffix, theme::size_caption, theme::ash);
    }
  }
}

void Widgets::toggle(Clay_ElementId id, bool on, bool enabled) {
  const float alpha = enabled ? 255.0F : 90.0F;
  const Clay_Color track = on ? theme::lamp : theme::rule;
  const Clay_Color knob = on ? theme::graphite : theme::ash;
  CLAY(id, {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(34.0F),
                                  .height = CLAY_SIZING_FIXED(20.0F)},
                       .padding = {3, 3, 3, 3},
                       .childAlignment = {.x = on ? CLAY_ALIGN_X_RIGHT : CLAY_ALIGN_X_LEFT,
                                          .y = CLAY_ALIGN_Y_CENTER}},
            .backgroundColor = with_alpha(track, alpha),
            .cornerRadius = CLAY_CORNER_RADIUS(10.0F)}) {
    CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(14.0F),
                                        .height = CLAY_SIZING_FIXED(14.0F)}},
                  .backgroundColor = with_alpha(knob, alpha),
                  .cornerRadius = CLAY_CORNER_RADIUS(7.0F)}) {}
  }
}

void Widgets::scrollbar(Clay_ElementId container_id) {
  const Clay_ScrollContainerData data = Clay_GetScrollContainerData(container_id);
  if (!data.found || data.scrollPosition == nullptr) {
    return;
  }
  const float view = data.scrollContainerDimensions.height;
  const float content = data.contentDimensions.height;
  if (view <= 0.0F || content <= view + 1.0F) {
    return;
  }
  const float thumb = std::max(28.0F, view * view / content);
  const float progress = std::clamp(-data.scrollPosition->y / (content - view), 0.0F, 1.0F);
  CLAY_AUTO_ID(
      {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(3.0F), .height = CLAY_SIZING_FIXED(thumb)}},
       .backgroundColor = theme::smoke,
       .cornerRadius = CLAY_CORNER_RADIUS(1.5F),
       .floating = {.offset = {-5.0F, (view - thumb) * progress},
                    .parentId = container_id.id,
                    .zIndex = 5,
                    .attachPoints = {.element = CLAY_ATTACH_POINT_RIGHT_TOP,
                                     .parent = CLAY_ATTACH_POINT_RIGHT_TOP},
                    .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
                    .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID,
                    .clipTo = CLAY_CLIP_TO_NONE}}) {}
}

} // namespace torrview::ui
