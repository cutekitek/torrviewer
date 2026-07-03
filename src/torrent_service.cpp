#include "torrent_service.hpp"

#include "config.hpp"
#include "input_source.hpp"
#include "memory_disk_io.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#if defined(TORRVIEW_HAVE_LIBTORRENT)
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/download_priority.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
#endif

namespace torrview {
namespace {

#if defined(TORRVIEW_HAVE_LIBTORRENT)
std::string file_extension(std::string_view path) {
  std::filesystem::path file_path{std::string(path)};
  std::string extension = file_path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return extension;
}

std::string metadata_cache_path() {
  std::filesystem::path path = std::filesystem::temp_directory_path() / "torrview-metadata";
  std::error_code error;
  std::filesystem::create_directories(path, error);
  return path.string();
}

namespace lt = libtorrent;

std::string compact_alert_detail(std::string value) {
  for (char& character : value) {
    if (character == '\n' || character == '\r' || character == '\t') {
      character = ' ';
    }
  }

  constexpr std::size_t max_length = 96;
  if (value.size() > max_length) {
    value.resize(max_length - 3);
    value += "...";
  }
  return value;
}

lt::settings_pack make_session_settings() {
  lt::settings_pack settings;
  settings.set_str(lt::settings_pack::user_agent, "Torrview/" TORRVIEW_VERSION);
  settings.set_str(lt::settings_pack::listen_interfaces, "0.0.0.0:0,[::]:0");
  settings.set_bool(lt::settings_pack::enable_dht, true);
  settings.set_bool(lt::settings_pack::enable_lsd, false);
  settings.set_bool(lt::settings_pack::enable_upnp, false);
  settings.set_bool(lt::settings_pack::enable_natpmp, false);
  settings.set_int(lt::settings_pack::connections_limit, 80);
  settings.set_int(lt::settings_pack::upload_rate_limit, 1);
  settings.set_int(lt::settings_pack::unchoke_slots_limit, 0);
  settings.set_int(lt::settings_pack::num_optimistic_unchoke_slots, 0);
  settings.set_int(lt::settings_pack::alert_mask,
                   lt::alert_category::error | lt::alert_category::status |
                       lt::alert_category::tracker | lt::alert_category::dht |
                       lt::alert_category::peer);
  return settings;
}

lt::session_params make_session_params() {
  lt::session_params params(make_session_settings());
  params.disk_io_constructor = create_memory_disk_io;
  return params;
}

std::vector<TorrentFileInfo> extract_files(const lt::torrent_info& info) {
  std::vector<TorrentFileInfo> files;
  const lt::file_storage& storage = info.files();
  files.reserve(static_cast<std::size_t>(storage.num_files()));

  for (int index = 0; index < storage.num_files(); ++index) {
    const lt::file_index_t file_index(index);
    TorrentFileInfo file;
    file.index = index;
    file.path = storage.file_path(file_index);
    file.extension = file_extension(file.path);
    file.size = storage.file_size(file_index);
    file.offset = storage.file_offset(file_index);
    file.likely_video = !storage.pad_file_at(file_index) && has_media_extension(file.path);
    files.push_back(std::move(file));
  }

  return files;
}

std::vector<TorrentFileInfo> filter_video_files(const std::vector<TorrentFileInfo>& files) {
  std::vector<TorrentFileInfo> video_files;
  for (const TorrentFileInfo& file : files) {
    if (file.likely_video) {
      video_files.push_back(file);
    }
  }

  std::sort(video_files.begin(), video_files.end(), [](const auto& left, const auto& right) {
    if (left.size != right.size) {
      return left.size > right.size;
    }
    return left.path < right.path;
  });
  return video_files;
}

void configure_metadata_only_add(lt::add_torrent_params& params) {
  params.save_path = metadata_cache_path();
  params.flags &= ~lt::torrent_flags::auto_managed;
  params.flags &= ~lt::torrent_flags::paused;
  params.flags |= lt::torrent_flags::default_dont_download;
  params.max_connections = 80;

  if (params.ti != nullptr) {
    params.file_priorities.assign(static_cast<std::size_t>(params.ti->num_files()),
                                  lt::dont_download);
  }
}

const char* libtorrent_state_label(lt::torrent_status::state_t state) {
  switch (static_cast<int>(state)) {
#if TORRENT_ABI_VERSION == 1
  case 0:
    return "Queued for checking";
#endif
  case static_cast<int>(lt::torrent_status::checking_files):
    return "Checking files";
  case static_cast<int>(lt::torrent_status::downloading_metadata):
    return "Downloading metadata";
  case static_cast<int>(lt::torrent_status::downloading):
    return "Downloading";
#if TORRENT_ABI_VERSION == 1
  case 6:
    return "Allocating";
#endif
  case static_cast<int>(lt::torrent_status::finished):
    return "Finished";
  case static_cast<int>(lt::torrent_status::seeding):
    return "Seeding";
  case static_cast<int>(lt::torrent_status::checking_resume_data):
    return "Checking resume data";
  }
  return "Waiting";
}

#endif

} // namespace

#if defined(TORRVIEW_HAVE_LIBTORRENT)
class TorrentService::Impl final {
public:
  Impl() : session_(make_session_params()) {
    snapshot_.available = true;
    snapshot_.state = TorrentLoadState::idle;
    snapshot_.status = "Ready";
  }

  bool available() const { return true; }

  void load_torrent_file(const std::string& path) {
    clear_current();
    snapshot_.source = path;
    snapshot_.state = TorrentLoadState::loading_metadata;
    snapshot_.status = "Reading torrent metadata";

    lt::error_code error;
    auto info = std::make_shared<lt::torrent_info>(path, error);
    if (error) {
      fail("Failed to load .torrent: " + error.message());
      return;
    }

    snapshot_.name = info->name();
    set_files(extract_files(*info));
    snapshot_.has_metadata = true;
    snapshot_.metadata_progress = 1.0F;
    snapshot_.state = TorrentLoadState::ready;
    snapshot_.status = "Metadata loaded";

    lt::add_torrent_params params;
    params.ti = std::move(info);
    configure_metadata_only_add(params);
    session_.async_add_torrent(std::move(params));
  }

  void load_magnet(const std::string& magnet) {
    clear_current();
    snapshot_.source = magnet;
    snapshot_.state = TorrentLoadState::loading_metadata;
    snapshot_.status = "Waiting for magnet metadata";

    lt::error_code error;
    lt::add_torrent_params params = lt::parse_magnet_uri(magnet, error);
    if (error) {
      fail("Invalid magnet link: " + error.message());
      return;
    }

    configure_metadata_only_add(params);
    session_.async_add_torrent(std::move(params));
  }

  void process_alerts() {
    std::vector<lt::alert*> alerts;
    session_.pop_alerts(&alerts);

    for (lt::alert* alert : alerts) {
      if (auto* add = lt::alert_cast<lt::add_torrent_alert>(alert)) {
        if (add->error) {
          fail("Failed to add torrent: " + add->error.message());
          continue;
        }
        handle_ = add->handle;
        if (handle_.is_valid()) {
          if (auto info = handle_.torrent_file(); info != nullptr && info->is_valid()) {
            snapshot_.name = info->name();
            set_files(extract_files(*info));
            snapshot_.has_metadata = true;
            snapshot_.metadata_progress = 1.0F;
            snapshot_.state = TorrentLoadState::ready;
            snapshot_.status = "Metadata loaded";
          }
        }
        continue;
      }

      if (auto* metadata = lt::alert_cast<lt::metadata_received_alert>(alert)) {
        handle_ = metadata->handle;
        if (auto info = handle_.torrent_file(); info != nullptr && info->is_valid()) {
          snapshot_.name = info->name();
          set_files(extract_files(*info));
          snapshot_.has_metadata = true;
          snapshot_.metadata_progress = 1.0F;
          snapshot_.state = TorrentLoadState::ready;
          snapshot_.status = "Metadata loaded";
        }
        continue;
      }

      if (auto* metadata_failed = lt::alert_cast<lt::metadata_failed_alert>(alert)) {
        fail("Metadata failed: " + metadata_failed->error.message());
        continue;
      }

      if (auto* torrent_error = lt::alert_cast<lt::torrent_error_alert>(alert)) {
        fail("Torrent error: " + torrent_error->error.message());
        continue;
      }

      if (auto* tracker_reply = lt::alert_cast<lt::tracker_reply_alert>(alert)) {
        note_network_event("Tracker replied: " + std::to_string(tracker_reply->num_peers) +
                           " peers");
        continue;
      }

      if (auto* tracker_warning = lt::alert_cast<lt::tracker_warning_alert>(alert)) {
        note_network_event("Tracker warning: " +
                           compact_alert_detail(tracker_warning->warning_message()));
        continue;
      }

      if (auto* tracker_error = lt::alert_cast<lt::tracker_error_alert>(alert)) {
        const char* failure = tracker_error->failure_reason();
        std::string detail =
            failure != nullptr && failure[0] != '\0' ? failure : tracker_error->error.message();
        note_network_event("Tracker error: " + compact_alert_detail(std::move(detail)));
        continue;
      }

      if (lt::alert_cast<lt::peer_error_alert>(alert) != nullptr) {
        note_network_event("Peer error: " + compact_alert_detail(alert->message()));
        continue;
      }

      if (lt::alert_cast<lt::peer_disconnected_alert>(alert) != nullptr) {
        note_network_event("Peer disconnected: " + compact_alert_detail(alert->message()));
        continue;
      }
    }

    poll_status();
  }

  void reset() { clear_current(); }

  const TorrentSnapshot& snapshot() const { return snapshot_; }

private:
  void clear_current() {
    if (handle_.is_valid()) {
      session_.remove_torrent(handle_);
      handle_ = {};
    }

    snapshot_ = {};
    snapshot_.available = true;
    snapshot_.state = TorrentLoadState::idle;
    snapshot_.status = "Ready";
  }

  void fail(std::string message) {
    last_network_event_.clear();
    snapshot_.state = TorrentLoadState::error;
    snapshot_.status = "Error";
    snapshot_.error = std::move(message);
    snapshot_.has_metadata = false;
    snapshot_.metadata_progress = 0.0F;
    snapshot_.files.clear();
    snapshot_.video_files.clear();
  }

  void set_files(std::vector<TorrentFileInfo> files) {
    snapshot_.files = std::move(files);
    snapshot_.video_files = filter_video_files(snapshot_.files);
  }

  void note_network_event(std::string event) {
    if (snapshot_.state == TorrentLoadState::loading_metadata) {
      last_network_event_ = std::move(event);
    }
  }

  void poll_status() {
    if (!handle_.is_valid()) {
      return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - last_status_poll_ < std::chrono::milliseconds(500)) {
      return;
    }
    last_status_poll_ = now;

    lt::torrent_status status = handle_.status();
    snapshot_.peers = status.num_peers;
    snapshot_.seeds = status.num_seeds;
    snapshot_.download_rate = status.download_rate;
    snapshot_.upload_rate = status.upload_rate;
    snapshot_.metadata_progress =
        status.has_metadata ? 1.0F : std::clamp(status.progress, 0.0F, 1.0F);
    snapshot_.has_metadata = status.has_metadata || snapshot_.has_metadata;
    if (!status.name.empty()) {
      snapshot_.name = status.name;
    }
    if (snapshot_.state == TorrentLoadState::loading_metadata && !snapshot_.has_metadata) {
      snapshot_.status = std::string(libtorrent_state_label(status.state)) + " - " +
                         std::to_string(status.num_peers) + " peers";
      if (!last_network_event_.empty()) {
        snapshot_.status += " - " + last_network_event_;
      }
    }
    if (status.errc) {
      fail("Torrent error: " + status.errc.message());
    }
  }

  lt::session session_;
  lt::torrent_handle handle_;
  TorrentSnapshot snapshot_;
  std::string last_network_event_;
  std::chrono::steady_clock::time_point last_status_poll_;
};
#else
class TorrentService::Impl final {
public:
  Impl() {
    snapshot_.available = false;
    snapshot_.state = TorrentLoadState::idle;
    snapshot_.status = "This build was configured without libtorrent";
  }

  bool available() const { return false; }

  void load_torrent_file(const std::string& path) {
    snapshot_.source = path;
    fail();
  }

  void load_magnet(const std::string& magnet) {
    snapshot_.source = magnet;
    fail();
  }

  void process_alerts() {}

  void reset() {
    snapshot_ = {};
    snapshot_.available = false;
    snapshot_.state = TorrentLoadState::idle;
    snapshot_.status = "This build was configured without libtorrent";
  }

  const TorrentSnapshot& snapshot() const { return snapshot_; }

private:
  void fail() {
    snapshot_.available = false;
    snapshot_.state = TorrentLoadState::unavailable;
    snapshot_.status = "Unavailable";
    snapshot_.error = "Torrent metadata browsing requires a build with libtorrent-rasterbar.";
  }

  TorrentSnapshot snapshot_;
};
#endif

TorrentService::TorrentService() : impl_(std::make_unique<Impl>()) {}
TorrentService::~TorrentService() = default;

bool TorrentService::available() const { return impl_->available(); }

void TorrentService::load_torrent_file(const std::string& path) { impl_->load_torrent_file(path); }

void TorrentService::load_magnet(const std::string& magnet) { impl_->load_magnet(magnet); }

void TorrentService::process_alerts() { impl_->process_alerts(); }

void TorrentService::reset() { impl_->reset(); }

const TorrentSnapshot& TorrentService::snapshot() const { return impl_->snapshot(); }

const char* torrent_state_label(TorrentLoadState state) {
  switch (state) {
  case TorrentLoadState::idle:
    return "Idle";
  case TorrentLoadState::loading_metadata:
    return "Loading metadata";
  case TorrentLoadState::ready:
    return "Ready";
  case TorrentLoadState::error:
    return "Error";
  case TorrentLoadState::unavailable:
    return "Unavailable";
  }
  return "Unknown";
}

} // namespace torrview
