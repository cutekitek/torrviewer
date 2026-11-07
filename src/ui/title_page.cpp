#include "ui/title_page.hpp"

#include "../app.hpp"
#include "ui/render_context.hpp"

#include <algorithm>
#include <cmath>

namespace torrview::ui {
namespace {

constexpr float input_padding = 16.0F;
constexpr float source_row_height = 48.0F;

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

std::string_view file_name(std::string_view path) {
  const std::size_t slash = path.find_last_of("/\\");
  return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

std::string entry_summary(const LibraryEntry& entry) {
  if (entry.files.empty()) {
    return "File list not downloaded yet";
  }
  std::size_t videos = 0;
  for (const TorrentFileInfo& file : entry.files) {
    videos += file.likely_video ? 1U : 0U;
  }
  return std::to_string(videos) + (videos == 1 ? " video" : " videos");
}

std::string entry_position(const LibraryEntry& entry) {
  if (entry.last_file < 0) {
    return {};
  }
  const auto progress = entry.progress.find(entry.last_file);
  if (progress == entry.progress.end() || progress->second.position <= 0.0) {
    return {};
  }
  std::string_view name;
  for (const TorrentFileInfo& file : entry.files) {
    if (file.index == entry.last_file) {
      name = file_name(file.path);
    }
  }
  std::string result = "Stopped at " + format_clock(progress->second.position);
  if (!name.empty()) {
    result += " in ";
    result += truncate_middle(name, 48);
  }
  return result;
}

} // namespace

TitlePage::TitlePage(Application& app, Settings& settings, const bool& drag_active,
                     const std::optional<ParsedInput>& last_input)
    : app_(app), settings_(settings), ui_(settings), drag_active_(drag_active),
      last_input_(last_input) {
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
  build(render_context.metrics, drag_active_);
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
  const uint16_t font_size = ui_.size(theme::size_lead);
  const float local = std::max(0.0F, pointer_x - Clay_GetElementData(id).boundingBox.x -
                                         input_padding + input_scroll_);
  std::vector<std::size_t> boundaries{0};
  for (std::size_t at = 0; at < value.size();) {
    at = next_character(value, at);
    boundaries.push_back(at);
  }
  auto found = std::lower_bound(
      boundaries.begin(), boundaries.end(), local, [&](std::size_t offset, float x) {
        return app_.text_width(std::string_view(value).substr(0, offset), font_size) < x;
      });
  cursor_ = found == boundaries.end() ? value.size() : *found;
  if (found != boundaries.begin() && found != boundaries.end()) {
    const auto before = *(found - 1);
    const float midpoint = (app_.text_width(std::string_view(value).substr(0, before), font_size) +
                            app_.text_width(std::string_view(value).substr(0, *found), font_size)) /
                           2;
    if (local < midpoint)
      cursor_ = before;
  }
  anchor_ = cursor_;
  caret_epoch_ = SDL_GetTicks();
}

void TitlePage::submit_magnet() {
  if (!is_magnet_link(magnet_)) {
    input_error_ = "That is not a magnet link. Magnet links start with magnet:?";
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
  if (input && edit_input(*input, event)) {
    if (input == &magnet_ && event.type == SDL_EVENT_TEXT_INPUT)
      input_error_.clear();
    return true;
  }
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

void TitlePage::begin_frame() { ui_.begin_frame(); }

void TitlePage::input_field(Clay_ElementId id, const std::string& value,
                            std::string_view placeholder, bool focused) {
  const auto font_size = ui_.size(theme::size_lead);
  const float width =
      std::max(80.0F, Clay_GetElementData(id).boundingBox.width - input_padding * 2.0F);
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
  const auto line_height = static_cast<uint16_t>(ui_.line(theme::size_lead));
  auto segment = [&](std::string_view part, bool selected) {
    if (part.empty())
      return;
    const auto retained = ui_.keep(std::string(part));
    CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_FIT(0), .height = CLAY_SIZING_FIT(0)}},
                  .backgroundColor = selected ? Clay_Color{92, 86, 76, 255} : theme::clear}) {
      CLAY_TEXT(clay_string(retained), CLAY_TEXT_CONFIG({.textColor = theme::lamp,
                                                         .fontSize = font_size,
                                                         .lineHeight = line_height,
                                                         .wrapMode = CLAY_TEXT_WRAP_NONE}));
    }
  };
  auto caret = [&] {
    CLAY_AUTO_ID(
        {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(1.5F),
                               .height = CLAY_SIZING_FIXED(static_cast<float>(line_height))}},
         .backgroundColor = caret_visible ? theme::lamp : theme::clear}) {}
  };
  const bool hovered = Clay_PointerOver(id);
  CLAY(id, {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0),
                                  .height = CLAY_SIZING_FIXED(source_row_height)},
                       .padding = {static_cast<uint16_t>(input_padding),
                                   static_cast<uint16_t>(input_padding), 0, 0},
                       .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
            .backgroundColor = focused || hovered ? theme::lifted : theme::raised,
            .cornerRadius = CLAY_CORNER_RADIUS(theme::radius_control),
            .clip = {.horizontal = true, .childOffset = {focused ? -input_scroll_ : 0, 0}},
            .border = {.color = focused ? theme::lamp : theme::clear, .width = {1, 1, 1, 1, 0}}}) {
    CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_FIT(0)},
                             .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
      if (value.empty()) {
        if (focused)
          caret();
        CLAY_TEXT(clay_string(placeholder), CLAY_TEXT_CONFIG({.textColor = theme::smoke,
                                                              .fontSize = font_size,
                                                              .lineHeight = line_height,
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

void TitlePage::library_row(const LibraryEntry& entry, uint32_t index, float column_width) {
  const Row row{entry.key, CLAY_IDI("LibraryName", index), CLAY_IDI("LibraryPlay", index),
                CLAY_IDI("LibraryMenu", index)};
  rows_.push_back(row);
  const bool menu_open = menu_key_ == entry.key;
  const bool hovered = Clay_PointerOver(CLAY_IDI("LibraryRow", index)) && modal_key_.empty();
  const std::string position = entry_position(entry);
  const std::size_t name_chars = ui_.chars_for_width(column_width - 200.0F, theme::size_lead);

  CLAY(CLAY_IDI("LibraryRow", index),
       {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIT(0)},
                   .padding = {0, 8, 0, 0},
                   .childGap = 8,
                   .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
        .backgroundColor = hovered || menu_open ? theme::raised : theme::clear,
        .cornerRadius = CLAY_CORNER_RADIUS(theme::radius_control)}) {
    CLAY(row.name,
         {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIT(0)},
                     .padding = {14, 8, 14, 14},
                     .childGap = 4,
                     .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
      ui_.text(ui_.keep(truncate_middle(entry.name, name_chars)), theme::size_lead, theme::lamp,
               theme::font_semibold);
      ui_.text(ui_.keep(position.empty() ? entry_summary(entry)
                                         : entry_summary(entry) + ". " + position),
               theme::size_caption, theme::ash);
    }
    ui_.button(row.play, entry.last_file >= 0 ? "Resume" : "Play", ButtonKind::secondary);
    ui_.icon_button(row.menu, "icons/more.svg", menu_open, 34.0F, 18.0F);
  }
  if (menu_open) {
    row_menu(row);
  }
}

void TitlePage::row_menu(const Row& row) {
  CLAY(CLAY_ID("LibraryActions"),
       {.layout = {.sizing = {.width = CLAY_SIZING_FIT(200)},
                   .padding = {4, 4, 4, 4},
                   .childGap = 2,
                   .layoutDirection = CLAY_TOP_TO_BOTTOM},
        .backgroundColor = theme::lifted,
        .cornerRadius = CLAY_CORNER_RADIUS(theme::radius_menu),
        .floating = {.offset = {0, 6},
                     .parentId = row.menu.id,
                     .zIndex = 40,
                     .attachPoints = {.element = CLAY_ATTACH_POINT_RIGHT_TOP,
                                      .parent = CLAY_ATTACH_POINT_RIGHT_BOTTOM},
                     .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE,
                     .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID,
                     .clipTo = CLAY_CLIP_TO_NONE}}) {
    const auto item = [this](Clay_ElementId id, std::string_view label, Clay_Color ink) {
      CLAY(id, {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0),
                                      .height = CLAY_SIZING_FIXED(ui_.line(theme::size_body) + 14)},
                           .padding = {12, 16, 0, 0},
                           .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
                .backgroundColor = Clay_PointerOver(id) ? theme::rule : theme::clear,
                .cornerRadius = CLAY_CORNER_RADIUS(theme::radius_control)}) {
        ui_.text(label, theme::size_body, ink);
      }
    };
    item(CLAY_ID("LibraryRename"), "Rename", theme::lamp);
    item(CLAY_ID("LibraryDelete"), "Remove from library", theme::fault);
  }
}

void TitlePage::modal(float column_width) {
  const std::string name = [&] {
    for (const auto& entry : app_.library().entries)
      if (entry.key == modal_key_)
        return entry.name;
    return std::string();
  }();
  CLAY(CLAY_ID("LibraryModal"),
       {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)},
                   .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
        .backgroundColor = theme::veil,
        .floating = {.zIndex = 100,
                     .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE,
                     .attachTo = CLAY_ATTACH_TO_ROOT}}) {
    CLAY(CLAY_ID("LibraryDialog"),
         {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(std::min(column_width, 460.0F))},
                     .padding = {24, 24, 22, 20},
                     .childGap = 12,
                     .layoutDirection = CLAY_TOP_TO_BOTTOM},
          .backgroundColor = theme::raised,
          .cornerRadius = CLAY_CORNER_RADIUS(theme::radius_menu),
          .border = {.color = theme::rule, .width = {1, 1, 1, 1, 0}}}) {
      ui_.text(deleting_ ? "Remove from library?" : "Rename", 18, theme::lamp,
               theme::font_semibold);
      if (deleting_) {
        ui_.text(ui_.keep(truncate_middle(name, 60)), theme::size_body, theme::lamp,
                 theme::font_regular, CLAY_TEXT_WRAP_WORDS);
        ui_.text("Its saved playback positions are removed too. You can open the torrent again "
                 "at any time.",
                 theme::size_body, theme::ash, theme::font_regular, CLAY_TEXT_WRAP_WORDS);
      } else {
        input_field(CLAY_ID("RenameInput"), rename_, "Name", true);
      }
      ui_.gap(1.0F, 4.0F);
      CLAY(CLAY_ID("LibraryDialogButtons"),
           {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0)}, .childGap = 8}}) {
        ui_.spacer();
        ui_.button(CLAY_ID("LibraryCancel"), "Cancel", ButtonKind::quiet);
        ui_.button(CLAY_ID("LibraryConfirm"), deleting_ ? "Remove" : "Rename",
                   deleting_ ? ButtonKind::danger : ButtonKind::primary);
      }
    }
  }
}

void TitlePage::build(const WindowMetrics& metrics, bool drag_active) {
  rows_.clear();
  const float column_width =
      std::clamp(static_cast<float>(metrics.logical_width) - 96.0F, 280.0F, 880.0F);
  const auto& entries = app_.library().entries;
  const bool magnet_focused = input_focused_ && modal_key_.empty();

  CLAY(CLAY_ID("TitleRoot"),
       {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)},
                   .padding = {0, 0, 24, 0},
                   .childAlignment = {.x = CLAY_ALIGN_X_CENTER},
                   .layoutDirection = CLAY_TOP_TO_BOTTOM},
        .backgroundColor = theme::graphite}) {
    CLAY(CLAY_ID("TitleHeader"), {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(column_width)},
                                             .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
      CLAY(CLAY_ID("TitleTopRow"), {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0)},
                                               .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
        ui_.text("Torrview", theme::size_lead, theme::lamp, theme::font_semibold);
        ui_.spacer();
        ui_.icon_button(settings_id_, "icons/gear.svg");
      }
      ui_.gap(1.0F, 56.0F);
      ui_.text("Watch a torrent while it downloads", theme::size_title, theme::lamp,
               theme::font_semibold);
      ui_.gap(1.0F, 20.0F);
      CLAY(CLAY_ID("SourceToolbar"),
           {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0)}, .childGap = 8}}) {
        input_field(CLAY_ID("MagnetInput"), magnet_, "Paste a magnet link", magnet_focused);
        CLAY(source_panel_id_,
             {.layout = {.sizing = {.width = CLAY_SIZING_FIT(0),
                                    .height = CLAY_SIZING_FIXED(source_row_height)},
                         .padding = {18, 18, 0, 0},
                         .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
              .backgroundColor =
                  Clay_PointerOver(source_panel_id_) ? Clay_Color{255, 251, 243, 255} : theme::lamp,
              .cornerRadius = CLAY_CORNER_RADIUS(theme::radius_control)}) {
          ui_.text("Open file", theme::size_body, theme::graphite, theme::font_semibold);
        }
      }
      ui_.gap(1.0F, 10.0F);
      if (!input_error_.empty()) {
        ui_.text(input_error_, theme::size_caption, theme::fault);
      } else if (drag_active) {
        ui_.text("Release to open", theme::size_caption, theme::lamp);
      } else {
        ui_.text(magnet_focused ? "Press Enter to open the link"
                                : "Or drop a .torrent or video file onto this window",
                 theme::size_caption, theme::smoke);
      }
      const auto error = app_.library().error();
      if (!error.empty()) {
        ui_.gap(1.0F, 6.0F);
        ui_.text(ui_.keep("The library could not be saved: " + error), theme::size_caption,
                 theme::fault, theme::font_regular, CLAY_TEXT_WRAP_WORDS);
      }
      if (!app_.library_status().empty()) {
        ui_.gap(1.0F, 6.0F);
        ui_.text(app_.library_status(), theme::size_caption, theme::fault);
      }
      ui_.gap(1.0F, 48.0F);
      CLAY(CLAY_ID("LibraryHeading"), {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0)},
                                                  .padding = {0, 0, 0, 10},
                                                  .childGap = 10,
                                                  .childAlignment = {.y = CLAY_ALIGN_Y_BOTTOM}}}) {
        ui_.text("Library", theme::size_body, theme::ash, theme::font_semibold);
        if (!entries.empty()) {
          ui_.text(ui_.keep(std::to_string(entries.size())), theme::size_body, theme::smoke);
        }
      }
      ui_.rule();
    }

    CLAY(CLAY_ID("LibraryList"),
         {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)},
                     .padding = {0, 0, 6, 32},
                     .childAlignment = {.x = CLAY_ALIGN_X_CENTER},
                     .layoutDirection = CLAY_TOP_TO_BOTTOM},
          .clip = {.vertical = true, .childOffset = Clay_GetScrollOffset()}}) {
      CLAY(CLAY_ID("LibraryColumn"),
           {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(column_width)},
                       .childGap = 2,
                       .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
        if (entries.empty()) {
          CLAY(CLAY_ID("LibraryEmpty"), {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0)},
                                                    .padding = {0, 0, 18, 0},
                                                    .childGap = 4,
                                                    .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
            ui_.text("Nothing here yet", theme::size_body, theme::lamp);
            ui_.text("Torrents you open are kept here with the place you stopped watching.",
                     theme::size_body, theme::ash, theme::font_regular, CLAY_TEXT_WRAP_WORDS);
          }
        }
        uint32_t index = 0;
        for (const auto& entry : entries) {
          library_row(entry, index, column_width);
          ++index;
        }
      }
    }
    ui_.scrollbar(CLAY_ID("LibraryList"));
    if (!modal_key_.empty()) {
      modal(column_width);
    }
  }
}

} // namespace torrview::ui
