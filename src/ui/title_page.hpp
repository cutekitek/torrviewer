#pragma once

#include "input_source.hpp"
#include "window_state.hpp"

#include "clay.h"

#include <deque>
#include <optional>
#include <string>
#include <string_view>

namespace torrview::ui {

class TitlePage {
public:
  void initialize_ids();
  Clay_ElementId source_panel_id() const;

  void begin_frame();
  void build(const WindowMetrics& metrics, bool drag_active,
             const std::optional<ParsedInput>& last_input);

private:
  std::string status_text(const std::optional<ParsedInput>& last_input) const;
  std::string_view retain_frame_text(std::string value);
  void text(std::string_view value, uint16_t font_size, Clay_Color color,
            Clay_TextElementConfigWrapMode wrap = CLAY_TEXT_WRAP_NONE);

  Clay_ElementId source_panel_id_ = {};
  std::deque<std::string> frame_strings_;
};

} // namespace torrview::ui
