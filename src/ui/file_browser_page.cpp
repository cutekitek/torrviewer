#include "ui/file_browser_page.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace torrview::ui {
namespace {

constexpr Clay_Color color_background = {12.0F, 13.0F, 15.0F, 255.0F};
constexpr Clay_Color color_panel = {22.0F, 25.0F, 29.0F, 255.0F};
constexpr Clay_Color color_row = {31.0F, 35.0F, 41.0F, 255.0F};
constexpr Clay_Color color_row_selected = {58.0F, 125.0F, 114.0F, 255.0F};
constexpr Clay_Color color_text = {235.0F, 238.0F, 243.0F, 255.0F};
constexpr Clay_Color color_muted = {166.0F, 174.0F, 186.0F, 255.0F};
constexpr Clay_Color color_accent = {74.0F, 181.0F, 162.0F, 255.0F};
constexpr Clay_Color color_error = {214.0F, 94.0F, 82.0F, 255.0F};
constexpr Clay_Color color_rail = {67.0F, 73.0F, 83.0F, 255.0F};

Clay_String clay_string(std::string_view value) {
  const std::size_t max_length = static_cast<std::size_t>(std::numeric_limits<int32_t>::max());
  const std::size_t length = std::min(value.size(), max_length);
  return {.isStaticallyAllocated = false,
          .length = static_cast<int32_t>(length),
          .chars = value.data()};
}

std::string format_bytes(std::int64_t bytes) {
  constexpr double kib = 1024.0;
  constexpr double mib = kib * 1024.0;
  constexpr double gib = mib * 1024.0;
  const double value = static_cast<double>(std::max<std::int64_t>(0, bytes));

  std::ostringstream out;
  out << std::fixed << std::setprecision(value >= gib ? 2 : 1);
  if (value >= gib) {
    out << value / gib << " GiB";
  } else if (value >= mib) {
    out << value / mib << " MiB";
  } else if (value >= kib) {
    out << value / kib << " KiB";
  } else {
    out.str("");
    out << static_cast<std::int64_t>(value) << " B";
  }
  return out.str();
}

std::string compact_middle(std::string_view value, std::size_t max_length) {
  if (value.size() <= max_length) {
    return std::string(value);
  }
  if (max_length <= 5) {
    return std::string(value.substr(0, max_length));
  }

  const std::size_t left = (max_length - 3) / 2;
  const std::size_t right = max_length - 3 - left;
  std::string result(value.substr(0, left));
  result += "...";
  result += value.substr(value.size() - right);
  return result;
}

std::string source_label(const TorrentSnapshot& snapshot) {
  if (snapshot.source.empty()) {
    return "No source";
  }
  return compact_middle(snapshot.source, 86);
}

std::string status_label(const TorrentSnapshot& snapshot) {
  if (!snapshot.error.empty()) {
    return snapshot.error;
  }
  if (!snapshot.status.empty()) {
    return snapshot.status;
  }
  return torrent_state_label(snapshot.state);
}

} // namespace

void FileBrowserPage::initialize_ids() {
  open_source_id_ = Clay_GetElementId(CLAY_STRING("FileBrowserOpenSource"));
  file_list_id_ = Clay_GetElementId(CLAY_STRING("FileBrowserFileList"));
}

void FileBrowserPage::begin_frame() {
  frame_strings_.clear();
  row_ids_.clear();
  row_file_indices_.clear();
}

std::string_view FileBrowserPage::retain_frame_text(std::string value) {
  frame_strings_.push_back(std::move(value));
  return frame_strings_.back();
}

void FileBrowserPage::text(std::string_view value, uint16_t font_size, Clay_Color color,
                           Clay_TextElementConfigWrapMode wrap) {
  CLAY_TEXT(clay_string(value), CLAY_TEXT_CONFIG({.textColor = color,
                                                  .fontSize = font_size,
                                                  .lineHeight = static_cast<uint16_t>(std::lround(
                                                      static_cast<float>(font_size) * 1.25F)),
                                                  .wrapMode = wrap}));
}

void FileBrowserPage::file_row(Clay_ElementId id, const TorrentFileInfo& file, bool selected) {
  const Clay_Color background = selected ? color_row_selected : color_row;
  const std::string path = compact_middle(file.path, 78);
  const std::string details =
      format_bytes(file.size) + "  " + (file.extension.empty() ? "file" : file.extension);

  CLAY(id,
       {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F), .height = CLAY_SIZING_FIXED(58.0F)},
                   .padding = {14, 14, 8, 8},
                   .childGap = 4,
                   .layoutDirection = CLAY_TOP_TO_BOTTOM},
        .backgroundColor = background,
        .cornerRadius = CLAY_CORNER_RADIUS(6.0F)}) {
    text(retain_frame_text(path), 13, color_text, CLAY_TEXT_WRAP_NONE);
    text(retain_frame_text(details), 11, selected ? color_text : color_muted, CLAY_TEXT_WRAP_NONE);
  }
}

void FileBrowserPage::build(const WindowMetrics& metrics, const TorrentSnapshot& snapshot,
                            std::optional<int> selected_file_index) {
  const float width = static_cast<float>(metrics.logical_width);
  const float height = static_cast<float>(metrics.logical_height);
  const float content_width = std::clamp(width - 64.0F, 520.0F, 980.0F);
  const float list_height = std::max(180.0F, height - 270.0F);
  const std::vector<TorrentFileInfo>& files =
      snapshot.video_files.empty() ? snapshot.files : snapshot.video_files;
  const bool loading = snapshot.state == TorrentLoadState::loading_metadata;
  const bool error =
      snapshot.state == TorrentLoadState::error || snapshot.state == TorrentLoadState::unavailable;
  const float progress = std::clamp(snapshot.metadata_progress, 0.0F, 1.0F);
  const std::string title = snapshot.name.empty() ? "Torrent source" : snapshot.name;
  const std::string counts = std::to_string(snapshot.video_files.size()) + " video files, " +
                             std::to_string(snapshot.files.size()) + " total";
  const std::string swarm =
      "Peers " + std::to_string(snapshot.peers) + "  Seeds " + std::to_string(snapshot.seeds);

  CLAY(CLAY_ID("FileBrowserRoot"),
       {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F), .height = CLAY_SIZING_GROW(0.0F)},
                   .padding = {32, 32, 28, 28},
                   .childGap = 18,
                   .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_TOP},
                   .layoutDirection = CLAY_TOP_TO_BOTTOM},
        .backgroundColor = color_background}) {
    CLAY(CLAY_ID("FileBrowserHeader"),
         {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(content_width),
                                .height = CLAY_SIZING_FIXED(112.0F)},
                     .padding = {18, 18, 14, 14},
                     .childGap = 8,
                     .layoutDirection = CLAY_TOP_TO_BOTTOM},
          .backgroundColor = color_panel,
          .cornerRadius = CLAY_CORNER_RADIUS(8.0F)}) {
      CLAY(CLAY_ID("FileBrowserTopRow"),
           {.layout = {
                .sizing = {.width = CLAY_SIZING_GROW(0.0F), .height = CLAY_SIZING_FIXED(32.0F)},
                .childGap = 12,
                .childAlignment = {.x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER}}}) {
        text(retain_frame_text(compact_middle(title, 64)), 17, color_text, CLAY_TEXT_WRAP_NONE);
        CLAY(CLAY_ID("FileBrowserTopSpacer"),
             {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                    .height = CLAY_SIZING_FIXED(1.0F)}}}) {}
        CLAY(open_source_id_,
             {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(148.0F),
                                    .height = CLAY_SIZING_FIXED(32.0F)},
                         .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = color_row,
              .cornerRadius = CLAY_CORNER_RADIUS(6.0F)}) {
          text("Open source", 12, color_text, CLAY_TEXT_WRAP_NONE);
        }
      }
      text(retain_frame_text(source_label(snapshot)), 12, color_muted, CLAY_TEXT_WRAP_NONE);
      CLAY(CLAY_ID("FileBrowserStatusRow"),
           {.layout = {
                .sizing = {.width = CLAY_SIZING_GROW(0.0F), .height = CLAY_SIZING_FIXED(22.0F)},
                .childGap = 14,
                .childAlignment = {.x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER}}}) {
        text(retain_frame_text(status_label(snapshot)), 12, error ? color_error : color_accent,
             CLAY_TEXT_WRAP_NONE);
        text(retain_frame_text(counts), 12, color_muted, CLAY_TEXT_WRAP_NONE);
        text(retain_frame_text(swarm), 12, color_muted, CLAY_TEXT_WRAP_NONE);
      }
      if (loading) {
        CLAY(CLAY_ID("FileBrowserMetadataProgress"),
             {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                    .height = CLAY_SIZING_FIXED(6.0F)}},
              .backgroundColor = color_rail,
              .cornerRadius = CLAY_CORNER_RADIUS(3.0F)}) {
          CLAY(CLAY_ID("FileBrowserMetadataProgressFill"),
               {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(
                                          std::max(8.0F, (content_width - 36.0F) * progress)),
                                      .height = CLAY_SIZING_GROW(0.0F)}},
                .backgroundColor = color_accent,
                .cornerRadius = CLAY_CORNER_RADIUS(3.0F)}) {}
        }
      }
    }

    CLAY(file_list_id_, {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(content_width),
                                               .height = CLAY_SIZING_FIXED(list_height)},
                                    .padding = {8, 8, 8, 8},
                                    .childGap = 8,
                                    .layoutDirection = CLAY_TOP_TO_BOTTOM},
                         .backgroundColor = color_background,
                         .clip = {.vertical = true, .childOffset = Clay_GetScrollOffset()}}) {
      if (files.empty()) {
        CLAY(CLAY_ID("FileBrowserEmpty"),
             {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                    .height = CLAY_SIZING_FIXED(72.0F)},
                         .padding = {14, 14, 0, 0},
                         .childAlignment = {.x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = color_row,
              .cornerRadius = CLAY_CORNER_RADIUS(6.0F)}) {
          const char* empty_text = loading ? "Waiting for torrent metadata"
                                           : "No video files were detected in this source";
          text(empty_text, 13, color_muted, CLAY_TEXT_WRAP_NONE);
        }
      } else {
        for (std::size_t index = 0; index < files.size(); ++index) {
          const Clay_ElementId row_id = Clay_GetElementIdWithIndex(
              CLAY_STRING("FileBrowserFileRow"), static_cast<uint32_t>(index));
          row_ids_.push_back(row_id);
          row_file_indices_.push_back(files[index].index);
          file_row(row_id, files[index],
                   selected_file_index.has_value() && *selected_file_index == files[index].index);
        }
      }
    }
  }
}

FileBrowserAction FileBrowserPage::hit_test_click() {
  if (Clay_PointerOver(open_source_id_)) {
    return FileBrowserAction::open_source;
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
