#include "ui/settings_page.hpp"

#include "../app.hpp"
#include "ui/render_context.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace torrview::ui {
namespace {

constexpr float control_width = 156.0F;

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

SettingsPage::SettingsPage(Application& app, Settings& settings, MpvPlayer* player,
                           TorrentService& torrent_service)
    : app_(app), settings_(settings), player_(player), torrent_service_(torrent_service),
      ui_(settings) {
  initialize_ids();
}

void SettingsPage::initialize_ids() {
  close_id_ = CLAY_ID("SettingsClose");
  cancel_id_ = CLAY_ID("SettingsCancel");
  save_id_ = CLAY_ID("SettingsSave");
  font_size_id_ = CLAY_ID("SettingsFontSize");
  buffer_size_id_ = CLAY_ID("SettingsBufferSize");
  upload_speed_id_ = CLAY_ID("SettingsUploadSpeed");
  max_connections_id_ = CLAY_ID("SettingsMaxConnections");
  proxy_url_id_ = CLAY_ID("SettingsProxyUrl");
  proxy_peer_id_ = CLAY_ID("SettingsProxyPeer");
  proxy_tracker_id_ = CLAY_ID("SettingsProxyTracker");
  proxy_dns_id_ = CLAY_ID("SettingsProxyDns");
}

void SettingsPage::begin_frame() { ui_.begin_frame(); }

void SettingsPage::open(const AppSettings& settings) {
  const AppSettings clamped = clamp_settings(settings);
  font_size_text_ = std::to_string(clamped.font_size);
  buffer_size_text_ = std::to_string(clamped.buffer_size_mib);
  upload_speed_text_ = std::to_string(clamped.upload_speed_kib);
  max_connections_text_ = std::to_string(clamped.max_connections);
  proxy_url_text_ = clamped.proxy_url;
  proxy_peer_connections_ = clamped.proxy_peer_connections;
  proxy_tracker_connections_ = clamped.proxy_tracker_connections;
  proxy_dns_ = clamped.proxy_dns;
  focused_field_ = Field::none;
}

void SettingsPage::draw(RenderContext& render_context) {
  begin_frame();
  build(render_context.metrics);
}

bool SettingsPage::handle_event(const SDL_Event& event) {
  switch (event.type) {
  case SDL_EVENT_TEXT_INPUT:
    if (event.text.text != nullptr) {
      append_text(event.text.text);
    }
    return true;
  case SDL_EVENT_KEY_DOWN: {
    const bool command = (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
    if (command && event.key.key == SDLK_V && has_focus()) {
      char* clipboard = SDL_GetClipboardText();
      if (clipboard != nullptr) {
        append_text(clipboard);
        SDL_free(clipboard);
      }
      return true;
    }

    switch (event.key.key) {
    case SDLK_ESCAPE:
      if (event.key.repeat) {
        break;
      }
      if (has_focus()) {
        focused_field_ = Field::none;
      } else {
        return_to_main_page();
      }
      break;
    case SDLK_BACKSPACE:
      backspace();
      break;
    case SDLK_TAB:
      focus_next();
      break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
      if (!event.key.repeat) {
        save_settings();
      }
      break;
    default:
      break;
    }
    return true;
  }
  case SDL_EVENT_MOUSE_BUTTON_UP:
    if (event.button.button == SDL_BUTTON_LEFT) {
      Clay_SetPointerState({.x = event.button.x, .y = event.button.y}, false);
      handle_click_action(hit_test_click());
      return true;
    }
    break;
  default:
    break;
  }
  return false;
}

void SettingsPage::close() { focused_field_ = Field::none; }

void SettingsPage::section(std::string_view title) {
  CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F)},
                           .padding = {0, 0, 36, 10},
                           .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
    ui_.text(title, theme::size_lead, theme::lamp, theme::font_semibold);
  }
  ui_.rule();
}

void SettingsPage::number_row(std::string_view label, std::string_view help, Clay_ElementId id,
                              Field field, std::string_view value, std::string_view suffix) {
  CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F)},
                           .padding = {0, 0, 14, 14},
                           .childGap = 24,
                           .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
    CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F)},
                             .childGap = 3,
                             .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
      ui_.text(label, theme::size_body, theme::lamp);
      if (!help.empty()) {
        ui_.text(help, theme::size_caption, theme::ash, theme::font_regular, CLAY_TEXT_WRAP_WORDS);
      }
    }
    ui_.field(id, value, {}, suffix, focused_field_ == field, control_width);
  }
  ui_.rule();
}

void SettingsPage::toggle_row(std::string_view label, Clay_ElementId id, bool value, bool enabled) {
  CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F)},
                           .padding = {0, 0, 12, 12},
                           .childGap = 24,
                           .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
    ui_.text(label, theme::size_body, enabled ? theme::lamp : theme::smoke);
    ui_.spacer();
    ui_.toggle(id, value, enabled);
  }
}

void SettingsPage::build(const WindowMetrics& metrics) {
  const float width = static_cast<float>(metrics.logical_width);
  const float column_width = std::clamp(width - 96.0F, 320.0F, 640.0F);
  const bool proxy_set = !proxy_url_text_.empty();
  const bool changed = dirty();

  CLAY(CLAY_ID("SettingsRoot"),
       {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F), .height = CLAY_SIZING_GROW(0.0F)},
                   .layoutDirection = CLAY_TOP_TO_BOTTOM},
        .backgroundColor = theme::graphite}) {
    CLAY(CLAY_ID("SettingsScroll"),
         {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F), .height = CLAY_SIZING_GROW(0.0F)},
                     .padding = {0, 0, 24, 32},
                     .childAlignment = {.x = CLAY_ALIGN_X_CENTER},
                     .layoutDirection = CLAY_TOP_TO_BOTTOM},
          .clip = {.vertical = true, .childOffset = Clay_GetScrollOffset()}}) {
      CLAY(CLAY_ID("SettingsColumn"),
           {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(column_width)},
                       .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
        CLAY(close_id_,
             {.layout = {.sizing = {.width = CLAY_SIZING_FIT(0.0F),
                                    .height = CLAY_SIZING_FIXED(32.0F)},
                         .padding = {4, 12, 0, 0},
                         .childGap = 4,
                         .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = Clay_PointerOver(close_id_) ? theme::raised : theme::clear,
              .cornerRadius = CLAY_CORNER_RADIUS(theme::radius_control)}) {
          ui_.icon("icons/back.svg", 18.0F);
          ui_.text("Back", theme::size_body, theme::ash);
        }
        ui_.gap(1.0F, 22.0F);
        ui_.text("Settings", theme::size_title, theme::lamp, theme::font_semibold);

        section("Display");
        number_row("Text size", "Scales every label in Torrview.", font_size_id_, Field::font_size,
                   font_size_text_, "px");

        section("Streaming");
        number_row("Stream buffer",
                   "Pieces of the video are held in memory and never written to disk.",
                   buffer_size_id_, Field::buffer_size, buffer_size_text_, "MiB");
        number_row("Upload limit", "Set to 0 to upload without a limit.", upload_speed_id_,
                   Field::upload_speed, upload_speed_text_, "KiB/s");
        number_row("Connection limit", "The most peers Torrview talks to at once.",
                   max_connections_id_, Field::max_connections, max_connections_text_, {});

        section("Proxy");
        CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F)},
                                 .padding = {0, 0, 14, 10},
                                 .childGap = 8,
                                 .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
          ui_.text("Proxy address", theme::size_body, theme::lamp);
          ui_.field(proxy_url_id_, proxy_url_text_, "socks5://127.0.0.1:9050", {},
                    focused_field_ == Field::proxy_url, column_width);
          ui_.text("Leave empty to connect directly.", theme::size_caption, theme::ash);
        }
        toggle_row("Send peer traffic through the proxy", proxy_peer_id_, proxy_peer_connections_,
                   proxy_set);
        toggle_row("Send tracker requests through the proxy", proxy_tracker_id_,
                   proxy_tracker_connections_, proxy_set);
        toggle_row("Look up host names through the proxy", proxy_dns_id_, proxy_dns_, proxy_set);
      }
    }
    ui_.scrollbar(CLAY_ID("SettingsScroll"));

    CLAY(CLAY_ID("SettingsFooter"), {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F)},
                                                .padding = {0, 0, 0, 0},
                                                .childAlignment = {.x = CLAY_ALIGN_X_CENTER},
                                                .layoutDirection = CLAY_TOP_TO_BOTTOM},
                                     .backgroundColor = theme::graphite}) {
      ui_.rule();
      CLAY(CLAY_ID("SettingsFooterRow"),
           {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(column_width)},
                       .padding = {0, 0, 14, 14},
                       .childGap = 8,
                       .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
        if (changed) {
          ui_.text("Unsaved changes", theme::size_caption, theme::ash);
        }
        ui_.spacer();
        ui_.button(cancel_id_, changed ? "Discard" : "Close", ButtonKind::quiet);
        ui_.button(save_id_, "Save changes", changed ? ButtonKind::primary : ButtonKind::secondary);
      }
    }
  }
}

SettingsPageAction SettingsPage::hit_test_click() {
  const auto focus = [this](Field field) {
    focused_field_ = field;
    return SettingsPageAction::none;
  };
  if (Clay_PointerOver(close_id_) || Clay_PointerOver(cancel_id_)) {
    focused_field_ = Field::none;
    return SettingsPageAction::close;
  }
  if (Clay_PointerOver(save_id_)) {
    focused_field_ = Field::none;
    return SettingsPageAction::save;
  }
  if (Clay_PointerOver(font_size_id_)) {
    return focus(Field::font_size);
  }
  if (Clay_PointerOver(buffer_size_id_)) {
    return focus(Field::buffer_size);
  }
  if (Clay_PointerOver(upload_speed_id_)) {
    return focus(Field::upload_speed);
  }
  if (Clay_PointerOver(max_connections_id_)) {
    return focus(Field::max_connections);
  }
  if (Clay_PointerOver(proxy_url_id_)) {
    return focus(Field::proxy_url);
  }
  if (!proxy_url_text_.empty()) {
    if (Clay_PointerOver(proxy_peer_id_)) {
      proxy_peer_connections_ = !proxy_peer_connections_;
    } else if (Clay_PointerOver(proxy_tracker_id_)) {
      proxy_tracker_connections_ = !proxy_tracker_connections_;
    } else if (Clay_PointerOver(proxy_dns_id_)) {
      proxy_dns_ = !proxy_dns_;
    }
  }
  return focus(Field::none);
}

void SettingsPage::handle_click_action(SettingsPageAction action) {
  switch (action) {
  case SettingsPageAction::save:
    save_settings();
    break;
  case SettingsPageAction::close:
    return_to_main_page();
    break;
  case SettingsPageAction::none:
    break;
  }
}

bool SettingsPage::dirty() const {
  AppSettings next = settings_.values();
  write_settings(next);
  const AppSettings& current = settings_.values();
  return next.font_size != current.font_size || next.buffer_size_mib != current.buffer_size_mib ||
         next.upload_speed_kib != current.upload_speed_kib ||
         next.max_connections != current.max_connections || next.proxy_url != current.proxy_url ||
         next.proxy_peer_connections != current.proxy_peer_connections ||
         next.proxy_tracker_connections != current.proxy_tracker_connections ||
         next.proxy_dns != current.proxy_dns;
}

void SettingsPage::save_settings() {
  AppSettings next = settings_.values();
  write_settings(next);
  settings_.replace(std::move(next));
  settings_.save();
  app_.apply_settings();
  return_to_main_page();
}

void SettingsPage::return_to_main_page() {
  if (player_ != nullptr && player_->has_file()) {
    app_.navigate<PlayerOverlay>();
  } else if (torrent_service_.snapshot().state != TorrentLoadState::idle) {
    app_.navigate<FileBrowserPage>();
  } else {
    app_.navigate<TitlePage>();
  }
}

void SettingsPage::append_text(std::string_view value) {
  switch (focused_field_) {
  case Field::font_size:
    append_numeric(font_size_text_, value);
    break;
  case Field::buffer_size:
    append_numeric(buffer_size_text_, value);
    break;
  case Field::upload_speed:
    append_numeric(upload_speed_text_, value);
    break;
  case Field::max_connections:
    append_numeric(max_connections_text_, value);
    break;
  case Field::proxy_url:
    for (char character : value) {
      if (character != '\r' && character != '\n' && character != '\t') {
        proxy_url_text_.push_back(character);
      }
    }
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
  case Field::buffer_size:
    target = &buffer_size_text_;
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
  if (target == nullptr || target->empty()) {
    return;
  }
  // Remove one UTF-8 code point.
  std::size_t end = target->size() - 1;
  while (end > 0 && (static_cast<unsigned char>((*target)[end]) & 0xC0U) == 0x80U) {
    --end;
  }
  target->erase(end);
}

void SettingsPage::focus_next() {
  switch (focused_field_) {
  case Field::none:
  case Field::proxy_url:
    focused_field_ = Field::font_size;
    break;
  case Field::font_size:
    focused_field_ = Field::buffer_size;
    break;
  case Field::buffer_size:
    focused_field_ = Field::upload_speed;
    break;
  case Field::upload_speed:
    focused_field_ = Field::max_connections;
    break;
  case Field::max_connections:
    focused_field_ = Field::proxy_url;
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
  settings.buffer_size_mib = int_field(buffer_size_text_, settings.buffer_size_mib);
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
