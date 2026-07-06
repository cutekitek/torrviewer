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
  settings_id_ = Clay_GetElementId(CLAY_STRING("TitleSettings"));
}

Clay_ElementId TitlePage::source_panel_id() const { return source_panel_id_; }

Clay_ElementId TitlePage::settings_button_id() const { return settings_id_; }

void TitlePage::set_font_size(int font_size) { font_size_ = std::clamp(font_size, 9, 24); }

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
  const uint16_t scaled_size = scaled_font_size(font_size);
  CLAY_TEXT(clay_string(value), CLAY_TEXT_CONFIG({.textColor = color,
                                                  .fontSize = scaled_size,
                                                  .lineHeight = static_cast<uint16_t>(std::lround(
                                                      static_cast<float>(scaled_size) * 1.25F)),
                                                  .wrapMode = wrap}));
}

uint16_t TitlePage::scaled_font_size(uint16_t font_size) const {
  const float scale = static_cast<float>(std::clamp(font_size_, 9, 24)) / 13.0F;
  return static_cast<uint16_t>(std::clamp<int>(static_cast<int>(std::lround(font_size * scale)), 8,
                                               32));
}

float TitlePage::line_height(uint16_t font_size) const {
  return static_cast<float>(scaled_font_size(font_size)) * 1.25F;
}

void TitlePage::build(const WindowMetrics& metrics, bool drag_active,
                      const std::optional<ParsedInput>& last_input) {
  const float window_width = static_cast<float>(metrics.logical_width);
  const float content_width = std::clamp(window_width - 76.0F, 420.0F, 860.0F);
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
    CLAY(settings_id_,
         {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(40.0F),
                                .height = CLAY_SIZING_FIXED(40.0F)},
                     .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {24.0F, 27.0F, 31.0F, 255.0F},
          .cornerRadius = CLAY_CORNER_RADIUS(6.0F),
          .floating = {.offset = {-18.0F, 18.0F},
                       .parentId = Clay_GetElementId(CLAY_STRING("TitleRoot")).id,
                       .zIndex = 10,
                       .attachPoints = {.element = CLAY_ATTACH_POINT_RIGHT_TOP,
                                        .parent = CLAY_ATTACH_POINT_RIGHT_TOP},
                       .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE,
                       .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID,
                       .clipTo = CLAY_CLIP_TO_NONE}}) {
      CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(20.0F),
                                          .height = CLAY_SIZING_FIXED(20.0F)}},
                    .image = {.imageData = const_cast<char*>("icons/gear.svg")}}) {}
    }
    CLAY(source_panel_id_,
         {.layout = {.sizing = {.width = CLAY_SIZING_FIT(0.0F, content_width),
                                .height = CLAY_SIZING_FIT(0.0F)},
                     .padding = {24, 24, 0, 0},
                     .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = color_background,
          .cornerRadius = CLAY_CORNER_RADIUS(10.0F),
          .clip = {.horizontal = true, .vertical = true},
          .border = {.color = panel_border,
                     .width = {.left = 1, .right = 1, .top = 1, .bottom = 1}}}) {
      text(label, 15, label_color, CLAY_TEXT_WRAP_WORDS);
    }
  }
}

} // namespace torrview::ui
