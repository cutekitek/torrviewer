#include "ui/settings_page.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>

namespace torrview::ui {
namespace {

constexpr Clay_Color color_background = {12.0F, 13.0F, 15.0F, 255.0F};
constexpr Clay_Color color_panel = {20.0F, 23.0F, 27.0F, 255.0F};
constexpr Clay_Color color_input = {14.0F, 15.0F, 17.0F, 255.0F};
constexpr Clay_Color color_button = {34.0F, 38.0F, 44.0F, 255.0F};
constexpr Clay_Color color_accent = {74.0F, 181.0F, 162.0F, 255.0F};
constexpr Clay_Color color_text = {235.0F, 238.0F, 243.0F, 255.0F};
constexpr Clay_Color color_muted = {166.0F, 174.0F, 186.0F, 255.0F};
constexpr Clay_Color color_border = {198.0F, 203.0F, 214.0F, 255.0F};

Clay_String clay_string(std::string_view value) {
  const std::size_t max_length = static_cast<std::size_t>(std::numeric_limits<int32_t>::max());
  const std::size_t length = std::min(value.size(), max_length);
  return {.isStaticallyAllocated = false,
          .length = static_cast<int32_t>(length),
          .chars = value.data()};
}

bool append_numeric(std::string& target, std::string_view value) {
  bool changed = false;
  for (char character : value) {
    if (std::isdigit(static_cast<unsigned char>(character)) == 0 || target.size() >= 7) {
      continue;
    }
    target.push_back(character);
    changed = true;
  }
  return changed;
}

} // namespace

void SettingsPage::initialize_ids() {
  close_id_ = Clay_GetElementId(CLAY_STRING("SettingsClose"));
  save_id_ = Clay_GetElementId(CLAY_STRING("SettingsSave"));
  font_size_id_ = Clay_GetElementId(CLAY_STRING("SettingsFontSize"));
  upload_speed_id_ = Clay_GetElementId(CLAY_STRING("SettingsUploadSpeed"));
  max_connections_id_ = Clay_GetElementId(CLAY_STRING("SettingsMaxConnections"));
  proxy_url_id_ = Clay_GetElementId(CLAY_STRING("SettingsProxyUrl"));
  proxy_peer_id_ = Clay_GetElementId(CLAY_STRING("SettingsProxyPeer"));
  proxy_tracker_id_ = Clay_GetElementId(CLAY_STRING("SettingsProxyTracker"));
  proxy_dns_id_ = Clay_GetElementId(CLAY_STRING("SettingsProxyDns"));
}

void SettingsPage::begin_frame() { frame_strings_.clear(); }

void SettingsPage::open(const AppSettings& settings) {
  const AppSettings clamped = clamp_settings(settings);
  font_size_ = clamped.font_size;
  font_size_text_ = std::to_string(clamped.font_size);
  upload_speed_text_ = std::to_string(clamped.upload_speed_kib);
  max_connections_text_ = std::to_string(clamped.max_connections);
  proxy_url_text_ = clamped.proxy_url;
  proxy_peer_connections_ = clamped.proxy_peer_connections;
  proxy_tracker_connections_ = clamped.proxy_tracker_connections;
  proxy_dns_ = clamped.proxy_dns;
  focused_field_ = Field::none;
}

void SettingsPage::set_font_size(int font_size) {
  AppSettings settings;
  settings.font_size = font_size;
  font_size_ = clamp_settings(settings).font_size;
}

std::string_view SettingsPage::retain_frame_text(std::string value) {
  frame_strings_.push_back(std::move(value));
  return frame_strings_.back();
}

uint16_t SettingsPage::scaled_font_size(uint16_t font_size) const {
  const float scale = static_cast<float>(std::clamp(font_size_, 9, 24)) / 13.0F;
  return static_cast<uint16_t>(std::clamp<int>(static_cast<int>(std::lround(font_size * scale)), 8,
                                               32));
}

float SettingsPage::line_height(uint16_t font_size) const {
  return static_cast<float>(scaled_font_size(font_size)) * 1.25F;
}

void SettingsPage::text(std::string_view value, uint16_t font_size, Clay_Color color,
                        Clay_TextElementConfigWrapMode wrap) {
  const uint16_t scaled_size = scaled_font_size(font_size);
  CLAY_TEXT(clay_string(value), CLAY_TEXT_CONFIG({.textColor = color,
                                                  .fontSize = scaled_size,
                                                  .lineHeight = static_cast<uint16_t>(std::lround(
                                                      static_cast<float>(scaled_size) * 1.25F)),
                                                  .wrapMode = wrap}));
}

void SettingsPage::input_row(std::string_view label, Clay_ElementId id, Field field,
                             std::string_view value, std::string_view suffix) {
  const bool focused = focused_field_ == field;
  CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                      .height = CLAY_SIZING_FIT(0.0F)},
                          .childGap = 14,
                          .childAlignment = {.x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER}}}) {
    CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_FIT(0.0F),
                                        .height = CLAY_SIZING_FIT(0.0F)},
                            .childAlignment = {.x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER}},
                  .clip = {.horizontal = true}}) {
      text(label, 12, color_muted, CLAY_TEXT_WRAP_NONE);
    }
    CLAY(id,
         {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                .height = CLAY_SIZING_FIT(0.0F)},
                     .padding = {12, 12, 0, 0},
                     .childAlignment = {.x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = color_input,
          .cornerRadius = CLAY_CORNER_RADIUS(6.0F),
          .border = {.color = focused ? color_accent : color_border,
                     .width = {.left = 1, .right = 1, .top = 1, .bottom = 1}}}) {
      text(value.empty() ? std::string_view(" ") : value, 13, color_text, CLAY_TEXT_WRAP_NONE);
    }
    if (!suffix.empty()) {
      CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_FIT(0.0F),
                                          .height = CLAY_SIZING_FIT(0.0F)},
                              .childAlignment = {.x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER}}}) {
        text(suffix, 12, color_muted, CLAY_TEXT_WRAP_NONE);
      }
    }
  }
}

void SettingsPage::toggle_row(std::string_view label, Clay_ElementId id, bool value) {
  const float box_size = std::max(24.0F, line_height(12));
  CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                      .height = CLAY_SIZING_FIT(0.0F)},
                          .childGap = 12,
                          .childAlignment = {.x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER}}}) {
    CLAY(id,
         {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(box_size),
                                .height = CLAY_SIZING_FIXED(box_size)},
                     .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = value ? color_accent : color_input,
          .cornerRadius = CLAY_CORNER_RADIUS(5.0F),
          .border = {.color = value ? color_accent : color_border,
                     .width = {.left = 1, .right = 1, .top = 1, .bottom = 1}}}) {
      if (value) {
        text("x", 14, color_text, CLAY_TEXT_WRAP_NONE);
      }
    }
    text(label, 12, color_text, CLAY_TEXT_WRAP_NONE);
  }
}

void SettingsPage::build(const WindowMetrics& metrics) {
  const float width = static_cast<float>(metrics.logical_width);
  const float height = static_cast<float>(metrics.logical_height);
  const float panel_width = std::clamp(width - 48.0F, 460.0F, 900.0F);
  constexpr uint16_t gap = 10;
  const float max_panel_height = std::max(360.0F, height - 48.0F);

  CLAY(CLAY_ID("SettingsRoot"),
       {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F), .height = CLAY_SIZING_GROW(0.0F)},
                   .padding = {24, 24, 24, 24},
                   .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
        .backgroundColor = color_background}) {
    CLAY(CLAY_ID("SettingsPanel"),
         {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(panel_width),
                                .height = CLAY_SIZING_FIT(0.0F, max_panel_height)},
                     .padding = {22, 22, 18, 18},
                     .childGap = gap,
                     .layoutDirection = CLAY_TOP_TO_BOTTOM},
          .backgroundColor = color_panel,
          .cornerRadius = CLAY_CORNER_RADIUS(8.0F),
          .clip = {.vertical = true, .childOffset = Clay_GetScrollOffset()}}) {
      CLAY(CLAY_ID("SettingsHeader"),
           {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                  .height = CLAY_SIZING_FIT(0.0F)},
                       .childGap = 12,
                       .childAlignment = {.x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER}}}) {
        text("Settings", 18, color_text, CLAY_TEXT_WRAP_NONE);
        CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                            .height = CLAY_SIZING_FIXED(1.0F)}}}) {}
        CLAY(close_id_,
             {.layout = {.sizing = {.width = CLAY_SIZING_FIT(0.0F),
                                    .height = CLAY_SIZING_FIT(0.0F)},
                         .padding = {10, 10, 5, 5},
                         .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = color_button,
              .cornerRadius = CLAY_CORNER_RADIUS(6.0F)}) {
          text("Close", 12, color_text, CLAY_TEXT_WRAP_NONE);
        }
      }

      input_row("Font size", font_size_id_, Field::font_size, font_size_text_, "px");
      input_row("Upload speed", upload_speed_id_, Field::upload_speed, upload_speed_text_, "KiB/s");
      input_row("Max connections", max_connections_id_, Field::max_connections,
                max_connections_text_);
      input_row("Proxy URL", proxy_url_id_, Field::proxy_url, proxy_url_text_);

      CLAY(CLAY_ID("SettingsProxyFlags"),
           {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                  .height = CLAY_SIZING_FIT(0.0F)},
                       .childGap = 6,
                       .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
        toggle_row("Proxy peer connections", proxy_peer_id_, proxy_peer_connections_);
        toggle_row("Proxy tracker connections", proxy_tracker_id_, proxy_tracker_connections_);
        toggle_row("Proxy DNS", proxy_dns_id_, proxy_dns_);
      }

      CLAY(CLAY_ID("SettingsFooter"),
           {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                  .height = CLAY_SIZING_FIT(0.0F)},
                       .childAlignment = {.x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER}}}) {
        CLAY(save_id_,
             {.layout = {.sizing = {.width = CLAY_SIZING_FIT(0.0F),
                                    .height = CLAY_SIZING_FIT(0.0F)},
                         .padding = {16, 16, 6, 6},
                         .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = color_accent,
              .cornerRadius = CLAY_CORNER_RADIUS(6.0F)}) {
          text("Save", 12, color_text, CLAY_TEXT_WRAP_NONE);
        }
      }
    }
  }
}

SettingsPageAction SettingsPage::hit_test_click() {
  if (Clay_PointerOver(close_id_)) {
    focused_field_ = Field::none;
    return SettingsPageAction::close;
  }
  if (Clay_PointerOver(save_id_)) {
    focused_field_ = Field::none;
    return SettingsPageAction::save;
  }
  if (Clay_PointerOver(font_size_id_)) {
    focused_field_ = Field::font_size;
    return SettingsPageAction::none;
  }
  if (Clay_PointerOver(upload_speed_id_)) {
    focused_field_ = Field::upload_speed;
    return SettingsPageAction::none;
  }
  if (Clay_PointerOver(max_connections_id_)) {
    focused_field_ = Field::max_connections;
    return SettingsPageAction::none;
  }
  if (Clay_PointerOver(proxy_url_id_)) {
    focused_field_ = Field::proxy_url;
    return SettingsPageAction::none;
  }
  if (Clay_PointerOver(proxy_peer_id_)) {
    proxy_peer_connections_ = !proxy_peer_connections_;
    focused_field_ = Field::none;
    return SettingsPageAction::none;
  }
  if (Clay_PointerOver(proxy_tracker_id_)) {
    proxy_tracker_connections_ = !proxy_tracker_connections_;
    focused_field_ = Field::none;
    return SettingsPageAction::none;
  }
  if (Clay_PointerOver(proxy_dns_id_)) {
    proxy_dns_ = !proxy_dns_;
    focused_field_ = Field::none;
    return SettingsPageAction::none;
  }

  focused_field_ = Field::none;
  return SettingsPageAction::none;
}

void SettingsPage::append_text(std::string_view value) {
  switch (focused_field_) {
  case Field::font_size:
    append_numeric(font_size_text_, value);
    break;
  case Field::upload_speed:
    append_numeric(upload_speed_text_, value);
    break;
  case Field::max_connections:
    append_numeric(max_connections_text_, value);
    break;
  case Field::proxy_url:
    proxy_url_text_.append(value);
    break;
  case Field::none:
    break;
  }
}

void SettingsPage::backspace() {
  std::string* target = nullptr;
  switch (focused_field_) {
  case Field::font_size:
    target = &font_size_text_;
    break;
  case Field::upload_speed:
    target = &upload_speed_text_;
    break;
  case Field::max_connections:
    target = &max_connections_text_;
    break;
  case Field::proxy_url:
    target = &proxy_url_text_;
    break;
  case Field::none:
    break;
  }
  if (target != nullptr && !target->empty()) {
    target->pop_back();
  }
}

void SettingsPage::focus_next() {
  switch (focused_field_) {
  case Field::none:
    focused_field_ = Field::font_size;
    break;
  case Field::font_size:
    focused_field_ = Field::upload_speed;
    break;
  case Field::upload_speed:
    focused_field_ = Field::max_connections;
    break;
  case Field::max_connections:
    focused_field_ = Field::proxy_url;
    break;
  case Field::proxy_url:
    focused_field_ = Field::font_size;
    break;
  }
}

int SettingsPage::int_field(std::string_view value, int fallback) const {
  try {
    return std::stoi(std::string(value));
  } catch (const std::exception&) {
    return fallback;
  }
}

void SettingsPage::write_settings(AppSettings& settings) const {
  settings.font_size = int_field(font_size_text_, settings.font_size);
  settings.upload_speed_kib = int_field(upload_speed_text_, settings.upload_speed_kib);
  settings.max_connections = int_field(max_connections_text_, settings.max_connections);
  settings.proxy_url = proxy_url_text_;
  settings.proxy_peer_connections = proxy_peer_connections_;
  settings.proxy_tracker_connections = proxy_tracker_connections_;
  settings.proxy_dns = proxy_dns_;
  settings = clamp_settings(settings);
}

bool SettingsPage::has_focus() const { return focused_field_ != Field::none; }

} // namespace torrview::ui
