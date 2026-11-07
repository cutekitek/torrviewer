#pragma once

#include "mpv_player.hpp"
#include "settings.hpp"
#include "torrent_service.hpp"
#include "ui/page.hpp"
#include "ui/widgets.hpp"
#include "window_state.hpp"

#include "clay.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace torrview {
class Application;
}

namespace torrview::ui {

enum class FileBrowserAction {
  none,
  open_source,
  open_settings,
  select_file,
};

class FileBrowserPage : public Page {
public:
  FileBrowserPage(Application& app, Settings& settings, MpvPlayer* player,
                  TorrentService& torrent_service);
  void open();
  void draw(RenderContext& render_context) override;
  bool handle_event(const SDL_Event& event) override;
  void close() override;

  void initialize_ids();
  void begin_frame();
  void build(const WindowMetrics& metrics, const TorrentSnapshot& snapshot,
             std::optional<int> selected_file_index);
  FileBrowserAction hit_test_click();
  int selected_file_index() const;

private:
  void header(const TorrentSnapshot& snapshot, float column_width);
  void file_row(std::size_t position, const TorrentFileInfo& file, int number,
                std::string_view root, bool selected, float column_width);
  void empty_state(const TorrentSnapshot& snapshot);

  Application& app_;
  Settings& settings_;
  MpvPlayer* player_ = nullptr;
  TorrentService& torrent_service_;
  Widgets ui_;
  Clay_ElementId open_source_id_ = {};
  Clay_ElementId settings_id_ = {};
  Clay_ElementId file_list_id_ = {};
  std::vector<Clay_ElementId> row_ids_;
  std::vector<int> row_file_indices_;
  int selected_file_index_ = -1;
};

} // namespace torrview::ui
