#pragma once

#include "input_source.hpp"
#include "settings.hpp"
#include "ui/page.hpp"
#include "ui/widgets.hpp"
#include "window_state.hpp"

#include "clay.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace torrview {
class Application;
struct LibraryEntry;
} // namespace torrview

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

  void begin_frame();
  void build(const WindowMetrics& metrics, bool drag_active);

private:
  struct Row {
    std::string key;
    Clay_ElementId name, play, menu;
  };

  void library_row(const LibraryEntry& entry, uint32_t index, float column_width);
  void row_menu(const Row& row);
  void modal(float column_width);
  void input_field(Clay_ElementId id, const std::string& value, std::string_view placeholder,
                   bool focused);
  void focus_input(Clay_ElementId id, const std::string& value, float pointer_x);
  void replace_selection(std::string& value, std::string_view inserted);
  bool edit_input(std::string& value, const SDL_Event& event);
  void submit_magnet();
  void confirm_modal();

  std::string magnet_;
  bool input_focused_ = false;
  std::size_t cursor_ = 0;
  std::size_t anchor_ = 0;
  float input_scroll_ = 0;
  uint64_t caret_epoch_ = 0;
  std::string input_error_;
  std::string menu_key_;
  std::string modal_key_;
  std::string rename_;
  bool deleting_ = false;
  std::vector<Row> rows_;
  Application& app_;
  Settings& settings_;
  Widgets ui_;
  const bool& drag_active_;
  const std::optional<ParsedInput>& last_input_;
  Clay_ElementId source_panel_id_ = {};
  Clay_ElementId settings_id_ = {};
};

} // namespace torrview::ui
