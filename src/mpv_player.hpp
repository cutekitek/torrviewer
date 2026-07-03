#pragma once

#include "torrent_stream.hpp"
#include "window_state.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct mpv_handle;
struct mpv_render_context;

namespace torrview {

struct AudioTrackInfo {
  int64_t id = -1;
  std::string label;
};

struct PlaybackSnapshot {
  bool available = false;
  bool initialized = false;
  bool loaded = false;
  bool paused = true;
  bool eof_reached = false;
  double time_pos = 0.0;
  double duration = 0.0;
  double volume = 100.0;
  int video_tracks = 0;
  int audio_tracks = 0;
  int subtitle_tracks = 0;
  int active_audio_index = -1;
  std::vector<int64_t> audio_track_ids;
  std::vector<AudioTrackInfo> audio_track_list;
  std::string media_title;
  std::string path;
  std::string status;
};

class MpvPlayer final {
public:
  MpvPlayer() = default;
  MpvPlayer(const MpvPlayer&) = delete;
  MpvPlayer& operator=(const MpvPlayer&) = delete;
  ~MpvPlayer();

  bool available() const;
  void set_torrent_stream_provider(TorrentStreamProvider* provider);
  void initialize();
  void load_file(const std::string& path);
  void process_events();
  void render(const WindowMetrics& metrics);
  void report_swap();

  void toggle_pause();
  void set_pause(bool pause);
  void seek_relative(double seconds);
  void seek_absolute(double seconds);
  void adjust_volume(double delta);
  void set_volume(double volume);
  void select_audio_track_relative(int direction);
  void select_audio_track(std::size_t index);
  void stop();

  bool has_file() const;
  PlaybackSnapshot snapshot() const;

private:
  void observe_properties();
  void handle_property(const char* name, void* data, int format);
  void update_track_counts(void* node_data);
  void register_torrent_stream_protocol();
  void command(const char** args);
  void set_status_from_error(const char* action, int code);

  mpv_handle* handle_ = nullptr;
  mpv_render_context* render_context_ = nullptr;
  TorrentStreamProvider* torrent_stream_provider_ = nullptr;
  bool torrent_stream_protocol_registered_ = false;
  PlaybackSnapshot snapshot_;
};

} // namespace torrview
