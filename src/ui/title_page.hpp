#pragma once

#include "input_source.hpp"
#include "settings.hpp"
#include "ui/page.hpp"
#include "window_state.hpp"

#include "clay.h"

#include <deque>
#include <optional>
#include <string>
#include <string_view>

namespace torrview {
class Application;
}

namespace torrview::ui {

class TitlePage : public Page {
public:
  TitlePage(Application& app, Settings& settings, const bool& drag_active,
            const std::optional<ParsedInput>& last_input);
  void open();
  void draw(RenderContext& render_context) override;
  bool handle_event(const SDL_Event& event) override;
  void close() override;

  void initialize_ids();
  Clay_ElementId source_panel_id() const;
  Clay_ElementId settings_button_id() const;
  void set_font_size(int font_size);

  void begin_frame();
  void build(const WindowMetrics& metrics, bool drag_active,
             const std::optional<ParsedInput>& last_input);

private:
  std::string status_text(const std::optional<ParsedInput>& last_input) const;
  std::string_view retain_frame_text(std::string value);
  void text(std::string_view value, uint16_t font_size, Clay_Color color,
            Clay_TextElementConfigWrapMode wrap = CLAY_TEXT_WRAP_NONE);
  [[nodiscard]] uint16_t scaled_font_size(uint16_t font_size) const;
  [[nodiscard]] float line_height(uint16_t font_size) const;

  Application& app_;
  Settings& settings_;
  const bool& drag_active_;
  const std::optional<ParsedInput>& last_input_;
  Clay_ElementId source_panel_id_ = {};
  Clay_ElementId settings_id_ = {};
  int font_size_ = 13;
  std::deque<std::string> frame_strings_;
};

} // namespace torrview::ui
