#pragma once

#include "torrent_stream.hpp"

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

enum class TorrentPieceState {
  missing,
  requested,
  partial,
  verified,
  retained,
  evictable,
  evicted,
  failed,
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
  int active_file_index = -1;
  int cache_limit_mib = 256;
  std::int64_t cache_bytes_used = 0;
  std::int64_t cache_bytes_limit = 256LL * 1024LL * 1024LL;
  bool buffering = false;
  bool stalled = false;
  float buffer_progress = 0.0F;
  std::string buffer_status;
  std::vector<TorrentPieceState> piece_states;
};

class TorrentService final : public TorrentStreamProvider {
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
  void set_cache_limit_mib(int limit_mib);
  const TorrentSnapshot& snapshot() const;
  std::string stream_uri_for_file(int file_index) const;
  std::unique_ptr<TorrentStreamReader> open_torrent_stream(const std::string& uri,
                                                           std::string& error) override;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

const char* torrent_state_label(TorrentLoadState state);

} // namespace torrview
