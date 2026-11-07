#pragma once

#include "torrent_stream.hpp"
#include <memory>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct mpv_handle;

namespace torrview {

class MpvPresentation;

struct AudioTrackInfo {
  int64_t id = -1;
  std::string label;
};

struct VideoPlaybackMetadata {
  std::string codec;
  std::string codec_profile;
  std::string decoder;
  std::string pixel_format;
  std::string hardware_pixel_format;
  std::string color_matrix;
  int width = 0;
  int height = 0;
  double fps = 0.0;
  double bitrate_bps = 0.0;
};

struct AudioPlaybackMetadata {
  std::string codec;
  std::string codec_profile;
  std::string decoder;
  std::string sample_format;
  std::string channels;
  int sample_rate = 0;
  int channel_count = 0;
  double bitrate_bps = 0.0;
};

struct PlaybackDiagnostics {
  std::string file_format;
  std::string hardware_decoder;
  double av_sync = 0.0;
  std::int64_t dropped_frames = 0;
  std::int64_t decoder_dropped_frames = 0;
  VideoPlaybackMetadata video;
  AudioPlaybackMetadata audio;
};

struct BufferedTimeRange {
  double start = 0.0;
  double end = 0.0;
};

struct PlaybackSnapshot {
  bool available = false;
  bool initialized = false;
  bool loaded = false;
  bool file_ready = false;
  bool position_valid = false;
  bool seeking = false;
  bool paused = true;
  bool eof_reached = false;
  double time_pos = 0.0;
  double duration = 0.0;
  std::vector<BufferedTimeRange> buffered_ranges;
  double volume = 100.0;
  int video_tracks = 0;
  int audio_tracks = 0;
  int subtitle_tracks = 0;
  int active_audio_index = -1;
  std::vector<int64_t> audio_track_ids;
  std::vector<AudioTrackInfo> audio_track_list;
  std::vector<AudioTrackInfo> subtitle_track_list;
  int active_subtitle_index = -1;
  PlaybackDiagnostics diagnostics;
  std::string media_title;
  std::string path;
  std::string status;
  std::string error;
};

class MpvPlayer final {
public:
  MpvPlayer();
  MpvPlayer(const MpvPlayer&) = delete;
  MpvPlayer& operator=(const MpvPlayer&) = delete;
  ~MpvPlayer();

  bool available() const;
  void set_torrent_stream_provider(TorrentStreamProvider* provider);
  void initialize();
  void load_file(const std::string& path, bool autoplay = true, double start_seconds = 0.0);
  void process_events();
  void render(int width, int height);
  void set_surface(void* surface, int width = 0, int height = 0);
  void select_subtitle_track(int index);
  void set_subtitle_font_directory(std::string directory);
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
  void update_video_params(void* node_data);
  void update_audio_params(void* node_data);
  void register_torrent_stream_protocol();
  void command(const char** args);
  void set_status_from_error(const char* action, int code);

  mpv_handle* handle_ = nullptr;
  std::unique_ptr<MpvPresentation> presentation_;
  TorrentStreamProvider* torrent_stream_provider_ = nullptr;
  bool torrent_stream_protocol_registered_ = false;
  PlaybackSnapshot snapshot_;
  std::string subtitle_font_directory_;
  double pending_start_ = 0.0;
  bool pending_autoplay_ = true;
};

} // namespace torrview
