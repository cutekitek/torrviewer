#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace torrview {

enum class TorrentLoadState {
  idle,
  loading_metadata,
  ready,
  error,
  unavailable,
};

struct TorrentFileInfo {
  int index = -1;
  std::string path;
  std::string extension;
  std::int64_t size = 0;
  std::int64_t offset = 0;
  bool likely_video = false;
};

struct TorrentSnapshot {
  bool available = false;
  TorrentLoadState state = TorrentLoadState::idle;
  bool has_metadata = false;
  float metadata_progress = 0.0F;
  int peers = 0;
  int seeds = 0;
  int download_rate = 0;
  int upload_rate = 0;
  std::string source;
  std::string name;
  std::string status;
  std::string error;
  std::vector<TorrentFileInfo> files;
  std::vector<TorrentFileInfo> video_files;
};

class TorrentService final {
public:
  TorrentService();
  TorrentService(const TorrentService&) = delete;
  TorrentService& operator=(const TorrentService&) = delete;
  ~TorrentService();

  bool available() const;
  void load_torrent_file(const std::string& path);
  void load_magnet(const std::string& magnet);
  void process_alerts();
  void reset();
  const TorrentSnapshot& snapshot() const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

const char* torrent_state_label(TorrentLoadState state);

} // namespace torrview
