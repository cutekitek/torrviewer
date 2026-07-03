#pragma once

#include "torrent_service.hpp"
#include "window_state.hpp"

#include "clay.h"

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace torrview::ui {

enum class FileBrowserAction {
  none,
  open_source,
  select_file,
};

class FileBrowserPage {
public:
  void initialize_ids();
  void begin_frame();
  void build(const WindowMetrics& metrics, const TorrentSnapshot& snapshot,
             std::optional<int> selected_file_index);
  FileBrowserAction hit_test_click();
  int selected_file_index() const;

private:
  std::string_view retain_frame_text(std::string value);
  void text(std::string_view value, uint16_t font_size, Clay_Color color,
            Clay_TextElementConfigWrapMode wrap = CLAY_TEXT_WRAP_NONE);
  void file_row(Clay_ElementId id, const TorrentFileInfo& file, bool selected);

  Clay_ElementId open_source_id_ = {};
  Clay_ElementId file_list_id_ = {};
  std::vector<Clay_ElementId> row_ids_;
  std::vector<int> row_file_indices_;
  int selected_file_index_ = -1;
  std::deque<std::string> frame_strings_;
};

} // namespace torrview::ui
