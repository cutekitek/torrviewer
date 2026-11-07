#include "library.hpp"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) {
  if (!ok)
    throw std::runtime_error(message);
}
} // namespace
int main() {
  const auto path =
      std::filesystem::temp_directory_path() /
      ("torrview-library-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sqlite");
  try {
    {
      torrview::Library db(path.string());
      torrview::LibraryEntry entry;
      entry.key = "hash";
      entry.source = "magnet:?xt=urn:btih:0123456789";
      entry.name = "Episode's name — 日本語";
      entry.metadata = {'d', '\0', 'e'};
      entry.files = {{0, "episode1.mkv", ".mkv", 5000000000LL, 0, true},
                     {1, "episode2.mkv", ".mkv", 6000000000LL, 5000000000LL, true},
                     {2, "readme.txt", ".txt", 100, 11000000000LL, false}};
      entry.last_file = 1;
      entry.progress[0] = {15.125, 1800};
      entry.progress[1] = {742.875, 1800.25};
      db.entries.push_back(entry);
      for (int i = 0; i < 25; ++i)
        db.save();
      // Destruction must flush the newest queued snapshot.
    }
    {
      torrview::Library db(path.string());
      require(db.entries.size() == 1, "entry missing after restart");
      auto& entry = db.entries.front();
      require(entry.last_file == 1, "last episode lost");
      require(entry.progress.at(1).position == 742.875, "fractional resume point lost");
      require(entry.progress.at(0).position == 15.125, "other episode progress lost");
      require(entry.metadata == std::vector<char>({'d', '\0', 'e'}), "binary metadata corrupted");
      require(entry.files.size() == 3 && !entry.files[2].likely_video, "non-video files lost");
      require(entry.files[1].offset == 5000000000LL, "64-bit file offset truncated");
      require(entry.name == "Episode's name — 日本語", "UTF-8 title corrupted");
      entry.name = "Renamed 'torrent'";
      entry.last_file = 0;
      entry.progress[0].position = 0;
      db.save();
      db.save_progress(entry.key, 0, {22.625, 1800});
      db.save_progress(entry.key, 0, {0, 1800});
    }
    {
      torrview::Library db(path.string());
      require(db.entries[0].name == "Renamed 'torrent'", "rename did not persist");
      require(db.entries[0].last_file == 0 && db.entries[0].progress[0].position == 0,
              "rewind to zero or episode switch lost");
      db.entries.clear();
      db.save();
    }
    {
      torrview::Library db(path.string());
      require(db.entries.empty(), "deletion did not persist");
    }
    std::filesystem::remove(path);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    std::filesystem::remove(path);
    return 1;
  }
}
