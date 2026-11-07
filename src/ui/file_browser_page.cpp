#include "ui/file_browser_page.hpp"

#include "../app.hpp"
#include "ui/render_context.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

namespace torrview::ui {
namespace {

constexpr float numeral_width = 40.0F;
constexpr float size_column_width = 76.0F;

std::string_view file_name(std::string_view path) {
  const std::size_t slash = path.find_last_of("/\\");
  return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

// Folder between the torrent's root folder and the file, if the file is nested deeper.
std::string_view sub_folder(std::string_view path, std::string_view root) {
  const std::size_t slash = path.find_last_of("/\\");
  if (slash == std::string_view::npos) {
    return {};
  }
  std::string_view folder = path.substr(0, slash);
  if (!root.empty() && folder.starts_with(root)) {
    folder.remove_prefix(root.size());
    while (!folder.empty() && (folder.front() == '/' || folder.front() == '\\')) {
      folder.remove_prefix(1);
    }
  }
  return folder;
}

// Episode number from an "S01E07"-style tag, if the name carries one.
std::optional<int> episode_number(std::string_view name) {
  for (std::size_t index = 0; index + 3 < name.size(); ++index) {
    if (std::toupper(static_cast<unsigned char>(name[index])) != 'S' ||
        std::isdigit(static_cast<unsigned char>(name[index + 1])) == 0) {
      continue;
    }
    std::size_t cursor = index + 1;
    while (cursor < name.size() && std::isdigit(static_cast<unsigned char>(name[cursor])) != 0) {
      ++cursor;
    }
    if (cursor + 1 >= name.size() ||
        std::toupper(static_cast<unsigned char>(name[cursor])) != 'E' ||
        std::isdigit(static_cast<unsigned char>(name[cursor + 1])) == 0) {
      continue;
    }
    int value = 0;
    for (++cursor; cursor < name.size() && value < 10000 &&
                   std::isdigit(static_cast<unsigned char>(name[cursor])) != 0;
         ++cursor) {
      value = value * 10 + (name[cursor] - '0');
    }
    return value;
  }
  return std::nullopt;
}

// Use episode tags only when every file has a distinct one; otherwise number by position.
std::vector<int> file_numbers(const std::vector<TorrentFileInfo>& files) {
  std::vector<int> numbers;
  std::set<int> seen;
  for (const TorrentFileInfo& file : files) {
    const std::optional<int> episode = episode_number(file_name(file.path));
    if (!episode.has_value() || !seen.insert(*episode).second) {
      numbers.clear();
      break;
    }
    numbers.push_back(*episode);
  }
  if (numbers.size() != files.size()) {
    numbers.clear();
    for (std::size_t index = 0; index < files.size(); ++index) {
      numbers.push_back(static_cast<int>(index) + 1);
    }
  }
  return numbers;
}

std::string two_digits(int value) {
  return value < 10 ? "0" + std::to_string(value) : std::to_string(value);
}

std::string plural(std::size_t count, const char* one, const char* many) {
  return std::to_string(count) + " " + (count == 1 ? one : many);
}

} // namespace

FileBrowserPage::FileBrowserPage(Application& app, Settings& settings, MpvPlayer* player,
                                 TorrentService& torrent_service)
    : app_(app), settings_(settings), player_(player), torrent_service_(torrent_service),
      ui_(settings) {
  initialize_ids();
}

void FileBrowserPage::open() { selected_file_index_ = -1; }

void FileBrowserPage::draw(RenderContext& render_context) {
  begin_frame();
  build(render_context.metrics, app_.browsing_snapshot(),
        selected_file_index_ < 0 ? std::nullopt : std::optional<int>(selected_file_index_));
}

bool FileBrowserPage::handle_event(const SDL_Event& event) {
  if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_BACKSPACE) {
    app_.navigate<TitlePage>();
    return true;
  }
  if (event.type != SDL_EVENT_MOUSE_BUTTON_UP || event.button.button != SDL_BUTTON_LEFT) {
    return false;
  }

  Clay_SetPointerState({.x = event.button.x, .y = event.button.y}, false);
  switch (hit_test_click()) {
  case FileBrowserAction::open_source:
    app_.navigate<TitlePage>();
    break;
  case FileBrowserAction::open_settings:
    app_.navigate<SettingsPage>(settings_.values());
    break;
  case FileBrowserAction::select_file:
    app_.play_torrent_file(selected_file_index_);
    break;
  case FileBrowserAction::none:
    break;
  }
  return true;
}

void FileBrowserPage::close() {}

void FileBrowserPage::initialize_ids() {
  open_source_id_ = CLAY_ID("FileBrowserOpenSource");
  settings_id_ = CLAY_ID("FileBrowserSettings");
  file_list_id_ = CLAY_ID("FileBrowserFileList");
}

void FileBrowserPage::begin_frame() {
  ui_.begin_frame();
  row_ids_.clear();
  row_file_indices_.clear();
}

void FileBrowserPage::header(const TorrentSnapshot& snapshot, float column_width) {
  const bool loading = snapshot.state == TorrentLoadState::loading_metadata;
  const bool failed =
      snapshot.state == TorrentLoadState::error || snapshot.state == TorrentLoadState::unavailable;
  const std::string title = snapshot.name.empty() ? "Untitled torrent" : snapshot.name;

  std::int64_t total_size = 0;
  for (const TorrentFileInfo& file : snapshot.video_files) {
    total_size += file.size;
  }

  CLAY(CLAY_ID("FileBrowserHeader"),
       {.layout = {
            .sizing = {.width = CLAY_SIZING_FIXED(column_width), .height = CLAY_SIZING_FIT(0.0F)},
            .padding = {0, 0, 20, 22},
            .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
    CLAY(CLAY_ID("FileBrowserTopRow"),
         {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F), .height = CLAY_SIZING_FIT(0.0F)},
                     .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
      CLAY(open_source_id_,
           {.layout = {.sizing = {.width = CLAY_SIZING_FIT(0.0F),
                                  .height = CLAY_SIZING_FIXED(32.0F)},
                       .padding = {4, 12, 0, 0},
                       .childGap = 4,
                       .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
            .backgroundColor = Clay_PointerOver(open_source_id_) ? theme::raised : theme::clear,
            .cornerRadius = CLAY_CORNER_RADIUS(theme::radius_control)}) {
        ui_.icon("icons/back.svg", 18.0F);
        ui_.text("Library", theme::size_body, theme::ash);
      }
      ui_.spacer();
      ui_.icon_button(settings_id_, "icons/gear.svg");
    }

    ui_.gap(1.0F, 22.0F);
    ui_.text(ui_.keep(truncate_middle(title, ui_.chars_for_width(column_width, theme::size_title))),
             theme::size_title, theme::lamp, theme::font_semibold);
    ui_.gap(1.0F, 8.0F);

    CLAY(CLAY_ID("FileBrowserMeta"),
         {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F), .height = CLAY_SIZING_FIT(0.0F)},
                     .childGap = 20,
                     .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
      if (failed || !snapshot.error.empty()) {
        const std::string message = !snapshot.error.empty() ? snapshot.error : snapshot.status;
        ui_.text(ui_.keep(message.empty() ? "This torrent could not be loaded" : message),
                 theme::size_body, theme::fault);
      } else if (loading) {
        const int percent =
            static_cast<int>(std::lround(std::clamp(snapshot.metadata_progress, 0.0F, 1.0F) * 100));
        ui_.text(ui_.keep("Fetching the file list from peers, " + std::to_string(percent) + "%"),
                 theme::size_body, theme::signal);
      } else {
        std::string summary = plural(snapshot.video_files.size(), "video", "videos");
        if (total_size > 0) {
          summary += ", " + format_size(total_size);
        }
        ui_.text(ui_.keep(summary), theme::size_body, theme::ash);
      }
      if (snapshot.state != TorrentLoadState::idle) {
        ui_.text(
            ui_.keep(
                plural(static_cast<std::size_t>(std::max(0, snapshot.peers)), "peer", "peers") +
                ", " +
                plural(static_cast<std::size_t>(std::max(0, snapshot.seeds)), "seed", "seeds")),
            theme::size_body, theme::smoke);
      }
    }

    if (loading) {
      const float progress = std::clamp(snapshot.metadata_progress, 0.0F, 1.0F);
      ui_.gap(1.0F, 12.0F);
      CLAY(CLAY_ID("FileBrowserMetadataProgress"),
           {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                  .height = CLAY_SIZING_FIXED(2.0F)}},
            .backgroundColor = theme::rule}) {
        CLAY(CLAY_ID("FileBrowserMetadataProgressFill"),
             {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(column_width * progress),
                                    .height = CLAY_SIZING_GROW(0.0F)}},
              .backgroundColor = theme::signal}) {}
      }
    }
  }
}

void FileBrowserPage::file_row(std::size_t position, const TorrentFileInfo& file, int number,
                               std::string_view root, bool selected, float column_width) {
  const Clay_ElementId row_id = Clay_GetElementIdWithIndex(CLAY_STRING("FileBrowserFileRow"),
                                                           static_cast<uint32_t>(position));
  row_ids_.push_back(row_id);
  row_file_indices_.push_back(file.index);

  const bool hovered = Clay_PointerOver(row_id);
  const float text_width = column_width - numeral_width - size_column_width - 72.0F;
  const std::string name =
      truncate_middle(file_name(file.path), ui_.chars_for_width(text_width, theme::size_lead));
  const std::string_view folder = sub_folder(file.path, root);
  const VideoProgress progress = app_.file_progress(file.index);
  const double fraction =
      progress.duration > 0.0 ? std::clamp(progress.position / progress.duration, 0.0, 1.0) : 0.0;

  CLAY(row_id,
       {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F), .height = CLAY_SIZING_FIT(0.0F)},
                   .padding = {12, 16, 14, 14},
                   .childGap = 20,
                   .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
        .backgroundColor = selected ? theme::lifted : (hovered ? theme::raised : theme::clear),
        .cornerRadius = CLAY_CORNER_RADIUS(theme::radius_control)}) {
    CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(numeral_width),
                                        .height = CLAY_SIZING_FIT(0.0F)}}}) {
      ui_.text(ui_.keep(two_digits(number)), theme::size_numeral,
               hovered || selected ? theme::lamp : theme::smoke, theme::font_semibold);
    }
    CLAY_AUTO_ID(
        {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F), .height = CLAY_SIZING_FIT(0.0F)},
                    .childGap = 4,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
      ui_.text(ui_.keep(name), theme::size_lead, theme::lamp);
      if (!folder.empty()) {
        ui_.text(
            ui_.keep(truncate_middle(folder, ui_.chars_for_width(text_width, theme::size_caption))),
            theme::size_caption, theme::smoke);
      }
      if (progress.position > 0.0) {
        const bool watched = fraction >= 0.95;
        CLAY_AUTO_ID(
            {.layout = {.sizing = {.width = CLAY_SIZING_FIT(0.0F), .height = CLAY_SIZING_FIT(0.0F)},
                        .childGap = 10,
                        .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
          if (fraction > 0.0) {
            CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(64.0F),
                                                .height = CLAY_SIZING_FIXED(2.0F)}},
                          .backgroundColor = theme::rule}) {
              CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(
                                                      64.0F * static_cast<float>(fraction)),
                                                  .height = CLAY_SIZING_GROW(0.0F)}},
                            .backgroundColor = theme::ash}) {}
            }
          }
          ui_.text(ui_.keep(watched ? std::string("Watched")
                                    : "Resume at " + format_clock(progress.position)),
                   theme::size_caption, theme::ash);
        }
      }
    }
    CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(size_column_width),
                                        .height = CLAY_SIZING_FIT(0.0F)},
                             .childAlignment = {.x = CLAY_ALIGN_X_RIGHT}}}) {
      ui_.text(ui_.keep(format_size(file.size)), theme::size_caption, theme::ash);
    }
  }
}

void FileBrowserPage::empty_state(const TorrentSnapshot& snapshot) {
  const bool loading = snapshot.state == TorrentLoadState::loading_metadata;
  CLAY(CLAY_ID("FileBrowserEmpty"),
       {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F), .height = CLAY_SIZING_FIT(0.0F)},
                   .padding = {0, 0, 28, 0},
                   .childGap = 6,
                   .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
    if (loading) {
      ui_.text("Waiting for the file list", theme::size_lead, theme::lamp);
      ui_.text("Peers send it before any video. This usually takes a few seconds.",
               theme::size_body, theme::ash, theme::font_regular, CLAY_TEXT_WRAP_WORDS);
    } else {
      ui_.text("No videos in this torrent", theme::size_lead, theme::lamp);
      ui_.text(ui_.keep("It contains " + plural(snapshot.files.size(), "file", "files") +
                        ", none of them in a format Torrview plays."),
               theme::size_body, theme::ash, theme::font_regular, CLAY_TEXT_WRAP_WORDS);
    }
  }
}

void FileBrowserPage::build(const WindowMetrics& metrics, const TorrentSnapshot& snapshot,
                            std::optional<int> selected_file_index) {
  const float width = static_cast<float>(metrics.logical_width);
  const float column_width = std::clamp(width - 96.0F, 300.0F, 1040.0F);
  const std::vector<TorrentFileInfo>& files = snapshot.video_files;
  const std::vector<int> numbers = file_numbers(files);
  const std::string_view root = snapshot.name;

  CLAY(CLAY_ID("FileBrowserRoot"),
       {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F), .height = CLAY_SIZING_GROW(0.0F)},
                   .padding = {0, 0, 24, 0},
                   .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_TOP},
                   .layoutDirection = CLAY_TOP_TO_BOTTOM},
        .backgroundColor = theme::graphite}) {
    header(snapshot, column_width);
    CLAY(CLAY_ID("FileBrowserHeaderRule"),
         {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(column_width),
                                .height = CLAY_SIZING_FIXED(1.0F)}},
          .backgroundColor = theme::rule}) {}

    // The scroll container spans the window so the wheel works anywhere below the header.
    // Rows inside must not clip (see Widgets).
    CLAY(file_list_id_,
         {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F), .height = CLAY_SIZING_GROW(0.0F)},
                     .padding = {0, 0, 8, 40},
                     .childAlignment = {.x = CLAY_ALIGN_X_CENTER},
                     .layoutDirection = CLAY_TOP_TO_BOTTOM},
          .clip = {.vertical = true, .childOffset = Clay_GetScrollOffset()}}) {
      CLAY(CLAY_ID("FileBrowserColumn"),
           {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(column_width),
                                  .height = CLAY_SIZING_FIT(0.0F)},
                       .childGap = 2,
                       .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
        if (files.empty()) {
          empty_state(snapshot);
        }
        for (std::size_t index = 0; index < files.size(); ++index) {
          file_row(index, files[index], numbers[index], root,
                   selected_file_index.has_value() && *selected_file_index == files[index].index,
                   column_width);
        }
      }
    }
    ui_.scrollbar(file_list_id_);
  }
}

FileBrowserAction FileBrowserPage::hit_test_click() {
  if (Clay_PointerOver(open_source_id_)) {
    return FileBrowserAction::open_source;
  }

  if (Clay_PointerOver(settings_id_)) {
    return FileBrowserAction::open_settings;
  }

  for (std::size_t index = 0; index < row_ids_.size(); ++index) {
    if (Clay_PointerOver(row_ids_[index])) {
      selected_file_index_ = row_file_indices_[index];
      return FileBrowserAction::select_file;
    }
  }

  return FileBrowserAction::none;
}

int FileBrowserPage::selected_file_index() const { return selected_file_index_; }

} // namespace torrview::ui
