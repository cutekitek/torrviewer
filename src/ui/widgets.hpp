#pragma once

#include "settings.hpp"
#include "ui/theme.hpp"

#include "clay.h"

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

namespace torrview::ui {

enum class ButtonKind {
  primary,   // lamp fill: the one action a screen is for
  secondary, // raised fill
  quiet,     // no fill until hovered
  danger,    // destructive confirmation
};

[[nodiscard]] Clay_String clay_string(std::string_view value);

// Shared building blocks for Clay pages. Each page owns one instance so the strings it retains
// for the current frame live until Clay renders them.
//
// Scroll containers: Clay sends wheel input to the most recently registered clip element under
// the pointer, so nothing inside a scrolling list may enable `.clip`. Truncate text instead.
class Widgets {
public:
  explicit Widgets(const Settings& settings) : settings_(settings) {}

  void begin_frame() { strings_.clear(); }
  std::string_view keep(std::string value);

  [[nodiscard]] uint16_t size(uint16_t base) const;
  [[nodiscard]] float line(uint16_t base) const;
  [[nodiscard]] std::size_t chars_for_width(float width, uint16_t base) const;

  void text(std::string_view value, uint16_t base_size, Clay_Color color,
            uint16_t font = theme::font_regular,
            Clay_TextElementConfigWrapMode wrap = CLAY_TEXT_WRAP_NONE);
  void icon(const char* path, float size);
  void spacer();
  void gap(float width, float height);
  void rule(Clay_Color color = theme::rule);
  void button(Clay_ElementId id, std::string_view label, ButtonKind kind);
  void icon_button(Clay_ElementId id, const char* icon_path, bool active = false, float box = 32.0F,
                   float glyph = 18.0F);
  void field(Clay_ElementId id, std::string_view value, std::string_view placeholder,
             std::string_view suffix, bool focused, float width);
  void toggle(Clay_ElementId id, bool on, bool enabled = true);
  // Declare outside the scroll container; the thumb floats over its right edge.
  void scrollbar(Clay_ElementId container_id);

private:
  const Settings& settings_;
  std::deque<std::string> strings_;
};

[[nodiscard]] std::string truncate_middle(std::string_view value, std::size_t max_chars);
[[nodiscard]] std::string truncate_left(std::string_view value, std::size_t max_chars);
[[nodiscard]] std::string format_size(std::int64_t bytes);
[[nodiscard]] std::string format_clock(double seconds);

} // namespace torrview::ui
