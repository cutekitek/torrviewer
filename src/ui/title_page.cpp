#include "ui/title_page.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace torrview::ui {
namespace {

constexpr Clay_Color color_background = {12.0F, 13.0F, 15.0F, 255.0F};
constexpr Clay_Color color_text = {233.0F, 236.0F, 241.0F, 255.0F};
constexpr Clay_Color color_accent = {74.0F, 181.0F, 162.0F, 255.0F};
constexpr Clay_Color color_error = {214.0F, 94.0F, 82.0F, 255.0F};

Clay_String clay_string(std::string_view value) {
  const std::size_t max_length = static_cast<std::size_t>(std::numeric_limits<int32_t>::max());
  const std::size_t length = std::min(value.size(), max_length);
  return {.isStaticallyAllocated = false,
          .length = static_cast<int32_t>(length),
          .chars = value.data()};
}

} // namespace

void TitlePage::initialize_ids() {
  source_panel_id_ = Clay_GetElementId(CLAY_STRING("SourcePanel"));
}

Clay_ElementId TitlePage::source_panel_id() const { return source_panel_id_; }

void TitlePage::begin_frame() { frame_strings_.clear(); }

std::string TitlePage::status_text(const std::optional<ParsedInput>& last_input) const {
  if (!last_input.has_value()) {
    return "Paste magnet, drop file or click to open file dialog";
  }

  std::string label = input_kind_label(last_input->kind);
  label += ": ";
  label += compact_value(last_input->value, 44);
  return label;
}

std::string_view TitlePage::retain_frame_text(std::string value) {
  frame_strings_.push_back(std::move(value));
  return frame_strings_.back();
}

void TitlePage::text(std::string_view value, uint16_t font_size, Clay_Color color,
                     Clay_TextElementConfigWrapMode wrap) {
  CLAY_TEXT(clay_string(value), CLAY_TEXT_CONFIG({.textColor = color,
                                                  .fontSize = font_size,
                                                  .lineHeight = static_cast<uint16_t>(std::lround(
                                                      static_cast<float>(font_size) * 1.25F)),
                                                  .wrapMode = wrap}));
}

void TitlePage::build(const WindowMetrics& metrics, bool drag_active,
                      const std::optional<ParsedInput>& last_input) {
  const float window_width = static_cast<float>(metrics.logical_width);
  const float content_width = std::clamp(window_width * 0.62F, 420.0F, 860.0F);
  const bool has_problem =
      last_input.has_value() && (last_input->kind == InputKind::unsupported_file ||
                                 last_input->kind == InputKind::dropped_text);
  const Clay_Color panel_border =
      drag_active ? color_accent : (has_problem ? color_error : color_text);
  const Clay_Color label_color =
      drag_active ? color_accent : (has_problem ? color_error : color_text);
  const std::string_view label = retain_frame_text(
      drag_active ? std::string("Drop to open torrent source") : status_text(last_input));

  CLAY(CLAY_ID("TitleRoot"),
       {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F), .height = CLAY_SIZING_GROW(0.0F)},
                   .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
        .backgroundColor = color_background}) {
    CLAY(source_panel_id_,
         {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(content_width),
                                .height = CLAY_SIZING_FIXED(132.0F)},
                     .padding = {24, 24, 0, 0},
                     .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = color_background,
          .cornerRadius = CLAY_CORNER_RADIUS(10.0F),
          .border = {.color = panel_border,
                     .width = {.left = 1, .right = 1, .top = 1, .bottom = 1}}}) {
      text(label, 15, label_color, CLAY_TEXT_WRAP_NONE);
    }
  }
}

} // namespace torrview::ui
