#pragma once

#include "torrent_service.hpp"
#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <thread>

struct sqlite3;

namespace torrview {
struct VideoProgress {
  double position = 0;
  double duration = 0;
};
struct LibraryEntry {
  std::string key;
  std::string source;
  std::string name;
  std::vector<char> metadata;
  std::vector<TorrentFileInfo> files;
  std::map<int, VideoProgress> progress;
  int last_file = -1;
};

// The UI owns the model. Only the worker touches SQLite after startup.
class Library {
public:
  explicit Library(const std::string& path);
  ~Library();
  Library(const Library&) = delete;
  Library& operator=(const Library&) = delete;
  std::vector<LibraryEntry> entries;
  void save();
  void save_progress(const std::string& key, int index, VideoProgress progress);
  [[nodiscard]] std::string error() const;

private:
  void run();
  sqlite3* database_ = nullptr;
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::optional<std::vector<LibraryEntry>> pending_;
  struct ProgressUpdate {
    std::string key;
    int index;
    VideoProgress progress;
  };
  std::vector<ProgressUpdate> pending_progress_;
  std::string error_;
  bool needs_full_save_ = false;
  bool stopping_ = false;
  std::thread worker_;
};
} // namespace torrview
