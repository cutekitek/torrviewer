#include "mpv_player.hpp"

#include "config.hpp"
#include "logger.hpp"
#include "mpv_presentation.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string_view>

#if defined(TORRVIEW_HAVE_MPV)
#include <mpv/client.h>
#include <mpv/stream_cb.h>
#endif

namespace torrview {
namespace {

#if defined(TORRVIEW_HAVE_MPV)
std::int64_t torrent_stream_read(void* cookie, char* buffer, std::uint64_t bytes) {
  auto* stream = static_cast<TorrentStreamReader*>(cookie);
  if (stream == nullptr) {
    TORRVIEW_LOG_ERROR("mpv torrent stream read failed: null stream cookie");
    return -1;
  }

  const std::int64_t result = stream->read(buffer, bytes);
  if (result <= 0) {
    TORRVIEW_LOG_DEBUG("mpv torrent stream read returned " << result << " for requested=" << bytes);
  }
  return result;
}

std::int64_t torrent_stream_seek(void* cookie, std::int64_t offset) {
  auto* stream = static_cast<TorrentStreamReader*>(cookie);
  if (stream == nullptr) {
    TORRVIEW_LOG_ERROR("mpv torrent stream seek failed: null stream cookie");
    return MPV_ERROR_GENERIC;
  }
  const std::int64_t result = stream->seek(offset);
  TORRVIEW_LOG_DEBUG("mpv torrent stream seek offset=" << offset << " result=" << result);
  return result;
}

std::int64_t torrent_stream_size(void* cookie) {
  auto* stream = static_cast<TorrentStreamReader*>(cookie);
  if (stream == nullptr) {
    TORRVIEW_LOG_ERROR("mpv torrent stream size failed: null stream cookie");
    return MPV_ERROR_UNSUPPORTED;
  }
  const std::int64_t result = stream->size();
  TORRVIEW_LOG_DEBUG("mpv torrent stream size result=" << result);
  return result;
}

void torrent_stream_cancel(void* cookie) {
  auto* stream = static_cast<TorrentStreamReader*>(cookie);
  if (stream != nullptr) {
    TORRVIEW_LOG_DEBUG("mpv torrent stream cancel");
    stream->cancel();
  }
}

void torrent_stream_close(void* cookie) {
  TORRVIEW_LOG_DEBUG("mpv torrent stream close");
  delete static_cast<TorrentStreamReader*>(cookie);
}

int torrent_stream_open(void* user_data, char* uri, mpv_stream_cb_info* info) {
  auto* provider = static_cast<TorrentStreamProvider*>(user_data);
  if (provider == nullptr || uri == nullptr || info == nullptr) {
    TORRVIEW_LOG_ERROR("mpv torrent stream open failed: invalid callback arguments");
    return MPV_ERROR_LOADING_FAILED;
  }

  TORRVIEW_LOG_INFO("mpv torrent stream open uri=" << uri);

  std::string error;
  std::unique_ptr<TorrentStreamReader> stream = provider->open_torrent_stream(uri, error);
  if (stream == nullptr) {
    TORRVIEW_LOG_ERROR("mpv torrent stream open failed: " << error);
    return MPV_ERROR_LOADING_FAILED;
  }

  info->cookie = stream.release();
  info->read_fn = torrent_stream_read;
  info->seek_fn = torrent_stream_seek;
  info->size_fn = torrent_stream_size;
  info->close_fn = torrent_stream_close;
  info->cancel_fn = torrent_stream_cancel;
  TORRVIEW_LOG_INFO("mpv torrent stream open ok");
  return 0;
}

void check_mpv(int code, const char* action) {
  if (code < 0) {
    throw std::runtime_error(std::string(action) + ": " + mpv_error_string(code));
  }
}

void set_optional_mpv_option(mpv_handle* handle, const char* name, const char* value) {
  const int code = mpv_set_option_string(handle, name, value);
  if (code == MPV_ERROR_OPTION_NOT_FOUND) {
    return;
  }

  check_mpv(code, (std::string("mpv ") + name + " option").c_str());
}

const mpv_node* map_lookup(const mpv_node& node, const char* key) {
  if (node.format != MPV_FORMAT_NODE_MAP || node.u.list == nullptr) {
    return nullptr;
  }

  for (int index = 0; index < node.u.list->num; ++index) {
    const char* item_key = node.u.list->keys[index];
    if (item_key != nullptr && std::string_view(item_key) == key) {
      return &node.u.list->values[index];
    }
  }

  return nullptr;
}

bool node_string_equals(const mpv_node& node, const char* expected) {
  return node.format == MPV_FORMAT_STRING && node.u.string != nullptr &&
         std::string_view(node.u.string) == expected;
}

int64_t node_int64_or(const mpv_node& node, int64_t fallback) {
  if (node.format == MPV_FORMAT_INT64) {
    return node.u.int64;
  }
  if (node.format == MPV_FORMAT_DOUBLE) {
    return static_cast<int64_t>(node.u.double_);
  }
  return fallback;
}

int64_t node_int64_or(const mpv_node* node, int64_t fallback) {
  return node != nullptr ? node_int64_or(*node, fallback) : fallback;
}

bool node_flag_or(const mpv_node& node, bool fallback) {
  if (node.format == MPV_FORMAT_FLAG) {
    return node.u.flag != 0;
  }
  return fallback;
}

bool node_flag_or(const mpv_node* node, bool fallback) {
  return node != nullptr ? node_flag_or(*node, fallback) : fallback;
}

double node_double_or(const mpv_node* node, double fallback = 0.0) {
  if (node == nullptr) {
    return fallback;
  }
  if (node->format == MPV_FORMAT_DOUBLE) {
    return node->u.double_;
  }
  if (node->format == MPV_FORMAT_INT64) {
    return static_cast<double>(node->u.int64);
  }
  return fallback;
}

std::string node_string_or(const mpv_node* node, std::string_view fallback = {}) {
  if (node != nullptr && node->format == MPV_FORMAT_STRING && node->u.string != nullptr) {
    return node->u.string;
  }

  return std::string(fallback);
}

void observe_optional_property(mpv_handle* handle, const char* name, mpv_format format) {
  const int code = mpv_observe_property(handle, 0, name, format);
  if (code < 0) {
    TORRVIEW_LOG_WARNING("mpv diagnostic property unavailable: " << name << " ("
                                                                 << mpv_error_string(code) << ')');
  }
}
#endif

} // namespace

MpvPlayer::MpvPlayer() : presentation_(make_mpv_presentation()) {}

MpvPlayer::~MpvPlayer() {
  presentation_.reset();
#if defined(TORRVIEW_HAVE_MPV)
  if (handle_ != nullptr) {
    mpv_terminate_destroy(handle_);
    handle_ = nullptr;
  }
#endif
}

bool MpvPlayer::available() const {
#if defined(TORRVIEW_HAVE_MPV)
  return true;
#else
  return false;
#endif
}

void MpvPlayer::set_torrent_stream_provider(TorrentStreamProvider* provider) {
  torrent_stream_provider_ = provider;
  register_torrent_stream_protocol();
}

void MpvPlayer::initialize() {
#if defined(TORRVIEW_HAVE_MPV)
  if (handle_ != nullptr) {
    return;
  }

  handle_ = mpv_create();
  if (handle_ == nullptr) {
    throw std::runtime_error("mpv_create failed");
  }
  TORRVIEW_LOG_DEBUG("mpv handle created");

  check_mpv(mpv_set_option_string(handle_, "terminal", "no"), "mpv terminal option");
  check_mpv(mpv_set_option_string(handle_, "msg-level", "all=warn"), "mpv msg-level option");
  presentation_->configure(handle_);
  if (!subtitle_font_directory_.empty()) {
    check_mpv(mpv_set_option_string(handle_, "sub-fonts-dir", subtitle_font_directory_.c_str()),
              "mpv subtitle fonts");
  }
  set_optional_mpv_option(handle_, "osc", "no");
  set_optional_mpv_option(handle_, "input-default-bindings", "no");
  set_optional_mpv_option(handle_, "input-vo-keyboard", "no");
  set_optional_mpv_option(handle_, "force-window", "no");
#if defined(_WIN32)
  set_optional_mpv_option(handle_, "hwdec", "auto-safe");
#endif
  check_mpv(mpv_set_option_string(handle_, "idle", "yes"), "mpv idle option");
  check_mpv(mpv_set_option_string(handle_, "keep-open", "yes"), "mpv keep-open option");
  set_optional_mpv_option(handle_, "audio-display", "no");

  int flag = 1;
  check_mpv(mpv_set_option(handle_, "pause", MPV_FORMAT_FLAG, &flag), "mpv pause option");

  register_torrent_stream_protocol();
  TORRVIEW_LOG_DEBUG("mpv initializing");
  check_mpv(mpv_initialize(handle_), "mpv_initialize");
  check_mpv(mpv_request_log_messages(handle_, "warn"), "mpv log events");
  TORRVIEW_LOG_DEBUG("mpv initialized");
  observe_properties();

  presentation_->initialize(handle_);

  snapshot_.available = true;
  snapshot_.initialized = true;
  snapshot_.status = "Ready";
#else
  snapshot_.available = false;
  snapshot_.initialized = false;
  snapshot_.status = "This build was configured without libmpv";
#endif
}

void MpvPlayer::load_file(const std::string& path, bool autoplay) {
#if defined(TORRVIEW_HAVE_MPV)
  if (handle_ == nullptr) {
    initialize();
  }

  if (path.starts_with("torrview://")) {
    TORRVIEW_LOG_INFO("mpv loadfile " << path);
  }

  int pause = autoplay ? 0 : 1;
  check_mpv(mpv_set_property(handle_, "pause", MPV_FORMAT_FLAG, &pause), "mpv initial pause");
  const char* args[] = {"loadfile", path.c_str(), "replace", nullptr};
  command(args);
  snapshot_.loaded = true;
  snapshot_.paused = !autoplay;
  snapshot_.eof_reached = false;
  snapshot_.path = path;
  snapshot_.media_title.clear();
  snapshot_.error.clear();
  snapshot_.diagnostics = {};
  snapshot_.buffered_ranges.clear();
  snapshot_.status = "Loading";
#else
  (void)path;
  (void)autoplay;
  throw std::runtime_error("Cannot play local media: this build was configured without libmpv");
#endif
}

void MpvPlayer::process_events() {
#if defined(TORRVIEW_HAVE_MPV)
  if (handle_ == nullptr) {
    return;
  }

  while (true) {
    mpv_event* event = mpv_wait_event(handle_, 0.0);
    if (event == nullptr || event->event_id == MPV_EVENT_NONE) {
      break;
    }

    switch (event->event_id) {
    case MPV_EVENT_LOG_MESSAGE: {
      const auto* message = static_cast<mpv_event_log_message*>(event->data);
      if (message != nullptr) {
        TORRVIEW_LOG_WARNING("mpv " << message->prefix << ": " << message->text);
      }
      break;
    }
    case MPV_EVENT_FILE_LOADED:
      if (snapshot_.path.starts_with("torrview://")) {
        TORRVIEW_LOG_INFO("mpv event file-loaded");
      }
      snapshot_.loaded = true;
      snapshot_.eof_reached = false;
      snapshot_.status = "Playing";
      break;
    case MPV_EVENT_END_FILE: {
      if (snapshot_.path.starts_with("torrview://")) {
        auto* end_file = static_cast<mpv_event_end_file*>(event->data);
        std::ostringstream out;
        out << "event end-file reason="
            << (end_file != nullptr ? static_cast<int>(end_file->reason) : -1)
            << " error=" << (end_file != nullptr ? end_file->error : 0);
        if (end_file != nullptr && end_file->error < 0) {
          out << " (" << mpv_error_string(end_file->error) << ')';
        }
        TORRVIEW_LOG_WARNING(out.str());
      }
      snapshot_.eof_reached = true;
      snapshot_.paused = true;
      const auto* end = static_cast<mpv_event_end_file*>(event->data);
      if (end != nullptr && end->error < 0) {
        snapshot_.loaded = false;
        set_status_from_error("Playback failed", end->error);
      } else {
        snapshot_.status = "Ended";
      }
      break;
    }
    case MPV_EVENT_PROPERTY_CHANGE: {
      auto* property = static_cast<mpv_event_property*>(event->data);
      if (property != nullptr) {
        handle_property(property->name, property->data, property->format);
      }
      break;
    }
    case MPV_EVENT_SHUTDOWN:
      snapshot_.loaded = false;
      snapshot_.status = "Stopped";
      break;
    default:
      if (event->error < 0) {
        set_status_from_error(mpv_event_name(event->event_id), event->error);
      }
      break;
    }
  }
#endif
}

void MpvPlayer::render(int width, int height) { presentation_->render(width, height); }

void MpvPlayer::report_swap() { presentation_->report_swap(); }

void MpvPlayer::set_surface(void* surface, int width, int height) {
  presentation_->set_surface(surface, width, height);
}

void MpvPlayer::set_subtitle_font_directory(std::string directory) {
  if (handle_ != nullptr) {
    throw std::logic_error("Subtitle font directory must be set before initialization");
  }
  subtitle_font_directory_ = std::move(directory);
}

void MpvPlayer::select_subtitle_track(int index) {
#if defined(TORRVIEW_HAVE_MPV)
  if (handle_ == nullptr || index < -1 ||
      index >= static_cast<int>(snapshot_.subtitle_track_list.size())) {
    return;
  }
  const std::string id =
      index < 0 ? "no"
                : std::to_string(snapshot_.subtitle_track_list[static_cast<std::size_t>(index)].id);
  const int result = mpv_set_property_string(handle_, "sid", id.c_str());
  if (result < 0) {
    set_status_from_error("set subtitle track", result);
  } else {
    snapshot_.active_subtitle_index = index;
  }
#else
  (void)index;
#endif
}

void MpvPlayer::toggle_pause() {
#if defined(TORRVIEW_HAVE_MPV)
  if (!snapshot_.loaded) {
    return;
  }

  if (snapshot_.eof_reached) {
    seek_absolute(0.0);
    set_pause(false);
    snapshot_.eof_reached = false;
    return;
  }

  set_pause(!snapshot_.paused);
#endif
}

void MpvPlayer::set_pause(bool pause) {
#if defined(TORRVIEW_HAVE_MPV)
  if (handle_ == nullptr) {
    return;
  }

  int flag = pause ? 1 : 0;
  const int result = mpv_set_property(handle_, "pause", MPV_FORMAT_FLAG, &flag);
  if (result < 0) {
    set_status_from_error("set pause", result);
  } else {
    snapshot_.paused = pause;
  }
#else
  (void)pause;
#endif
}

void MpvPlayer::seek_relative(double seconds) {
#if defined(TORRVIEW_HAVE_MPV)
  if (handle_ == nullptr || !snapshot_.loaded) {
    return;
  }

  const std::string amount = std::to_string(seconds);
  const char* args[] = {"seek", amount.c_str(), "relative", "exact", nullptr};
  command(args);
  snapshot_.eof_reached = false;
#else
  (void)seconds;
#endif
}

void MpvPlayer::seek_absolute(double seconds) {
#if defined(TORRVIEW_HAVE_MPV)
  if (handle_ == nullptr || !snapshot_.loaded) {
    return;
  }

  const std::string target = std::to_string(std::max(0.0, seconds));
  const char* args[] = {"seek", target.c_str(), "absolute", "exact", nullptr};
  command(args);
  snapshot_.eof_reached = false;
#else
  (void)seconds;
#endif
}

void MpvPlayer::adjust_volume(double delta) { set_volume(snapshot_.volume + delta); }

void MpvPlayer::set_volume(double volume) {
#if defined(TORRVIEW_HAVE_MPV)
  if (handle_ == nullptr) {
    return;
  }

  double clamped = std::clamp(volume, 0.0, 130.0);
  const int result = mpv_set_property(handle_, "volume", MPV_FORMAT_DOUBLE, &clamped);
  if (result < 0) {
    set_status_from_error("set volume", result);
  } else {
    snapshot_.volume = clamped;
  }
#else
  (void)volume;
#endif
}

void MpvPlayer::select_audio_track_relative(int direction) {
#if defined(TORRVIEW_HAVE_MPV)
  if (handle_ == nullptr || snapshot_.audio_track_list.empty() || direction == 0) {
    return;
  }

  const int track_count = static_cast<int>(snapshot_.audio_track_list.size());
  int current = snapshot_.active_audio_index >= 0 ? snapshot_.active_audio_index : 0;
  int next = (current + direction) % track_count;
  if (next < 0) {
    next += track_count;
  }

  select_audio_track(static_cast<std::size_t>(next));
#else
  (void)direction;
#endif
}

void MpvPlayer::select_audio_track(std::size_t index) {
#if defined(TORRVIEW_HAVE_MPV)
  if (handle_ == nullptr || index >= snapshot_.audio_track_list.size()) {
    return;
  }

  int64_t track_id = snapshot_.audio_track_list[index].id;
  const int result = mpv_set_property(handle_, "aid", MPV_FORMAT_INT64, &track_id);
  if (result < 0) {
    set_status_from_error("set audio track", result);
  } else {
    snapshot_.active_audio_index = static_cast<int>(index);
  }
#else
  (void)index;
#endif
}

void MpvPlayer::stop() {
#if defined(TORRVIEW_HAVE_MPV)
  if (handle_ == nullptr) {
    return;
  }

  const char* args[] = {"stop", nullptr};
  command(args);
#endif
  snapshot_.loaded = false;
  snapshot_.paused = true;
  snapshot_.eof_reached = false;
  snapshot_.time_pos = 0.0;
  snapshot_.duration = 0.0;
  snapshot_.buffered_ranges.clear();
  snapshot_.path.clear();
  snapshot_.media_title.clear();
  snapshot_.diagnostics = {};
  snapshot_.status = "Stopped";
  snapshot_.error.clear();
}

bool MpvPlayer::has_file() const { return snapshot_.loaded || !snapshot_.path.empty(); }

PlaybackSnapshot MpvPlayer::snapshot() const { return snapshot_; }

void MpvPlayer::observe_properties() {
#if defined(TORRVIEW_HAVE_MPV)
  check_mpv(mpv_observe_property(handle_, 0, "time-pos", MPV_FORMAT_DOUBLE), "observe time-pos");
  check_mpv(mpv_observe_property(handle_, 0, "duration", MPV_FORMAT_DOUBLE), "observe duration");
  check_mpv(mpv_observe_property(handle_, 0, "pause", MPV_FORMAT_FLAG), "observe pause");
  check_mpv(mpv_observe_property(handle_, 0, "eof-reached", MPV_FORMAT_FLAG),
            "observe eof-reached");
  check_mpv(mpv_observe_property(handle_, 0, "volume", MPV_FORMAT_DOUBLE), "observe volume");
  check_mpv(mpv_observe_property(handle_, 0, "media-title", MPV_FORMAT_STRING),
            "observe media-title");
  check_mpv(mpv_observe_property(handle_, 0, "track-list", MPV_FORMAT_NODE), "observe track-list");
  observe_optional_property(handle_, "file-format", MPV_FORMAT_STRING);
  observe_optional_property(handle_, "demuxer-cache-state", MPV_FORMAT_NODE);
  observe_optional_property(handle_, "video-params", MPV_FORMAT_NODE);
  observe_optional_property(handle_, "audio-params", MPV_FORMAT_NODE);
  observe_optional_property(handle_, "video-bitrate", MPV_FORMAT_DOUBLE);
  observe_optional_property(handle_, "audio-bitrate", MPV_FORMAT_DOUBLE);
  observe_optional_property(handle_, "container-fps", MPV_FORMAT_DOUBLE);
  observe_optional_property(handle_, "estimated-vf-fps", MPV_FORMAT_DOUBLE);
  observe_optional_property(handle_, "avsync", MPV_FORMAT_DOUBLE);
  observe_optional_property(handle_, "frame-drop-count", MPV_FORMAT_INT64);
  observe_optional_property(handle_, "decoder-frame-drop-count", MPV_FORMAT_INT64);
  observe_optional_property(handle_, "hwdec-current", MPV_FORMAT_STRING);
#endif
}

void MpvPlayer::handle_property(const char* name, void* data, int format) {
#if defined(TORRVIEW_HAVE_MPV)
  if (name == nullptr) {
    return;
  }

  const std::string_view property(name);
  if (property == "demuxer-cache-state") {
    snapshot_.buffered_ranges.clear();
    if (data == nullptr || format != MPV_FORMAT_NODE || snapshot_.path.empty()) {
      return;
    }
    const auto* ranges = map_lookup(*static_cast<mpv_node*>(data), "seekable-ranges");
    if (ranges == nullptr || ranges->format != MPV_FORMAT_NODE_ARRAY || ranges->u.list == nullptr) {
      return;
    }
    for (int index = 0; index < ranges->u.list->num; ++index) {
      const auto& range = ranges->u.list->values[index];
      const double start = node_double_or(map_lookup(range, "start"), -1.0);
      const double end = node_double_or(map_lookup(range, "end"), -1.0);
      if (std::isfinite(start) && std::isfinite(end) && end > std::max(0.0, start)) {
        snapshot_.buffered_ranges.push_back({std::max(0.0, start), end});
      }
    }
    return;
  }
  if (data == nullptr) {
    return;
  }
  if (property == "time-pos" && format == MPV_FORMAT_DOUBLE) {
    snapshot_.time_pos = *static_cast<double*>(data);
  } else if (property == "duration" && format == MPV_FORMAT_DOUBLE) {
    snapshot_.duration = *static_cast<double*>(data);
  } else if (property == "pause" && format == MPV_FORMAT_FLAG) {
    snapshot_.paused = *static_cast<int*>(data) != 0;
  } else if (property == "eof-reached" && format == MPV_FORMAT_FLAG) {
    snapshot_.eof_reached = *static_cast<int*>(data) != 0;
  } else if (property == "volume" && format == MPV_FORMAT_DOUBLE) {
    snapshot_.volume = *static_cast<double*>(data);
  } else if (property == "media-title" && format == MPV_FORMAT_STRING) {
    const auto* value = *static_cast<char**>(data);
    snapshot_.media_title = value != nullptr ? value : "";
  } else if (property == "track-list" && format == MPV_FORMAT_NODE) {
    update_track_counts(data);
  } else if (property == "file-format" && format == MPV_FORMAT_STRING) {
    const auto* value = *static_cast<char**>(data);
    snapshot_.diagnostics.file_format = value != nullptr ? value : "";
  } else if (property == "video-params" && format == MPV_FORMAT_NODE) {
    update_video_params(data);
  } else if (property == "audio-params" && format == MPV_FORMAT_NODE) {
    update_audio_params(data);
  } else if (property == "video-bitrate" && format == MPV_FORMAT_DOUBLE) {
    snapshot_.diagnostics.video.bitrate_bps = *static_cast<double*>(data);
  } else if (property == "audio-bitrate" && format == MPV_FORMAT_DOUBLE) {
    snapshot_.diagnostics.audio.bitrate_bps = *static_cast<double*>(data);
  } else if ((property == "container-fps" || property == "estimated-vf-fps") &&
             format == MPV_FORMAT_DOUBLE) {
    const double fps = *static_cast<double*>(data);
    if (property == "estimated-vf-fps" || snapshot_.diagnostics.video.fps <= 0.0) {
      snapshot_.diagnostics.video.fps = fps;
    }
  } else if (property == "avsync" && format == MPV_FORMAT_DOUBLE) {
    snapshot_.diagnostics.av_sync = *static_cast<double*>(data);
  } else if (property == "frame-drop-count" && format == MPV_FORMAT_INT64) {
    snapshot_.diagnostics.dropped_frames = *static_cast<std::int64_t*>(data);
  } else if (property == "decoder-frame-drop-count" && format == MPV_FORMAT_INT64) {
    snapshot_.diagnostics.decoder_dropped_frames = *static_cast<std::int64_t*>(data);
  } else if (property == "hwdec-current" && format == MPV_FORMAT_STRING) {
    const auto* value = *static_cast<char**>(data);
    snapshot_.diagnostics.hardware_decoder = value != nullptr ? value : "";
  }
#else
  (void)name;
  (void)data;
  (void)format;
#endif
}

void MpvPlayer::update_track_counts(void* node_data) {
#if defined(TORRVIEW_HAVE_MPV)
  auto* node = static_cast<mpv_node*>(node_data);
  VideoPlaybackMetadata& video = snapshot_.diagnostics.video;
  AudioPlaybackMetadata& audio = snapshot_.diagnostics.audio;
  snapshot_.video_tracks = 0;
  snapshot_.audio_tracks = 0;
  snapshot_.subtitle_tracks = 0;
  snapshot_.active_audio_index = -1;
  snapshot_.audio_track_ids.clear();
  snapshot_.audio_track_list.clear();
  snapshot_.subtitle_track_list.clear();
  snapshot_.active_subtitle_index = -1;
  video.codec.clear();
  video.codec_profile.clear();
  video.decoder.clear();
  audio.codec.clear();
  audio.codec_profile.clear();
  audio.decoder.clear();

  if (node == nullptr || node->format != MPV_FORMAT_NODE_ARRAY || node->u.list == nullptr) {
    return;
  }

  bool video_metadata_set = false;
  bool audio_metadata_set = false;
  for (int index = 0; index < node->u.list->num; ++index) {
    const mpv_node& track = node->u.list->values[index];
    const mpv_node* type = map_lookup(track, "type");
    if (type == nullptr) {
      continue;
    }

    if (node_string_equals(*type, "video")) {
      ++snapshot_.video_tracks;
      const bool selected = node_flag_or(map_lookup(track, "selected"), false);
      if (!video_metadata_set || selected) {
        video.codec = node_string_or(map_lookup(track, "codec"));
        video.codec_profile = node_string_or(map_lookup(track, "codec-profile"));
        video.decoder = node_string_or(map_lookup(track, "decoder"));
        video.pixel_format = node_string_or(map_lookup(track, "format-name"), video.pixel_format);
        video.width = static_cast<int>(
            node_int64_or(map_lookup(track, "demux-w"), static_cast<int64_t>(video.width)));
        video.height = static_cast<int>(
            node_int64_or(map_lookup(track, "demux-h"), static_cast<int64_t>(video.height)));
        video.fps = node_double_or(map_lookup(track, "demux-fps"), video.fps);
        video.bitrate_bps = node_double_or(map_lookup(track, "demux-bitrate"), video.bitrate_bps);
        video_metadata_set = true;
      }
    } else if (node_string_equals(*type, "audio")) {
      const mpv_node* id = map_lookup(track, "id");
      const mpv_node* selected = map_lookup(track, "selected");
      const mpv_node* title = map_lookup(track, "title");
      const mpv_node* lang = map_lookup(track, "lang");
      const int audio_index = snapshot_.audio_tracks;
      ++snapshot_.audio_tracks;
      if (id != nullptr) {
        const int64_t track_id = node_int64_or(*id, -1);
        std::ostringstream label;
        label << "Track " << (audio_index + 1);

        const std::string title_text = node_string_or(title);
        const std::string lang_text = node_string_or(lang);
        if (!title_text.empty()) {
          label << " - " << title_text;
        } else if (!lang_text.empty()) {
          label << " - " << lang_text;
        }

        snapshot_.audio_track_ids.push_back(track_id);
        snapshot_.audio_track_list.push_back({.id = track_id, .label = label.str()});
      }
      if (selected != nullptr && node_flag_or(*selected, false)) {
        snapshot_.active_audio_index = audio_index;
      }
      const bool is_selected = selected != nullptr && node_flag_or(*selected, false);
      if (!audio_metadata_set || is_selected) {
        audio.codec = node_string_or(map_lookup(track, "codec"));
        audio.codec_profile = node_string_or(map_lookup(track, "codec-profile"));
        audio.decoder = node_string_or(map_lookup(track, "decoder"));
        audio.sample_format = node_string_or(map_lookup(track, "format-name"), audio.sample_format);
        audio.channels = node_string_or(map_lookup(track, "demux-channels"), audio.channels);
        audio.sample_rate = static_cast<int>(node_int64_or(
            map_lookup(track, "demux-samplerate"), static_cast<int64_t>(audio.sample_rate)));
        audio.channel_count = static_cast<int>(node_int64_or(
            map_lookup(track, "demux-channel-count"), static_cast<int64_t>(audio.channel_count)));
        audio.bitrate_bps = node_double_or(map_lookup(track, "demux-bitrate"), audio.bitrate_bps);
        audio_metadata_set = true;
      }
    } else if (node_string_equals(*type, "sub")) {
      const int sub_index = snapshot_.subtitle_tracks++;
      const auto id = node_int64_or(map_lookup(track, "id"), -1);
      const auto label = node_string_or(
          map_lookup(track, "title"),
          node_string_or(map_lookup(track, "lang"), "Subtitle " + std::to_string(sub_index + 1)));
      snapshot_.subtitle_track_list.push_back({.id = id, .label = label});
      if (node_flag_or(map_lookup(track, "selected"), false)) {
        snapshot_.active_subtitle_index = sub_index;
      }
    }
  }

  if (snapshot_.active_audio_index < 0 && snapshot_.audio_tracks > 0) {
    snapshot_.active_audio_index = 0;
  }
#else
  (void)node_data;
#endif
}

void MpvPlayer::update_video_params(void* node_data) {
#if defined(TORRVIEW_HAVE_MPV)
  auto* node = static_cast<mpv_node*>(node_data);
  if (node == nullptr || node->format != MPV_FORMAT_NODE_MAP) {
    return;
  }

  VideoPlaybackMetadata& video = snapshot_.diagnostics.video;
  if (const mpv_node* width = map_lookup(*node, "w"); width != nullptr) {
    video.width = static_cast<int>(node_int64_or(*width, video.width));
  }
  if (const mpv_node* height = map_lookup(*node, "h"); height != nullptr) {
    video.height = static_cast<int>(node_int64_or(*height, video.height));
  }
  video.pixel_format = node_string_or(map_lookup(*node, "pixelformat"), video.pixel_format);
  video.hardware_pixel_format =
      node_string_or(map_lookup(*node, "hw-pixelformat"), video.hardware_pixel_format);
  video.color_matrix = node_string_or(map_lookup(*node, "colormatrix"), video.color_matrix);
#else
  (void)node_data;
#endif
}

void MpvPlayer::update_audio_params(void* node_data) {
#if defined(TORRVIEW_HAVE_MPV)
  auto* node = static_cast<mpv_node*>(node_data);
  if (node == nullptr || node->format != MPV_FORMAT_NODE_MAP) {
    return;
  }

  AudioPlaybackMetadata& audio = snapshot_.diagnostics.audio;
  audio.sample_format = node_string_or(map_lookup(*node, "format"), audio.sample_format);
  audio.channels = node_string_or(map_lookup(*node, "hr-channels"), audio.channels);
  if (const mpv_node* sample_rate = map_lookup(*node, "samplerate"); sample_rate != nullptr) {
    audio.sample_rate = static_cast<int>(node_int64_or(*sample_rate, audio.sample_rate));
  }
  if (const mpv_node* channel_count = map_lookup(*node, "channel-count");
      channel_count != nullptr) {
    audio.channel_count = static_cast<int>(node_int64_or(*channel_count, audio.channel_count));
  }
#else
  (void)node_data;
#endif
}

void MpvPlayer::register_torrent_stream_protocol() {
#if defined(TORRVIEW_HAVE_MPV)
  if (handle_ == nullptr || torrent_stream_provider_ == nullptr ||
      torrent_stream_protocol_registered_) {
    return;
  }

  check_mpv(
      mpv_stream_cb_add_ro(handle_, "torrview", torrent_stream_provider_, torrent_stream_open),
      "mpv torrview stream protocol");
  torrent_stream_protocol_registered_ = true;
#endif
}

void MpvPlayer::command(const char** args) {
#if defined(TORRVIEW_HAVE_MPV)
  const int result = mpv_command(handle_, args);
  if (result < 0) {
    if (snapshot_.path.starts_with("torrview://") ||
        (args != nullptr && args[0] != nullptr && std::string_view(args[0]) == "loadfile")) {
      std::ostringstream out;
      out << "command failed:";
      for (const char** arg = args; arg != nullptr && *arg != nullptr; ++arg) {
        out << ' ' << *arg;
      }
      out << " -> " << result << " (" << mpv_error_string(result) << ')';
      TORRVIEW_LOG_ERROR(out.str());
    }
    set_status_from_error(args != nullptr && args[0] != nullptr ? args[0] : "mpv command", result);
  }
#else
  (void)args;
#endif
}

void MpvPlayer::set_status_from_error(const char* action, int code) {
#if defined(TORRVIEW_HAVE_MPV)
  snapshot_.status =
      std::string(action != nullptr ? action : "mpv") + ": " + mpv_error_string(code);
  snapshot_.error = snapshot_.status;
#else
  (void)action;
  (void)code;
  snapshot_.status = "This build was configured without libmpv";
#endif
}

} // namespace torrview
