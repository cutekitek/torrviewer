#include "mpv_player.hpp"

#include "config.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string_view>

#if defined(TORRVIEW_HAVE_MPV)
#include <SDL3/SDL.h>
#include <mpv/client.h>
#include <mpv/render_gl.h>
#endif

namespace torrview {
namespace {

#if defined(TORRVIEW_HAVE_MPV)
void* get_mpv_gl_proc_address(void*, const char* name) {
  return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
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

bool node_flag_or(const mpv_node& node, bool fallback) {
  if (node.format == MPV_FORMAT_FLAG) {
    return node.u.flag != 0;
  }
  return fallback;
}

std::string node_string_or(const mpv_node* node, std::string_view fallback = {}) {
  if (node != nullptr && node->format == MPV_FORMAT_STRING && node->u.string != nullptr) {
    return node->u.string;
  }

  return std::string(fallback);
}
#endif

} // namespace

MpvPlayer::~MpvPlayer() {
#if defined(TORRVIEW_HAVE_MPV)
  if (render_context_ != nullptr) {
    mpv_render_context_free(render_context_);
    render_context_ = nullptr;
  }

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

void MpvPlayer::initialize() {
#if defined(TORRVIEW_HAVE_MPV)
  if (handle_ != nullptr) {
    return;
  }

  handle_ = mpv_create();
  if (handle_ == nullptr) {
    throw std::runtime_error("mpv_create failed");
  }

  check_mpv(mpv_set_option_string(handle_, "terminal", "no"), "mpv terminal option");
  check_mpv(mpv_set_option_string(handle_, "msg-level", "all=warn"), "mpv msg-level option");
  check_mpv(mpv_set_option_string(handle_, "vo", "libmpv"), "mpv video output option");
  set_optional_mpv_option(handle_, "osc", "no");
  set_optional_mpv_option(handle_, "input-default-bindings", "no");
  set_optional_mpv_option(handle_, "input-vo-keyboard", "no");
  set_optional_mpv_option(handle_, "force-window", "no");
  check_mpv(mpv_set_option_string(handle_, "idle", "yes"), "mpv idle option");
  check_mpv(mpv_set_option_string(handle_, "keep-open", "yes"), "mpv keep-open option");
  set_optional_mpv_option(handle_, "audio-display", "no");

  int flag = 1;
  check_mpv(mpv_set_option(handle_, "pause", MPV_FORMAT_FLAG, &flag), "mpv pause option");

  check_mpv(mpv_initialize(handle_), "mpv_initialize");
  observe_properties();

  mpv_opengl_init_params gl_init = {
      .get_proc_address = get_mpv_gl_proc_address,
      .get_proc_address_ctx = nullptr,
  };
  const char* api_type = MPV_RENDER_API_TYPE_OPENGL;
  mpv_render_param params[] = {
      {.type = MPV_RENDER_PARAM_API_TYPE, .data = const_cast<char*>(api_type)},
      {.type = MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, .data = &gl_init},
      {.type = MPV_RENDER_PARAM_INVALID, .data = nullptr},
  };
  check_mpv(mpv_render_context_create(&render_context_, handle_, params),
            "mpv_render_context_create");

  snapshot_.available = true;
  snapshot_.initialized = true;
  snapshot_.status = "Ready";
#else
  snapshot_.available = false;
  snapshot_.initialized = false;
  snapshot_.status = "This build was configured without libmpv";
#endif
}

void MpvPlayer::load_file(const std::string& path) {
#if defined(TORRVIEW_HAVE_MPV)
  if (handle_ == nullptr) {
    initialize();
  }

  const char* args[] = {"loadfile", path.c_str(), "replace", nullptr};
  command(args);
  snapshot_.loaded = true;
  snapshot_.paused = false;
  snapshot_.eof_reached = false;
  snapshot_.path = path;
  snapshot_.media_title.clear();
  snapshot_.status = "Loading";
  int pause = 0;
  check_mpv(mpv_set_property(handle_, "pause", MPV_FORMAT_FLAG, &pause), "mpv unpause");
#else
  (void)path;
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
    case MPV_EVENT_FILE_LOADED:
      snapshot_.loaded = true;
      snapshot_.eof_reached = false;
      snapshot_.status = "Playing";
      break;
    case MPV_EVENT_END_FILE:
      snapshot_.eof_reached = true;
      snapshot_.paused = true;
      snapshot_.status = "Ended";
      break;
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

void MpvPlayer::render(const WindowMetrics& metrics) {
#if defined(TORRVIEW_HAVE_MPV)
  if (render_context_ == nullptr || metrics.pixel_width <= 0 || metrics.pixel_height <= 0) {
    return;
  }

  (void)mpv_render_context_update(render_context_);
  mpv_opengl_fbo fbo = {
      .fbo = 0,
      .w = metrics.pixel_width,
      .h = metrics.pixel_height,
      .internal_format = 0,
  };
  int flip_y = 1;
  mpv_render_param params[] = {
      {.type = MPV_RENDER_PARAM_OPENGL_FBO, .data = &fbo},
      {.type = MPV_RENDER_PARAM_FLIP_Y, .data = &flip_y},
      {.type = MPV_RENDER_PARAM_INVALID, .data = nullptr},
  };
  const int result = mpv_render_context_render(render_context_, params);
  if (result < 0) {
    set_status_from_error("mpv render", result);
  }
#else
  (void)metrics;
#endif
}

void MpvPlayer::report_swap() {
#if defined(TORRVIEW_HAVE_MPV)
  if (render_context_ != nullptr) {
    mpv_render_context_report_swap(render_context_);
  }
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
  snapshot_.path.clear();
  snapshot_.media_title.clear();
  snapshot_.status = "Stopped";
}

bool MpvPlayer::has_file() const { return snapshot_.loaded || !snapshot_.path.empty(); }

PlaybackSnapshot MpvPlayer::snapshot() const { return snapshot_; }

void MpvPlayer::observe_properties() {
#if defined(TORRVIEW_HAVE_MPV)
  check_mpv(mpv_observe_property(handle_, 0, "time-pos", MPV_FORMAT_DOUBLE),
            "observe time-pos");
  check_mpv(mpv_observe_property(handle_, 0, "duration", MPV_FORMAT_DOUBLE),
            "observe duration");
  check_mpv(mpv_observe_property(handle_, 0, "pause", MPV_FORMAT_FLAG), "observe pause");
  check_mpv(mpv_observe_property(handle_, 0, "eof-reached", MPV_FORMAT_FLAG),
            "observe eof-reached");
  check_mpv(mpv_observe_property(handle_, 0, "volume", MPV_FORMAT_DOUBLE), "observe volume");
  check_mpv(mpv_observe_property(handle_, 0, "media-title", MPV_FORMAT_STRING),
            "observe media-title");
  check_mpv(mpv_observe_property(handle_, 0, "track-list", MPV_FORMAT_NODE),
            "observe track-list");
#endif
}

void MpvPlayer::handle_property(const char* name, void* data, int format) {
#if defined(TORRVIEW_HAVE_MPV)
  if (name == nullptr || data == nullptr) {
    return;
  }

  const std::string_view property(name);
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
    auto* value = static_cast<char*>(data);
    snapshot_.media_title = value != nullptr ? value : "";
  } else if (property == "track-list" && format == MPV_FORMAT_NODE) {
    update_track_counts(data);
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
  snapshot_.video_tracks = 0;
  snapshot_.audio_tracks = 0;
  snapshot_.subtitle_tracks = 0;
  snapshot_.active_audio_index = -1;
  snapshot_.audio_track_ids.clear();
  snapshot_.audio_track_list.clear();

  if (node == nullptr || node->format != MPV_FORMAT_NODE_ARRAY || node->u.list == nullptr) {
    return;
  }

  for (int index = 0; index < node->u.list->num; ++index) {
    const mpv_node& track = node->u.list->values[index];
    const mpv_node* type = map_lookup(track, "type");
    if (type == nullptr) {
      continue;
    }

    if (node_string_equals(*type, "video")) {
      ++snapshot_.video_tracks;
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
    } else if (node_string_equals(*type, "sub")) {
      ++snapshot_.subtitle_tracks;
    }
  }

  if (snapshot_.active_audio_index < 0 && snapshot_.audio_tracks > 0) {
    snapshot_.active_audio_index = 0;
  }
#else
  (void)node_data;
#endif
}

void MpvPlayer::command(const char** args) {
#if defined(TORRVIEW_HAVE_MPV)
  const int result = mpv_command(handle_, args);
  if (result < 0) {
    set_status_from_error(args != nullptr && args[0] != nullptr ? args[0] : "mpv command",
                          result);
  }
#else
  (void)args;
#endif
}

void MpvPlayer::set_status_from_error(const char* action, int code) {
#if defined(TORRVIEW_HAVE_MPV)
  snapshot_.status = std::string(action != nullptr ? action : "mpv") + ": " + mpv_error_string(code);
#else
  (void)action;
  (void)code;
  snapshot_.status = "This build was configured without libmpv";
#endif
}

} // namespace torrview
