#include "ui/title_page.hpp"

#include "../app.hpp"
#include "ui/render_context.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace torrview::ui {
namespace {

constexpr Clay_Color color_background = {12.0F, 13.0F, 15.0F, 255.0F};
constexpr Clay_Color color_text = {233.0F, 236.0F, 241.0F, 255.0F};
constexpr Clay_Color color_accent = {74.0F, 181.0F, 162.0F, 255.0F};
constexpr Clay_Color color_error = {214.0F, 94.0F, 82.0F, 255.0F};
constexpr Clay_Padding text_padding = {4, 4, 4, 4};

Clay_String clay_string(std::string_view value) {
  const std::size_t max_length = static_cast<std::size_t>(std::numeric_limits<int32_t>::max());
  const std::size_t length = std::min(value.size(), max_length);
  return {.isStaticallyAllocated = false,
          .length = static_cast<int32_t>(length),
          .chars = value.data()};
}

} // namespace

TitlePage::TitlePage(Application& app, Settings& settings, const bool& drag_active,
                     const std::optional<ParsedInput>& last_input)
    : app_(app), settings_(settings), drag_active_(drag_active), last_input_(last_input) {
  initialize_ids();
}

void TitlePage::open() {
  app_.cancel_resume();
  input_focused_ = false;
  menu_key_.clear();
  modal_key_.clear();
}

void TitlePage::draw(RenderContext& render_context) {
  begin_frame();
  build(render_context.metrics, drag_active_, last_input_);
}

void TitlePage::confirm_modal() {
  if (deleting_)
    app_.delete_library(modal_key_);
  else {
    if (trim(rename_).empty())
      return;
    app_.rename_library(modal_key_, rename_);
  }
  modal_key_.clear();
}

namespace {
std::size_t previous_character(std::string_view value, std::size_t at) {
  if (at == 0)
    return 0;
  --at;
  while (at > 0 && (static_cast<unsigned char>(value[at]) & 0xc0) == 0x80)
    --at;
  return at;
}
std::size_t next_character(std::string_view value, std::size_t at) {
  if (at >= value.size())
    return value.size();
  ++at;
  while (at < value.size() && (static_cast<unsigned char>(value[at]) & 0xc0) == 0x80)
    ++at;
  return at;
}
} // namespace

void TitlePage::replace_selection(std::string& value, std::string_view inserted) {
  const auto first = std::min(cursor_, anchor_);
  const auto last = std::max(cursor_, anchor_);
  std::string cleaned;
  for (char c : inserted)
    if (c != '\r' && c != '\n' && c != '\t')
      cleaned += c;
  if (value.size() - (last - first) + cleaned.size() > 16384)
    return;
  value.replace(first, last - first, cleaned);
  cursor_ = first + cleaned.size();
  anchor_ = cursor_;
  caret_epoch_ = SDL_GetTicks();
}

bool TitlePage::edit_input(std::string& value, const SDL_Event& event) {
  cursor_ = std::min(cursor_, value.size());
  anchor_ = std::min(anchor_, value.size());
  if (event.type == SDL_EVENT_TEXT_INPUT && event.text.text) {
    replace_selection(value, event.text.text);
    return true;
  }
  if (event.type != SDL_EVENT_KEY_DOWN)
    return false;
  const bool command = (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
  const bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
  if (command && event.key.key == SDLK_A) {
    anchor_ = 0;
    cursor_ = value.size();
  } else if (command && (event.key.key == SDLK_C || event.key.key == SDLK_X)) {
    if (cursor_ != anchor_) {
      SDL_SetClipboardText(value
                               .substr(std::min(cursor_, anchor_),
                                       std::max(cursor_, anchor_) - std::min(cursor_, anchor_))
                               .c_str());
      if (event.key.key == SDLK_X)
        replace_selection(value, "");
    }
  } else if (command && event.key.key == SDLK_V) {
    if (char* clipboard = SDL_GetClipboardText()) {
      replace_selection(value, clipboard);
      SDL_free(clipboard);
    }
  } else if (event.key.key == SDLK_BACKSPACE || event.key.key == SDLK_DELETE) {
    if (cursor_ == anchor_) {
      if (event.key.key == SDLK_BACKSPACE)
        anchor_ = previous_character(value, cursor_);
      else
        anchor_ = next_character(value, cursor_);
    }
    replace_selection(value, "");
  } else if (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT ||
             event.key.key == SDLK_HOME || event.key.key == SDLK_END) {
    if (event.key.key == SDLK_HOME)
      cursor_ = 0;
    else if (event.key.key == SDLK_END)
      cursor_ = value.size();
    else if (!shift && cursor_ != anchor_)
      cursor_ =
          event.key.key == SDLK_LEFT ? std::min(cursor_, anchor_) : std::max(cursor_, anchor_);
    else
      cursor_ = event.key.key == SDLK_LEFT ? previous_character(value, cursor_)
                                           : next_character(value, cursor_);
    if (!shift)
      anchor_ = cursor_;
  } else
    return false;
  caret_epoch_ = SDL_GetTicks();
  return true;
}

void TitlePage::focus_input(Clay_ElementId id, const std::string& value, float pointer_x) {
  const float local =
      std::max(0.0F, pointer_x - Clay_GetElementData(id).boundingBox.x - 14 + input_scroll_);
  std::vector<std::size_t> boundaries{0};
  for (std::size_t at = 0; at < value.size();) {
    at = next_character(value, at);
    boundaries.push_back(at);
  }
  auto found = std::lower_bound(
      boundaries.begin(), boundaries.end(), local, [&](std::size_t offset, float x) {
        return app_.text_width(std::string_view(value).substr(0, offset), scaled_font_size(15)) < x;
      });
  cursor_ = found == boundaries.end() ? value.size() : *found;
  if (found != boundaries.begin() && found != boundaries.end()) {
    const auto before = *(found - 1);
    const float midpoint =
        (app_.text_width(std::string_view(value).substr(0, before), scaled_font_size(15)) +
         app_.text_width(std::string_view(value).substr(0, *found), scaled_font_size(15))) /
        2;
    if (local < midpoint)
      cursor_ = before;
  }
  anchor_ = cursor_;
  caret_epoch_ = SDL_GetTicks();
}

void TitlePage::submit_magnet() {
  if (!is_magnet_link(magnet_)) {
    input_error_ = "Enter a magnet URL beginning with magnet:?";
    input_focused_ = true;
    return;
  }
  input_error_.clear();
  app_.open_text(trim(magnet_));
  input_focused_ = false;
}

bool TitlePage::handle_event(const SDL_Event& event) {
  const bool modal = !modal_key_.empty();
  if (!modal && !input_focused_ && event.type == SDL_EVENT_KEY_DOWN &&
      ((event.key.key == SDLK_V && (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) ||
       event.key.key == SDLK_TAB)) {
    input_focused_ = true;
    cursor_ = anchor_ = magnet_.size();
    caret_epoch_ = SDL_GetTicks();
  }
  std::string* input =
      modal ? (deleting_ ? nullptr : &rename_) : (input_focused_ ? &magnet_ : nullptr);
  if (input && edit_input(*input, event))
    return true;
  if (event.type == SDL_EVENT_KEY_DOWN && (modal || input)) {
    if (event.key.key == SDLK_ESCAPE) {
      modal_key_.clear();
      input_focused_ = false;
    } else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
      if (modal)
        confirm_modal();
      else
        submit_magnet();
    }
    return true;
  }
  if (event.type != SDL_EVENT_MOUSE_BUTTON_UP || event.button.button != SDL_BUTTON_LEFT)
    return modal;
  Clay_SetPointerState({event.button.x, event.button.y}, false);
  if (modal) {
    if (Clay_PointerOver(CLAY_ID("LibraryConfirm")))
      confirm_modal();
    else if (Clay_PointerOver(CLAY_ID("LibraryCancel")))
      modal_key_.clear();
    else if (!deleting_ && Clay_PointerOver(CLAY_ID("RenameInput")))
      focus_input(CLAY_ID("RenameInput"), rename_, event.button.x);
    return true;
  }
  if (!menu_key_.empty() &&
      (Clay_PointerOver(CLAY_ID("LibraryDelete")) || Clay_PointerOver(CLAY_ID("LibraryRename")))) {
    deleting_ = Clay_PointerOver(CLAY_ID("LibraryDelete"));
    modal_key_ = menu_key_;
    for (const auto& entry : app_.library().entries)
      if (entry.key == modal_key_)
        rename_ = entry.name;
    anchor_ = 0;
    cursor_ = rename_.size();
    input_scroll_ = 0;
    caret_epoch_ = SDL_GetTicks();
    menu_key_.clear();
    return true;
  }
  input_focused_ = Clay_PointerOver(CLAY_ID("MagnetInput"));
  if (input_focused_)
    focus_input(CLAY_ID("MagnetInput"), magnet_, event.button.x);
  if (Clay_PointerOver(settings_id_))
    app_.navigate<SettingsPage>(settings_.values());
  else if (Clay_PointerOver(source_panel_id_))
    app_.show_source_dialog();
  else
    for (const auto& row : rows_) {
      if (Clay_PointerOver(row.menu)) {
        menu_key_ = menu_key_ == row.key ? "" : row.key;
        return true;
      }
      if (Clay_PointerOver(row.play)) {
        app_.open_library(row.key, true);
        return true;
      }
      if (Clay_PointerOver(row.name)) {
        app_.open_library(row.key, false);
        return true;
      }
    }
  menu_key_.clear();
  return true;
}

void TitlePage::close() {}

void TitlePage::initialize_ids() {
  source_panel_id_ = CLAY_ID("SourcePanel");
  settings_id_ = CLAY_ID("TitleSettings");
}

Clay_ElementId TitlePage::source_panel_id() const { return source_panel_id_; }

Clay_ElementId TitlePage::settings_button_id() const { return settings_id_; }

void TitlePage::set_font_size(int font_size) { font_size_ = std::clamp(font_size, 9, 24); }

void TitlePage::begin_frame() { frame_strings_.clear(); }

std::string TitlePage::status_text(const std::optional<ParsedInput>& last_input) const {
  if (!last_input.has_value()) {
    return "Paste a magnet URL and press Enter, or open a torrent file";
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
  CLAY_AUTO_ID(
      {.layout = {.sizing = {.width = CLAY_SIZING_FIT(0.0F), .height = CLAY_SIZING_FIT(0.0F)},
                  .padding = text_padding,
                  .childAlignment = {.x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER}}}) {
    CLAY_TEXT(clay_string(value), CLAY_TEXT_CONFIG({.textColor = color,
                                                    .fontSize = scaled_size,
                                                    .lineHeight = static_cast<uint16_t>(std::lround(
                                                        static_cast<float>(scaled_size) * 1.25F)),
                                                    .wrapMode = wrap}));
  }
}

uint16_t TitlePage::scaled_font_size(uint16_t font_size) const {
  const float scale = static_cast<float>(std::clamp(settings_.values().font_size, 9, 24)) / 13.0F;
  return static_cast<uint16_t>(
      std::clamp<int>(static_cast<int>(std::lround(font_size * scale)), 8, 32));
}

float TitlePage::line_height(uint16_t font_size) const {
  return static_cast<float>(scaled_font_size(font_size)) * 1.25F;
}

void TitlePage::button(Clay_ElementId id, std::string_view label, bool grow, bool destructive) {
  CLAY(id, {.layout = {.sizing = {.width = grow ? CLAY_SIZING_GROW(0) : CLAY_SIZING_FIT(0),
                                  .height = CLAY_SIZING_FIXED(48)},
                       .padding = {12, 12, 4, 4},
                       .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
            .backgroundColor =
                Clay_PointerOver(id) ? Clay_Color{38, 43, 48, 255} : Clay_Color{22, 24, 28, 255},
            .cornerRadius = CLAY_CORNER_RADIUS(10),
            .clip = {.horizontal = true},
            .border = {.color = {100, 105, 112, 255}, .width = {1, 1, 1, 1, 0}}}) {
    text(label, 15, destructive ? color_error : color_text);
  }
}

void TitlePage::icon_button(Clay_ElementId id, const char* asset, std::string_view hint) {
  CLAY(id, {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(48), .height = CLAY_SIZING_FIXED(48)},
                       .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
            .backgroundColor =
                Clay_PointerOver(id) ? Clay_Color{38, 43, 48, 255} : Clay_Color{22, 24, 28, 255},
            .cornerRadius = CLAY_CORNER_RADIUS(10)}) {
    CLAY_AUTO_ID(
        {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(26), .height = CLAY_SIZING_FIXED(26)}},
         .image = {.imageData = const_cast<char*>(asset)}}) {}
    if (Clay_PointerOver(id) && modal_key_.empty() && menu_key_.empty()) {
      CLAY_AUTO_ID({.layout = {.padding = {8, 8, 4, 4}},
                    .backgroundColor = {42, 47, 54, 255},
                    .cornerRadius = CLAY_CORNER_RADIUS(5),
                    .floating = {.offset = {0, 6},
                                 .zIndex = 30,
                                 .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_TOP,
                                                  .parent = CLAY_ATTACH_POINT_CENTER_BOTTOM},
                                 .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
                                 .attachTo = CLAY_ATTACH_TO_PARENT}}) {
        text(hint, 11, color_text);
      }
    }
  }
}

void TitlePage::input_field(Clay_ElementId id, const std::string& value,
                            std::string_view placeholder, bool focused) {
  const auto font_size = scaled_font_size(15);
  const float width = std::max(80.0F, Clay_GetElementData(id).boundingBox.width - 28);
  if (focused) {
    cursor_ = std::min(cursor_, value.size());
    anchor_ = std::min(anchor_, value.size());
    const float caret_x = app_.text_width(std::string_view(value).substr(0, cursor_), font_size);
    const float max_scroll = std::max(0.0F, app_.text_width(value, font_size) - width + 3);
    input_scroll_ = std::min(input_scroll_, max_scroll);
    input_scroll_ = std::clamp(input_scroll_, std::max(0.0F, caret_x - width + 3), caret_x);
  }
  const auto first = focused ? std::min(cursor_, anchor_) : value.size();
  const auto last = focused ? std::max(cursor_, anchor_) : value.size();
  const bool caret_visible = focused && (SDL_GetTicks() - caret_epoch_) % 1000 < 600;
  auto segment = [&](std::string_view part, bool selected) {
    if (part.empty())
      return;
    const auto retained = retain_frame_text(std::string(part));
    CLAY_AUTO_ID(
        {.layout = {.sizing = {.width = CLAY_SIZING_FIT(0), .height = CLAY_SIZING_FIT(0)}},
         .backgroundColor = selected ? Clay_Color{44, 103, 100, 255} : Clay_Color{0, 0, 0, 0}}) {
      CLAY_TEXT(clay_string(retained), CLAY_TEXT_CONFIG({.textColor = color_text,
                                                         .fontSize = font_size,
                                                         .wrapMode = CLAY_TEXT_WRAP_NONE}));
    }
  };
  auto caret = [&] {
    CLAY_AUTO_ID(
        {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(1.5F),
                               .height = CLAY_SIZING_FIXED(static_cast<float>(font_size) * 1.3F)}},
         .backgroundColor = caret_visible ? color_accent : Clay_Color{0, 0, 0, 0}}) {}
  };
  CLAY(id, {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(48)},
                       .padding = {14, 14, 0, 0},
                       .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
            .backgroundColor = {16, 18, 21, 255},
            .cornerRadius = CLAY_CORNER_RADIUS(10),
            .clip = {.horizontal = true, .childOffset = {focused ? -input_scroll_ : 0, 0}},
            .border = {.color = focused ? color_accent : Clay_Color{120, 125, 133, 255},
                       .width = {1, 1, 1, 1, 0}}}) {
    CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_FIT(0)},
                             .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
      if (value.empty()) {
        if (focused)
          caret();
        CLAY_TEXT(clay_string(placeholder), CLAY_TEXT_CONFIG({.textColor = {137, 144, 155, 255},
                                                              .fontSize = font_size,
                                                              .wrapMode = CLAY_TEXT_WRAP_NONE}));
      } else {
        segment(std::string_view(value).substr(0, first), false);
        if (focused && cursor_ == first)
          caret();
        segment(std::string_view(value).substr(first, last - first), true);
        if (focused && cursor_ != first)
          caret();
        segment(std::string_view(value).substr(last), false);
      }
    }
  }
}

void TitlePage::build(const WindowMetrics& metrics, bool drag_active,
                      const std::optional<ParsedInput>& last_input) {
  rows_.clear();
  const float width =
      std::max(260.0F, std::min(960.0F, static_cast<float>(metrics.logical_width) - 64));
  CLAY(CLAY_ID("TitleRoot"),
       {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)},
                   .padding = {24, 24, 56, 24},
                   .childAlignment = {.x = CLAY_ALIGN_X_CENTER},
                   .layoutDirection = CLAY_TOP_TO_BOTTOM},
        .backgroundColor = color_background}) {
    CLAY(CLAY_ID("LibraryContent"),
         {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(width), .height = CLAY_SIZING_GROW(0)},
                     .childGap = 14,
                     .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
      CLAY(CLAY_ID("SourceToolbar"),
           {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0)}, .childGap = 10}}) {
        icon_button(source_panel_id_, "icons/open-file.svg", "Open file (Ctrl+O)");
        input_field(CLAY_ID("MagnetInput"), magnet_, "magnet url...",
                    input_focused_ && modal_key_.empty());
        icon_button(settings_id_, "icons/gear.svg", "Settings");
      }
      text(retain_frame_text(drag_active ? "Drop torrent here" : status_text(last_input)), 12,
           color_accent);
      if (!input_error_.empty())
        text(input_error_, 12, color_error);
      const auto error = app_.library().error();
      if (!error.empty())
        text(retain_frame_text("Could not save library: " + error), 13, color_error,
             CLAY_TEXT_WRAP_WORDS);
      if (!app_.library_status().empty())
        text(app_.library_status(), 13, color_error);
      CLAY(CLAY_ID("LibraryList"),
           {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)},
                       .childGap = 10,
                       .layoutDirection = CLAY_TOP_TO_BOTTOM},
            .clip = {.vertical = true, .childOffset = Clay_GetScrollOffset()}}) {
        if (app_.library().entries.empty())
          text("Your opened torrents will appear here", 15, color_text);
        uint32_t index = 0;
        for (const auto& entry : app_.library().entries) {
          Row row{entry.key, CLAY_IDI("LibraryName", index), CLAY_IDI("LibraryPlay", index),
                  CLAY_IDI("LibraryMenu", index)};
          rows_.push_back(row);
          CLAY(CLAY_IDI("LibraryRow", index),
               {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0)}, .childGap = 10}}) {
            button(row.name, retain_frame_text(entry.name), true);
            icon_button(row.play, "icons/play.svg",
                        entry.last_file >= 0 ? "Resume last video" : "Play");
            icon_button(row.menu, "icons/more.svg", "Torrent options");
          }
          if (menu_key_ == entry.key) {
            CLAY(CLAY_ID("LibraryActions"),
                 {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(160)},
                             .padding = {8, 8, 8, 8},
                             .childGap = 4,
                             .layoutDirection = CLAY_TOP_TO_BOTTOM},
                  .backgroundColor = {22, 24, 28, 255},
                  .cornerRadius = CLAY_CORNER_RADIUS(12),
                  .floating = {.offset = {0, 6},
                               .parentId = row.menu.id,
                               .zIndex = 40,
                               .attachPoints = {.element = CLAY_ATTACH_POINT_RIGHT_TOP,
                                                .parent = CLAY_ATTACH_POINT_RIGHT_BOTTOM},
                               .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE,
                               .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID,
                               .clipTo = CLAY_CLIP_TO_NONE},
                  .border = {.color = {100, 105, 112, 255}, .width = {1, 1, 1, 1, 0}}}) {
              button(CLAY_ID("LibraryDelete"), "Delete", true, true);
              button(CLAY_ID("LibraryRename"), "Rename", true);
            }
          }
          ++index;
        }
      }
    }
    if (!modal_key_.empty()) {
      CLAY(CLAY_ID("LibraryModal"),
           {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)},
                       .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
            .backgroundColor = {0, 0, 0, 210},
            .floating = {.zIndex = 100,
                         .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE,
                         .attachTo = CLAY_ATTACH_TO_ROOT}}) {
        CLAY(CLAY_ID("LibraryDialog"),
             {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(std::min(width, 480.0F))},
                         .padding = {24, 24, 24, 24},
                         .childGap = 16,
                         .layoutDirection = CLAY_TOP_TO_BOTTOM},
              .backgroundColor = {25, 28, 33, 255},
              .cornerRadius = CLAY_CORNER_RADIUS(14)}) {
          text(deleting_ ? "Delete torrent?" : "Rename torrent", 19,
               deleting_ ? color_error : color_text);
          text(retain_frame_text(deleting_ ? "Remove " + rename_ +
                                                 " and its saved progress from the library?"
                                           : "Enter a new name, then confirm."),
               14, color_text, CLAY_TEXT_WRAP_WORDS);
          if (!deleting_)
            input_field(CLAY_ID("RenameInput"), rename_, "New name...", true);
          CLAY(CLAY_ID("LibraryDialogButtons"), {.layout = {.childGap = 12}}) {
            button(CLAY_ID("LibraryCancel"), "Cancel");
            button(CLAY_ID("LibraryConfirm"), deleting_ ? "Delete" : "Confirm rename", false,
                   deleting_);
          }
        }
      }
    }
  }
}

} // namespace torrview::ui
