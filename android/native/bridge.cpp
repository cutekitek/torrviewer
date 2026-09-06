#include "logger.hpp"
#include "mpv_player.hpp"
#include "torrent_service.hpp"

#include <jni.h>
extern "C" {
#include <libavcodec/jni.h>
}
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
// JNI is private to NativeEngine. Every entry point runs on its single worker thread.
struct Engine {
  torrview::TorrentService torrent;
  std::unique_ptr<torrview::MpvPlayer> player;
  jobject surface = nullptr;
  std::vector<jobject> retained_surfaces;
  int local_fd = -1;
  int pending_file = -1;
  double pending_position = -1;
  bool foreground = false;
  bool pending_play = false;

  Engine(std::string directory, std::string fonts, int cache_mib) : torrent(std::move(directory)) {
    TORRVIEW_LOG_INFO("Android torrent session initialized");
    torrent.set_cache_limit_mib(cache_mib);
    torrent.set_suspended(true);
    player = std::make_unique<torrview::MpvPlayer>();
    player->set_subtitle_font_directory(std::move(fonts));
    player->set_torrent_stream_provider(&torrent);
    player->initialize();
    TORRVIEW_LOG_INFO("Android playback engine initialized");
  }
  ~Engine() {
    // mpv cancellation unblocks outstanding custom stream reads before torrent destruction.
    player.reset();
    if (local_fd >= 0) {
      close(local_fd);
    }
  }
  void stop() {
    pending_play = false;
    pending_file = -1;
    pending_position = -1;
    player->stop();
    if (local_fd >= 0) {
      close(local_fd);
      local_fd = -1;
    }
  }
  void tick() {
    player->process_events();
    torrent.process_alerts();
    if (pending_file >= 0 && surface != nullptr && foreground) {
      const std::string uri = torrent.stream_uri_for_file(pending_file);
      if (!uri.empty()) {
        player->load_file(uri, pending_position < 0);
        pending_file = -1;
        if (pending_position >= 0) {
          player->set_pause(true);
        }
      }
    }
    if (pending_position >= 0 && player->snapshot().duration > 0) {
      player->seek_absolute(pending_position);
      player->set_pause(true);
      pending_position = -1;
    }
    if (pending_play && foreground && surface != nullptr && player->snapshot().loaded) {
      player->set_pause(false);
      pending_play = false;
    }
  }
};

Engine& engine(jlong handle) {
  if (handle == 0) {
    throw std::runtime_error("The playback engine is closed");
  }
  return *reinterpret_cast<Engine*>(static_cast<std::intptr_t>(handle));
}

void fail(JNIEnv* env, const char* message) {
  if (!env->ExceptionCheck()) {
    const auto cls = env->FindClass("java/lang/IllegalStateException");
    if (cls != nullptr) {
      env->ThrowNew(cls, message);
      env->DeleteLocalRef(cls);
    }
  }
}

std::string bytes(JNIEnv* env, jbyteArray value) {
  if (value == nullptr) {
    return {};
  }
  const jsize length = env->GetArrayLength(value);
  std::string result(static_cast<std::size_t>(length), '\0');
  env->GetByteArrayRegion(value, 0, length, reinterpret_cast<jbyte*>(result.data()));
  return result;
}

std::string quoted(const std::string& value) {
  std::ostringstream out;
  out << '"';
  for (const char character : value) {
    const auto ch = static_cast<unsigned char>(character);
    if (ch == '"' || ch == '\\') {
      out << '\\' << ch;
    } else if (ch < 32) {
      out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch);
    } else {
      out << ch;
    }
  }
  out << '"';
  return out.str();
}

double finite(double value) { return std::isfinite(value) ? value : 0; }

std::string snapshot(Engine& e) {
  e.tick();
  const auto p = e.player->snapshot();
  const auto t = e.torrent.snapshot();
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::boolalpha << "{\"loaded\":" << p.loaded << ",\"paused\":" << p.paused
      << ",\"position\":" << finite(p.time_pos) << ",\"duration\":" << finite(p.duration)
      << ",\"title\":" << quoted(p.media_title) << ",\"status\":" << quoted(p.status)
      << ",\"torrentStatus\":" << quoted(t.status)
      << ",\"error\":" << quoted(t.error.empty() ? p.error : t.error)
      << ",\"buffering\":" << t.buffering << ",\"bufferProgress\":" << finite(t.buffer_progress)
      << ",\"cacheUsed\":" << t.cache_bytes_used << ",\"cacheLimit\":" << t.cache_bytes_limit
      << ",\"downloadRate\":" << t.download_rate << ",\"peers\":" << t.peers
      << ",\"activeFile\":" << t.active_file_index << ",\"audioIndex\":" << p.active_audio_index
      << ",\"subtitleIndex\":" << p.active_subtitle_index << ",\"bufferedRanges\":[";
  bool first = true;
  for (const auto& range : p.buffered_ranges) {
    if (!first) {
      out << ',';
    }
    first = false;
    out << "{\"start\":" << finite(range.start) << ",\"end\":" << finite(range.end) << '}';
  }
  out << "],\"files\":[";
  first = true;
  for (const auto& file : t.files) {
    if (!first) {
      out << ',';
    }
    first = false;
    out << "{\"index\":" << file.index << ",\"path\":" << quoted(file.path)
        << ",\"size\":" << file.size << ",\"video\":" << file.likely_video << '}';
  }
  out << "],\"audio\":[";
  first = true;
  for (const auto& track : p.audio_track_list) {
    if (!first) {
      out << ',';
    }
    first = false;
    out << quoted(track.label);
  }
  out << "],\"subtitles\":[";
  first = true;
  for (const auto& track : p.subtitle_track_list) {
    if (!first) {
      out << ',';
    }
    first = false;
    out << quoted(track.label);
  }
  out << "]}";
  return out.str();
}
} // namespace

extern "C" JNIEXPORT jlong JNICALL Java_org_torrview_app_NativeBindings_create(JNIEnv* env, jobject,
                                                                               jbyteArray directory,
                                                                               jbyteArray fonts,
                                                                               jint cache) {
  try {
    JavaVM* vm = nullptr;
    if (env->GetJavaVM(&vm) != JNI_OK || av_jni_set_java_vm(vm, nullptr) < 0) {
      throw std::runtime_error("Cannot initialize Android media decoding");
    }
    const auto metadata_directory = bytes(env, directory);
    const auto certificates = std::filesystem::path(metadata_directory) / "cacerts.pem";
    if (std::filesystem::exists(certificates) &&
        setenv("SSL_CERT_FILE", certificates.c_str(), 1) != 0) {
      throw std::runtime_error("Cannot configure tracker certificate verification");
    }
    auto result =
        std::make_unique<Engine>(metadata_directory, bytes(env, fonts), std::clamp(cache, 16, 512));
    return static_cast<jlong>(reinterpret_cast<std::intptr_t>(result.release()));
  } catch (const std::exception& error) {
    fail(env, error.what());
    return 0;
  }
}

extern "C" JNIEXPORT void JNICALL Java_org_torrview_app_NativeBindings_release(JNIEnv* env, jobject,
                                                                               jlong handle) {
  if (handle == 0) {
    return;
  }
  auto* e = &engine(handle);
  const jobject surface = e->surface;
  const auto retained = std::move(e->retained_surfaces);
  delete e;
  if (surface != nullptr) {
    env->DeleteGlobalRef(surface);
  }
  for (jobject item : retained) {
    env->DeleteGlobalRef(item);
  }
}

extern "C" JNIEXPORT void JNICALL Java_org_torrview_app_NativeBindings_surface(
    JNIEnv* env, jobject, jlong handle, jobject surface, jint width, jint height) {
  try {
    auto& e = engine(handle);
    jobject next = surface == nullptr ? nullptr : env->NewGlobalRef(surface);
    if (surface != nullptr && next == nullptr) {
      return;
    }
    try {
      e.player->set_surface(next, std::max(0, width), std::max(0, height));
    } catch (...) {
      // If mpv partially attached, retain the reference until player destruction.
      if (next != nullptr) {
        e.retained_surfaces.push_back(next);
      }
      throw;
    }
    if (e.surface != nullptr) {
      env->DeleteGlobalRef(e.surface);
    }
    e.surface = next;
  } catch (const std::exception& error) {
    fail(env, error.what());
  }
}

extern "C" JNIEXPORT void JNICALL Java_org_torrview_app_NativeBindings_openFd(JNIEnv* env, jobject,
                                                                              jlong handle, jint fd,
                                                                              jboolean autoplay) {
  try {
    auto& e = engine(handle);
    if (lseek(fd, 0, SEEK_CUR) < 0) {
      throw std::runtime_error(
          "This document provider does not support seeking. Choose a local file.");
    }
    const int owned = dup(fd);
    if (owned < 0) {
      throw std::runtime_error("Cannot open the media file descriptor");
    }
    e.stop();
    e.torrent.reset();
    e.local_fd = owned;
    e.pending_play = autoplay && e.foreground && e.surface == nullptr;
    e.player->load_file("fd://" + std::to_string(owned),
                        autoplay && e.foreground && e.surface != nullptr);
    if (!e.foreground || e.surface == nullptr) {
      e.player->set_pause(true);
    }
  } catch (const std::exception& error) {
    fail(env, error.what());
  }
}

extern "C" JNIEXPORT void JNICALL Java_org_torrview_app_NativeBindings_command(
    JNIEnv* env, jobject, jlong handle, jbyteArray action, jbyteArray argument, jdouble amount) {
  try {
    auto& e = engine(handle);
    const auto op = bytes(env, action);
    const auto value = bytes(env, argument);
    if (!std::isfinite(amount)) {
      throw std::runtime_error("Invalid playback value");
    }
    if (op == "magnet" || op == "torrent") {
      e.stop();
      if (op == "magnet") {
        e.torrent.load_magnet(value);
      } else {
        e.torrent.load_torrent_file(value);
      }
      e.torrent.set_suspended(!e.foreground);
    } else if (op == "file") {
      const auto& files = e.torrent.snapshot().files;
      const auto found = std::find_if(files.begin(), files.end(), [amount](const auto& f) {
        return static_cast<double>(f.index) == amount;
      });
      if (found == files.end()) {
        throw std::runtime_error("Torrent file is unavailable");
      }
      e.stop();
      e.pending_file = found->index;
    } else if (op == "play") {
      e.pending_play = e.foreground;
      if (e.foreground && e.surface != nullptr) {
        e.torrent.set_suspended(false);
        e.player->set_pause(false);
        e.pending_play = false;
      }
    } else if (op == "pause") {
      e.pending_play = false;
      e.player->set_pause(true);
    } else if (op == "seek") {
      e.player->seek_absolute(amount);
    } else if (op == "resumePosition") {
      e.pending_play = false;
      e.pending_position = std::max(0.0, amount);
    } else if (op == "audio") {
      if (amount >= 0 && amount < 10000) {
        e.player->select_audio_track(static_cast<std::size_t>(amount));
      }
    } else if (op == "subtitle") {
      if (amount >= -1 && amount < 10000) {
        e.player->select_subtitle_track(static_cast<int>(amount));
      }
    } else if (op == "stop") {
      e.stop();
      e.torrent.reset();
    } else if (op == "foreground") {
      e.foreground = amount != 0;
      if (!e.foreground) {
        e.pending_play = false;
        e.player->set_pause(true);
      }
      e.torrent.set_suspended(!e.foreground);
    } else if (op == "network") {
      e.torrent.network_changed();
    } else if (op == "cache") {
      e.torrent.set_cache_limit_mib(static_cast<int>(std::clamp(amount, 16.0, 512.0)));
    } else {
      throw std::runtime_error("Unknown engine command");
    }
  } catch (const std::exception& error) {
    fail(env, error.what());
  }
}

extern "C" JNIEXPORT void JNICALL
Java_org_torrview_app_NativeBindings_settings(JNIEnv* env, jobject, jlong handle, jint cache,
                                              jint upload, jint connections, jbyteArray proxy) {
  try {
    auto& e = engine(handle);
    e.torrent.set_cache_limit_mib(std::clamp(cache, 16, 512));
    torrview::TorrentRuntimeSettings settings;
    settings.upload_speed_kib = std::clamp(upload, 0, 1024 * 1024);
    settings.max_connections = std::clamp(connections, 1, 1000);
    settings.proxy_url = bytes(env, proxy);
    e.torrent.set_runtime_settings(settings);
  } catch (const std::exception& error) {
    fail(env, error.what());
  }
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_org_torrview_app_NativeBindings_snapshot(JNIEnv* env, jobject, jlong handle) {
  try {
    const std::string state = snapshot(engine(handle));
    auto result = env->NewByteArray(static_cast<jsize>(state.size()));
    if (result != nullptr) {
      env->SetByteArrayRegion(result, 0, static_cast<jsize>(state.size()),
                              reinterpret_cast<const jbyte*>(state.data()));
    }
    return result;
  } catch (const std::exception& error) {
    fail(env, error.what());
    return nullptr;
  }
}
