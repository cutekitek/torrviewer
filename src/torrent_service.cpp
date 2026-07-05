#include "torrent_service.hpp"

#include "cache_policy.hpp"
#include "config.hpp"
#include "input_source.hpp"
#include "logger.hpp"
#include "memory_disk_io.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#if defined(__linux__)
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#endif

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

#include <boost/system/errc.hpp>
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

int env_int_or_default(const char* name, int fallback) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }

  try {
    return std::stoi(value);
  } catch (const std::exception&) {
    return fallback;
  }
}

#if defined(__linux__)
std::optional<std::string> linux_default_route_interface() {
  std::ifstream routes("/proc/net/route");
  std::string iface;
  std::string destination;
  std::string gateway;
  std::string flags_text;

  std::string header;
  std::getline(routes, header);
  while (routes >> iface >> destination >> gateway >> flags_text) {
    if (destination != "00000000") {
      std::string rest;
      std::getline(routes, rest);
      continue;
    }

    unsigned int flags = 0;
    try {
      flags = static_cast<unsigned int>(std::stoul(flags_text, nullptr, 16));
    } catch (const std::exception&) {
      return std::nullopt;
    }

    if ((flags & RTF_UP) != 0U) {
      return iface;
    }
  }

  return std::nullopt;
}

std::optional<std::string> linux_ipv4_for_interface(const std::string& interface_name) {
  ifaddrs* addresses = nullptr;
  if (getifaddrs(&addresses) != 0) {
    return std::nullopt;
  }

  std::optional<std::string> result;
  for (ifaddrs* address = addresses; address != nullptr; address = address->ifa_next) {
    if (address->ifa_addr == nullptr || interface_name != address->ifa_name ||
        address->ifa_addr->sa_family != AF_INET ||
        (address->ifa_flags & IFF_LOOPBACK) != 0U) {
      continue;
    }

    char buffer[INET_ADDRSTRLEN] = {};
    const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address->ifa_addr);
    if (inet_ntop(AF_INET, &ipv4->sin_addr, buffer, sizeof(buffer)) != nullptr) {
      result = std::string(buffer) + ":0";
      break;
    }
  }

  freeifaddrs(addresses);
  return result;
}
#endif

std::string default_listen_interfaces() {
#if defined(__linux__)
  if (const auto iface = linux_default_route_interface(); iface.has_value()) {
    if (const auto ipv4 = linux_ipv4_for_interface(*iface); ipv4.has_value()) {
      return *ipv4;
    }
  }
#endif
  return "0.0.0.0:0";
}

lt::settings_pack make_session_settings() {
  lt::settings_pack settings;
  const char* listen_env = std::getenv("TORRVIEW_LISTEN_INTERFACES");
  const std::string listen_interfaces =
      listen_env != nullptr && listen_env[0] != '\0' ? listen_env : default_listen_interfaces();
  settings.set_str(lt::settings_pack::user_agent, "Torrview/" TORRVIEW_VERSION);
  settings.set_str(lt::settings_pack::listen_interfaces, listen_interfaces);
  settings.set_bool(lt::settings_pack::enable_dht, true);
  settings.set_bool(lt::settings_pack::enable_lsd, true);
  settings.set_bool(lt::settings_pack::enable_upnp, true);
  settings.set_bool(lt::settings_pack::enable_natpmp, true);
  settings.set_int(lt::settings_pack::connections_limit, 200);
  settings.set_int(lt::settings_pack::upload_rate_limit,
                   env_int_or_default("TORRVIEW_UPLOAD_RATE_LIMIT", 1024 * 1024));
  settings.set_int(lt::settings_pack::alert_mask,
                   lt::alert_category::error | lt::alert_category::status |
                       lt::alert_category::tracker | lt::alert_category::dht |
                       lt::alert_category::peer | lt::alert_category::storage |
                       lt::alert_category::piece_progress);
  TORRVIEW_LOG_INFO("torrent listen_interfaces=" << listen_interfaces);
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

std::optional<std::pair<std::uint64_t, int>> parse_stream_uri(std::string_view uri) {
  constexpr std::string_view prefix = "torrview://torrent/";
  if (!uri.starts_with(prefix)) {
    return std::nullopt;
  }

  std::string_view rest = uri.substr(prefix.size());
  const std::size_t slash = rest.find('/');
  if (slash == std::string_view::npos || slash == 0 || slash + 1 >= rest.size()) {
    return std::nullopt;
  }

  std::uint64_t generation = 0;
  int file_index = 0;
  try {
    generation = std::stoull(std::string(rest.substr(0, slash)));
    file_index = std::stoi(std::string(rest.substr(slash + 1)));
  } catch (const std::exception&) {
    return std::nullopt;
  }

  if (file_index < 0) {
    return std::nullopt;
  }
  return std::make_pair(generation, file_index);
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

std::string format_rate(int bytes_per_second) {
  if (bytes_per_second <= 0) {
    return "0 KiB/s";
  }

  constexpr double kib = 1024.0;
  constexpr double mib = kib * 1024.0;
  std::ostringstream out;
  if (bytes_per_second >= static_cast<int>(mib)) {
    out.precision(1);
    out << std::fixed << static_cast<double>(bytes_per_second) / mib << " MiB/s";
  } else {
    out << std::max(1, static_cast<int>(static_cast<double>(bytes_per_second) / kib))
        << " KiB/s";
  }
  return out.str();
}

PieceWindow clamp_to_file_window(PieceWindow window, int first_piece, int end_piece) {
  window.first_piece = std::clamp(window.first_piece, first_piece, end_piece);
  window.end_piece = std::clamp(window.end_piece, window.first_piece, end_piece);
  if (window.empty() && first_piece < end_piece) {
    window.first_piece = first_piece;
    window.end_piece = std::min(end_piece, first_piece + 1);
  }
  return window;
}

bool is_operation_canceled(const lt::error_code& error) {
  return error == lt::error_code(boost::system::errc::operation_canceled,
                                 boost::system::generic_category());
}

#endif

} // namespace

#if defined(TORRVIEW_HAVE_LIBTORRENT)
struct SchedulerSnapshot {
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

struct SchedulerDiagnostics {
  int urgent_piece = -1;
  PieceWindow active_window;
};

class StreamingScheduler final {
public:
  StreamingScheduler(lt::torrent_handle handle, std::shared_ptr<const lt::torrent_info> info,
                     int file_index, CachePolicy policy, std::uint64_t stream_id)
      : handle_(std::move(handle)), info_(std::move(info)), file_index_(file_index),
        stream_id_(stream_id),
        policy_(normalize_cache_policy(policy)) {
    const lt::file_storage& files = info_->files();
    const lt::file_index_t index(file_index_);
    file_offset_ = files.file_offset(index);
    file_size_ = files.file_size(index);
    piece_length_ = files.piece_length();
    total_pieces_ = static_cast<int>(files.end_piece());

    if (file_size_ > 0) {
      first_file_piece_ = static_cast<int>(file_offset_ / piece_length_);
      const std::int64_t file_end = file_offset_ + file_size_;
      end_file_piece_ = static_cast<int>((file_end + piece_length_ - 1) / piece_length_);
      end_file_piece_ = std::clamp(end_file_piece_, first_file_piece_, total_pieces_);
    }

    TORRVIEW_LOG_INFO("torrent scheduler stream=" << stream_id_ << " file=" << file_index_
                                                  << " offset=" << file_offset_
                                                  << " size=" << file_size_
                                                  << " piece_length=" << piece_length_
                                                  << " file_pieces=[" << first_file_piece_ << ','
                                                  << end_file_piece_ << ") cache_mib="
                                                  << policy_.limit_mib());
  }

  void prepare_initial_priorities() {
    if (!handle_.is_valid() || info_ == nullptr) {
      return;
    }

    try {
      handle_.unset_flags(lt::torrent_flags::paused | lt::torrent_flags::upload_mode);
      handle_.resume();
      handle_.force_reannounce();
      handle_.clear_piece_deadlines();
      std::vector<lt::download_priority_t> file_priorities(
          static_cast<std::size_t>(info_->num_files()), lt::dont_download);
      file_priorities[static_cast<std::size_t>(file_index_)] = lt::top_priority;
      handle_.prioritize_files(file_priorities);
      TORRVIEW_LOG_INFO("torrent scheduler stream=" << stream_id_
                                                    << " selected file priority prepared and torrent resumed");
    } catch (const std::exception&) {
      TORRVIEW_LOG_WARNING("torrent scheduler stream=" << stream_id_
                                                       << " failed to prepare file priorities");
    }

    schedule_for_read(0, 1);
  }

  void set_policy(CachePolicy policy) {
    {
      std::lock_guard lock(mutex_);
      policy_ = normalize_cache_policy(policy);
    }
    TORRVIEW_LOG_INFO("torrent scheduler stream=" << stream_id_
                                                  << " cache policy changed to "
                                                  << policy_.limit_mib() << " MiB");
    set_memory_disk_cache_policy(policy_);
    schedule_for_read(last_file_position(), 1);
  }

  void schedule_for_read(std::int64_t file_position, std::uint64_t requested_bytes) {
    const std::int64_t clamped_position =
        std::clamp<std::int64_t>(file_position, 0, std::max<std::int64_t>(0, file_size_));
    const std::int64_t torrent_offset = file_offset_ + clamped_position;
    const int urgent_piece = piece_for_torrent_offset(torrent_offset);

    SchedulerWork work;
    {
      std::lock_guard lock(mutex_);
      last_file_position_ = clamped_position;
      last_requested_bytes_ = requested_bytes;
      urgent_piece_ = urgent_piece;
      work = build_work_locked(torrent_offset, false);
    }
    TORRVIEW_LOG_DEBUG("torrent scheduler stream=" << stream_id_ << " read position="
                                                   << clamped_position
                                                   << " requested=" << requested_bytes
                                                   << " urgent_piece=" << urgent_piece);
    apply_work(work);
  }

  void on_seek(std::int64_t file_position) {
    const std::int64_t clamped_position =
        std::clamp<std::int64_t>(file_position, 0, std::max<std::int64_t>(0, file_size_));
    const std::int64_t torrent_offset = file_offset_ + clamped_position;
    const int urgent_piece = piece_for_torrent_offset(torrent_offset);

    SchedulerWork work;
    {
      std::lock_guard lock(mutex_);
      last_file_position_ = clamped_position;
      urgent_piece_ = urgent_piece;
      deadline_pieces_.clear();
      requested_at_.clear();
      stalled_ = false;
      work = build_work_locked(torrent_offset, true);
    }
    TORRVIEW_LOG_INFO("torrent scheduler stream=" << stream_id_ << " seek position="
                                                  << clamped_position
                                                  << " urgent_piece=" << urgent_piece);
    apply_work(work);
  }

  void request_urgent_piece(lt::piece_index_t piece) {
    const int piece_key = static_cast<int>(piece);
    {
      std::lock_guard lock(mutex_);
      requested_at_[piece_key] = std::chrono::steady_clock::now();
      deadline_pieces_.insert(piece_key);
    }

    if (!handle_.is_valid()) {
      return;
    }

    try {
      handle_.piece_priority(piece, lt::top_priority);
      if (!handle_.have_piece(piece)) {
        handle_.set_piece_deadline(piece, 0, lt::torrent_handle::alert_when_available);
        TORRVIEW_LOG_DEBUG("torrent scheduler stream=" << stream_id_
                                                       << " urgent deadline piece=" << piece_key);
      } else {
        TORRVIEW_LOG_DEBUG("torrent scheduler stream=" << stream_id_
                                                       << " urgent piece already available piece="
                                                       << piece_key);
      }
    } catch (const std::exception&) {
      TORRVIEW_LOG_WARNING("torrent scheduler stream=" << stream_id_
                                                       << " failed to request urgent piece="
                                                       << piece_key);
    }
  }

  void on_piece_finished(lt::piece_index_t piece) {
    std::lock_guard lock(mutex_);
    requested_at_.erase(static_cast<int>(piece));
    last_piece_progress_ = std::chrono::steady_clock::now();
    stalled_ = false;
    TORRVIEW_LOG_DEBUG("torrent scheduler stream=" << stream_id_
                                                   << " piece finished piece="
                                                   << static_cast<int>(piece));
  }

  void on_read_piece(lt::piece_index_t piece, const lt::error_code& error) {
    if (is_operation_canceled(error)) {
      TORRVIEW_LOG_DEBUG("torrent scheduler stream=" << stream_id_
                                                     << " ignored canceled read_piece alert piece="
                                                     << static_cast<int>(piece));
      return;
    }

    std::lock_guard lock(mutex_);
    const int piece_key = static_cast<int>(piece);
    requested_at_.erase(piece_key);
    if (error) {
      failed_pieces_.insert(piece_key);
      TORRVIEW_LOG_WARNING("torrent scheduler stream=" << stream_id_
                                                       << " read_piece failed piece=" << piece_key
                                                       << " error=" << error.message());
    } else {
      failed_pieces_.erase(piece_key);
      last_piece_progress_ = std::chrono::steady_clock::now();
      stalled_ = false;
      TORRVIEW_LOG_DEBUG("torrent scheduler stream=" << stream_id_
                                                     << " read_piece ok piece=" << piece_key);
    }
  }

  SchedulerSnapshot snapshot(int peers, int download_rate) {
    const MemoryDiskCacheStatus cache_status = memory_disk_cache_status();

    std::set<int> evicted(cache_status.evicted_piece_indices.begin(),
                          cache_status.evicted_piece_indices.end());
    SchedulerSnapshot view;
    view.active_file_index = file_index_;
    view.cache_limit_mib = policy_.limit_mib();
    view.cache_bytes_used = cache_status.bytes_used;
    view.cache_bytes_limit = cache_status.policy.max_bytes;

    PieceWindow window;
    std::set<int> deadline_pieces;
    std::set<int> failed_pieces;
    int urgent_piece = -1;
    {
      std::lock_guard lock(mutex_);
      window = active_window_;
      deadline_pieces = deadline_pieces_;
      failed_pieces = failed_pieces_;
      urgent_piece = urgent_piece_;
      view.stalled = stalled_;
    }

    int window_pieces = 0;
    int ready_window_pieces = 0;
    bool urgent_ready = true;
    const int piece_count = std::max(0, end_file_piece_ - first_file_piece_);
    view.piece_states.reserve(static_cast<std::size_t>(piece_count));

    for (int piece = first_file_piece_; piece < end_file_piece_; ++piece) {
      const lt::piece_index_t piece_index(piece);
      bool have = false;
      try {
        have = handle_.is_valid() && handle_.have_piece(piece_index);
      } catch (const std::exception&) {
        have = false;
      }

      if (window.contains(piece)) {
        ++window_pieces;
        if (have) {
          ++ready_window_pieces;
        }
      }
      if (piece == urgent_piece && !have) {
        urgent_ready = false;
      }

      TorrentPieceState state = TorrentPieceState::missing;
      if (failed_pieces.find(piece) != failed_pieces.end()) {
        state = TorrentPieceState::failed;
      } else if (evicted.find(piece) != evicted.end()) {
        state = TorrentPieceState::evicted;
      } else if (have && window.contains(piece)) {
        state = TorrentPieceState::retained;
      } else if (have) {
        state = TorrentPieceState::evictable;
      } else if (deadline_pieces.find(piece) != deadline_pieces.end()) {
        state = TorrentPieceState::requested;
      }
      view.piece_states.push_back(state);
    }

    view.buffer_progress = window_pieces > 0
                               ? static_cast<float>(ready_window_pieces) /
                                     static_cast<float>(window_pieces)
                               : 0.0F;
    view.buffering = !urgent_ready;

    std::ostringstream status;
    if (view.stalled) {
      status << "Stalled";
    } else if (view.buffering) {
      status << "Buffering";
    } else {
      status << "Streaming";
    }
    status << " - " << peers << " peers - " << format_rate(download_rate);
    status << " - " << static_cast<int>(std::lround(view.buffer_progress * 100.0F)) << "% ahead";
    view.buffer_status = status.str();
    return view;
  }

  void check_stalled() {
    const auto now = std::chrono::steady_clock::now();
    std::vector<int> stalled_pieces;
    {
      std::lock_guard lock(mutex_);
      for (const auto& [piece, requested_at] : requested_at_) {
        if (now - requested_at >= stalled_timeout) {
          stalled_pieces.push_back(piece);
        }
      }
      stalled_ = !stalled_pieces.empty();
      for (int piece : stalled_pieces) {
        requested_at_[piece] = now;
      }
    }

    if (!handle_.is_valid()) {
      return;
    }

    std::vector<int> availability;
    try {
      handle_.piece_availability(availability);
    } catch (const std::exception&) {
      availability.clear();
    }

    for (int piece : stalled_pieces) {
      try {
        const lt::piece_index_t piece_index(piece);
        if (!handle_.have_piece(piece_index)) {
          if (piece >= 0 && piece < static_cast<int>(availability.size()) &&
              availability[static_cast<std::size_t>(piece)] == 0) {
            TORRVIEW_LOG_WARNING("torrent scheduler stream="
                                 << stream_id_
                                 << " stalled piece unavailable; keeping deadline piece="
                                 << piece);
            continue;
          }
          handle_.reset_piece_deadline(piece_index);
          handle_.set_piece_deadline(piece_index, 0,
                                     lt::torrent_handle::alert_when_available);
          TORRVIEW_LOG_WARNING("torrent scheduler stream=" << stream_id_
                                                           << " stalled piece reprioritized piece="
                                                           << piece);
        }
      } catch (const std::exception&) {
        TORRVIEW_LOG_WARNING("torrent scheduler stream=" << stream_id_
                                                         << " stalled piece reprioritize failed piece="
                                                         << piece);
      }
    }
  }

  SchedulerDiagnostics diagnostics() const {
    std::lock_guard lock(mutex_);
    return {
        .urgent_piece = urgent_piece_,
        .active_window = active_window_,
    };
  }

private:
  struct SchedulerWork {
    bool clear_deadlines = false;
    std::vector<int> reset_deadlines;
    std::vector<int> background_priority;
    struct ScheduledPiece {
      int piece = 0;
      int deadline_ms = 0;
      lt::download_priority_t priority = lt::low_priority;
    };
    std::vector<ScheduledPiece> deadlines;
  };

  static constexpr auto stalled_timeout = std::chrono::seconds(15);
  static constexpr int deadline_step_ms = 150;
  static constexpr int max_deadline_ms = 30000;
  static constexpr int min_stream_priority = 1;
  static constexpr int max_stream_priority = 7;

  [[nodiscard]] std::int64_t last_file_position() const {
    std::lock_guard lock(mutex_);
    return last_file_position_;
  }

  [[nodiscard]] int piece_for_torrent_offset(std::int64_t torrent_offset) const {
    if (piece_length_ <= 0 || total_pieces_ <= 0) {
      return 0;
    }
    return std::clamp(static_cast<int>(torrent_offset / piece_length_), 0,
                      std::max(0, total_pieces_ - 1));
  }

  [[nodiscard]] static lt::download_priority_t piece_priority_for_buffer_position(
      int sequence, int total) {
    if (total <= 1) {
      return lt::top_priority;
    }

    const int clamped_sequence = std::clamp(sequence, 0, total - 1);
    const int priority = max_stream_priority -
                         ((max_stream_priority - min_stream_priority) * clamped_sequence) /
                             (total - 1);
    return lt::download_priority_t{
        static_cast<std::uint8_t>(std::clamp(priority, min_stream_priority, max_stream_priority))};
  }

  SchedulerWork build_work_locked(std::int64_t torrent_offset, bool clear_deadlines) {
    SchedulerWork work;
    work.clear_deadlines = clear_deadlines;

    PieceWindow next_window = compute_retained_piece_window(torrent_offset, total_pieces_,
                                                            piece_length_, policy_);
    next_window = clamp_to_file_window(next_window, first_file_piece_, end_file_piece_);
    update_memory_disk_retained_window(torrent_offset);

    std::set<int> previous_deadlines = deadline_pieces_;
    std::set<int> next_deadlines;
    const int current_piece = piece_for_torrent_offset(torrent_offset);

    if (active_window_.empty()) {
      for (int piece = first_file_piece_; piece < end_file_piece_; ++piece) {
        if (!next_window.contains(piece)) {
          work.background_priority.push_back(piece);
        }
      }
    } else {
      for (int piece = active_window_.first_piece; piece < active_window_.end_piece; ++piece) {
        if (!next_window.contains(piece)) {
          work.background_priority.push_back(piece);
        }
      }
    }

    const int first_scheduled_piece = std::max(current_piece, next_window.first_piece);
    const int scheduled_piece_count = std::max(0, next_window.end_piece - first_scheduled_piece);
    for (int piece = first_scheduled_piece; piece < next_window.end_piece; ++piece) {
      const int sequence = piece - first_scheduled_piece;
      const int deadline = std::min(max_deadline_ms, sequence * deadline_step_ms);
      work.deadlines.push_back({
          .piece = piece,
          .deadline_ms = deadline,
          .priority = piece_priority_for_buffer_position(sequence, scheduled_piece_count),
      });
      next_deadlines.insert(piece);
    }

    if (!clear_deadlines) {
      for (int piece : previous_deadlines) {
        if (next_deadlines.find(piece) == next_deadlines.end()) {
          work.reset_deadlines.push_back(piece);
        }
      }
    }

    active_window_ = next_window;
    deadline_pieces_ = std::move(next_deadlines);
    TORRVIEW_LOG_DEBUG("torrent scheduler stream=" << stream_id_ << " window=["
                                                   << active_window_.first_piece << ','
                                                   << active_window_.end_piece << ") deadlines="
                                                   << work.deadlines.size()
                                                   << " background_priority="
                                                   << work.background_priority.size()
                                                   << " clear_deadlines=" << work.clear_deadlines);
    return work;
  }

  void apply_work(const SchedulerWork& work) {
    if (!handle_.is_valid()) {
      return;
    }

    try {
      if (work.clear_deadlines) {
        handle_.clear_piece_deadlines();
      }

      for (int piece : work.reset_deadlines) {
        handle_.reset_piece_deadline(lt::piece_index_t(piece));
      }

      for (int piece : work.background_priority) {
        handle_.piece_priority(lt::piece_index_t(piece), lt::dont_download);
      }

      for (const SchedulerWork::ScheduledPiece& scheduled : work.deadlines) {
        const lt::piece_index_t piece_index(scheduled.piece);
        handle_.piece_priority(piece_index, scheduled.priority);
        if (!handle_.have_piece(piece_index)) {
          handle_.set_piece_deadline(piece_index, scheduled.deadline_ms);
        }
      }
    } catch (const std::exception&) {
      TORRVIEW_LOG_WARNING("torrent scheduler stream=" << stream_id_
                                                       << " failed to apply scheduler work");
    }
  }

  lt::torrent_handle handle_;
  std::shared_ptr<const lt::torrent_info> info_;
  int file_index_ = -1;
  std::uint64_t stream_id_ = 0;
  std::int64_t file_offset_ = 0;
  std::int64_t file_size_ = 0;
  int piece_length_ = 1;
  int total_pieces_ = 0;
  int first_file_piece_ = 0;
  int end_file_piece_ = 0;
  mutable std::mutex mutex_;
  CachePolicy policy_;
  PieceWindow active_window_;
  std::set<int> deadline_pieces_;
  std::map<int, std::chrono::steady_clock::time_point> requested_at_;
  std::set<int> failed_pieces_;
  std::int64_t last_file_position_ = 0;
  std::uint64_t last_requested_bytes_ = 0;
  int urgent_piece_ = -1;
  bool stalled_ = false;
  std::chrono::steady_clock::time_point last_piece_progress_;
};

class TorrentPieceStream final : public TorrentStreamReader {
public:
  TorrentPieceStream(lt::torrent_handle handle, std::shared_ptr<const lt::torrent_info> info,
                     int file_index, std::shared_ptr<StreamingScheduler> scheduler,
                     std::uint64_t stream_id)
      : handle_(std::move(handle)), info_(std::move(info)), scheduler_(std::move(scheduler)),
        stream_id_(stream_id) {
    const lt::file_storage& files = info_->files();
    const lt::file_index_t index(file_index);
    file_offset_ = files.file_offset(index);
    file_size_ = files.file_size(index);
    piece_length_ = files.piece_length();
    TORRVIEW_LOG_INFO("torrent stream=" << stream_id_ << " opened file=" << file_index
                                        << " offset=" << file_offset_
                                        << " size=" << file_size_);
  }

  std::int64_t read(char* buffer, std::uint64_t bytes) override {
    if (buffer == nullptr || bytes == 0) {
      TORRVIEW_LOG_DEBUG("torrent stream=" << stream_id_
                                           << " read noop buffer=" << (buffer != nullptr)
                                           << " bytes=" << bytes);
      return 0;
    }

    const std::int64_t position = current_position();
    if (position >= file_size_) {
      TORRVIEW_LOG_DEBUG("torrent stream=" << stream_id_ << " read eof position=" << position
                                           << " size=" << file_size_);
      return 0;
    }

    const std::int64_t torrent_offset = file_offset_ + position;
    const lt::piece_index_t piece(static_cast<int>(torrent_offset / piece_length_));
    const int piece_offset = static_cast<int>(torrent_offset % piece_length_);
    const int piece_size = info_->piece_size(piece);
    const std::int64_t file_remaining = file_size_ - position;
    const std::int64_t piece_remaining = piece_size - piece_offset;
    const std::size_t to_copy = static_cast<std::size_t>(
        std::min<std::int64_t>({static_cast<std::int64_t>(bytes), file_remaining,
                                piece_remaining}));

    if (to_copy == 0) {
      return 0;
    }

    if (scheduler_ != nullptr) {
      scheduler_->schedule_for_read(position, bytes);
    }

    std::vector<char> piece_data;
    if (!wait_for_piece(piece, piece_data)) {
      TORRVIEW_LOG_WARNING("torrent stream=" << stream_id_
                                             << " read failed waiting for piece="
                                             << static_cast<int>(piece)
                                             << " position=" << position);
      return -1;
    }

    if (piece_offset < 0 ||
        piece_offset + static_cast<int>(to_copy) > static_cast<int>(piece_data.size())) {
      set_error("Torrent piece read returned too few bytes");
      return -1;
    }

    std::memcpy(buffer, piece_data.data() + piece_offset, to_copy);
    {
      std::lock_guard lock(mutex_);
      position_ += static_cast<std::int64_t>(to_copy);
      trim_piece_cache_locked();
    }
    return static_cast<std::int64_t>(to_copy);
  }

  std::int64_t seek(std::int64_t offset) override {
    if (offset < 0 || offset > file_size_) {
      TORRVIEW_LOG_WARNING("torrent stream=" << stream_id_ << " invalid seek offset=" << offset
                                             << " size=" << file_size_);
      return -1;
    }

    {
      std::lock_guard lock(mutex_);
      position_ = offset;
      cancelled_ = false;
      error_.clear();
      trim_piece_cache_locked();
    }

    if (scheduler_ != nullptr) {
      scheduler_->on_seek(offset);
    }

    TORRVIEW_LOG_INFO("torrent stream=" << stream_id_ << " seek offset=" << offset);
    return offset;
  }

  std::int64_t size() const override { return file_size_; }

  void cancel() override {
    std::lock_guard lock(mutex_);
    cancelled_ = true;
    condition_.notify_all();
    TORRVIEW_LOG_DEBUG("torrent stream=" << stream_id_ << " cancelled");
  }

  bool owns(const lt::torrent_handle& handle) const {
    return handle_.is_valid() && handle == handle_;
  }

  void on_piece_finished(lt::piece_index_t piece) {
    if (scheduler_ != nullptr) {
      scheduler_->on_piece_finished(piece);
    }

    bool requested = false;
    {
      std::lock_guard lock(mutex_);
      requested = requested_pieces_.find(static_cast<int>(piece)) != requested_pieces_.end();
    }

    if (requested && handle_.is_valid()) {
      try {
        handle_.read_piece(piece);
        TORRVIEW_LOG_DEBUG("torrent stream=" << stream_id_
                                             << " read_piece requested from finished alert piece="
                                             << static_cast<int>(piece));
      } catch (const std::exception& error) {
        set_error(error.what());
      }
    }
  }

  void on_read_piece(lt::piece_index_t piece, const char* data, int size,
                     const lt::error_code& error) {
    if (scheduler_ != nullptr) {
      scheduler_->on_read_piece(piece, error);
    }

    const int piece_key = static_cast<int>(piece);
    bool requested = false;
    bool rearm_request = false;
    {
      std::lock_guard lock(mutex_);
      requested = requested_pieces_.find(piece_key) != requested_pieces_.end();
      if (!requested && error) {
        TORRVIEW_LOG_DEBUG("torrent stream=" << stream_id_
                                             << " ignored unrequested read_piece error piece="
                                             << piece_key << " error=" << error.message());
        return;
      }
      if (!requested && data == nullptr) {
        TORRVIEW_LOG_DEBUG("torrent stream=" << stream_id_
                                             << " ignored unrequested empty read_piece alert piece="
                                             << piece_key);
        return;
      }
      if (requested && !is_operation_canceled(error)) {
        requested_pieces_.erase(piece_key);
      }
      if (error) {
        if (is_operation_canceled(error)) {
          rearm_request = requested && !cancelled_;
        } else {
          error_ = "Torrent piece read failed: " + error.message();
        }
      } else if (data == nullptr || size <= 0) {
        if (requested) {
          requested_pieces_.erase(piece_key);
          error_ = "Torrent piece read returned no data";
        } else {
          TORRVIEW_LOG_DEBUG("torrent stream=" << stream_id_
                                               << " ignored unrequested empty read_piece alert piece="
                                               << piece_key);
          return;
        }
      } else {
        if (requested) {
          pieces_[piece_key] = std::vector<char>(data, data + size);
        } else {
          TORRVIEW_LOG_DEBUG("torrent stream=" << stream_id_
                                               << " ignored unrequested read_piece data piece="
                                               << piece_key << " size=" << size);
          return;
        }
      }
    }
    if (rearm_request) {
      TORRVIEW_LOG_DEBUG("torrent stream=" << stream_id_
                                           << " rearming canceled read_piece request piece="
                                           << piece_key);
      request_piece(piece);
      return;
    }
    if (error) {
      TORRVIEW_LOG_WARNING("torrent stream=" << stream_id_ << " read_piece alert failed piece="
                                             << static_cast<int>(piece)
                                             << " error=" << error.message());
    } else {
      TORRVIEW_LOG_DEBUG("torrent stream=" << stream_id_ << " read_piece alert piece="
                                           << static_cast<int>(piece) << " size=" << size);
    }
    condition_.notify_all();
  }

private:
  std::int64_t current_position() const {
    std::lock_guard lock(mutex_);
    return position_;
  }

  bool wait_for_piece(lt::piece_index_t piece, std::vector<char>& data) {
    const int piece_key = static_cast<int>(piece);
    bool should_request = false;
    {
      std::lock_guard lock(mutex_);
      if (cancelled_) {
        TORRVIEW_LOG_DEBUG("torrent stream=" << stream_id_
                                             << " wait aborted by cancellation piece="
                                             << piece_key);
        return false;
      }
      if (!error_.empty()) {
        TORRVIEW_LOG_WARNING("torrent stream=" << stream_id_ << " wait aborted by error piece="
                                               << piece_key << " error=" << error_);
        return false;
      }
      if (auto found = pieces_.find(piece_key); found != pieces_.end()) {
        data = found->second;
        return true;
      }
      should_request = requested_pieces_.insert(piece_key).second;
    }

    if (should_request) {
      request_piece(piece);
    }

    std::unique_lock lock(mutex_);
    condition_.wait(lock, [&] {
      return cancelled_ || !error_.empty() || pieces_.find(piece_key) != pieces_.end();
    });

    if (cancelled_ || !error_.empty()) {
      TORRVIEW_LOG_WARNING("torrent stream=" << stream_id_ << " wait ended without data piece="
                                             << piece_key << " cancelled=" << cancelled_
                                             << " error=" << error_);
      return false;
    }

    data = pieces_.at(piece_key);
    return true;
  }

  void request_piece(lt::piece_index_t piece) {
    if (!handle_.is_valid()) {
      set_error("Torrent handle is not available");
      return;
    }

    try {
      if (scheduler_ != nullptr) {
        scheduler_->request_urgent_piece(piece);
      }
      handle_.piece_priority(piece, lt::top_priority);
      if (handle_.have_piece(piece)) {
        handle_.read_piece(piece);
        TORRVIEW_LOG_DEBUG("torrent stream=" << stream_id_
                                             << " request_piece have_piece read_piece piece="
                                             << static_cast<int>(piece));
      } else {
        handle_.set_piece_deadline(piece, 0, lt::torrent_handle::alert_when_available);
        TORRVIEW_LOG_DEBUG("torrent stream=" << stream_id_
                                             << " request_piece deadline piece="
                                             << static_cast<int>(piece));
      }
    } catch (const std::exception& error) {
      set_error(error.what());
    }
  }

  void set_error(std::string message) {
    std::lock_guard lock(mutex_);
    error_ = std::move(message);
    TORRVIEW_LOG_ERROR("torrent stream=" << stream_id_ << " error=" << error_);
    condition_.notify_all();
  }

  void trim_piece_cache_locked() {
    const std::int64_t torrent_offset = file_offset_ + position_;
    const int current_piece = static_cast<int>(torrent_offset / piece_length_);
    for (auto it = pieces_.begin(); it != pieces_.end();) {
      if (it->first + 1 < current_piece) {
        it = pieces_.erase(it);
      } else {
        ++it;
      }
    }
  }

  lt::torrent_handle handle_;
  std::shared_ptr<const lt::torrent_info> info_;
  std::shared_ptr<StreamingScheduler> scheduler_;
  std::uint64_t stream_id_ = 0;
  std::int64_t file_offset_ = 0;
  std::int64_t file_size_ = 0;
  int piece_length_ = 1;
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::int64_t position_ = 0;
  bool cancelled_ = false;
  std::string error_;
  std::map<int, std::vector<char>> pieces_;
  std::set<int> requested_pieces_;
};

class TorrentService::Impl final {
public:
  Impl() : session_(make_session_params()) {
    snapshot_.available = true;
    snapshot_.state = TorrentLoadState::idle;
    snapshot_.status = "Ready";
    snapshot_.cache_limit_mib = cache_policy_.limit_mib();
    snapshot_.cache_bytes_limit = cache_policy_.max_bytes;
    set_memory_disk_cache_policy(cache_policy_);
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

      if (auto* piece_finished = lt::alert_cast<lt::piece_finished_alert>(alert)) {
        notify_piece_finished(piece_finished->handle, piece_finished->piece_index);
        continue;
      }

      if (auto* read_piece = lt::alert_cast<lt::read_piece_alert>(alert)) {
        notify_read_piece(read_piece->handle, read_piece->piece, read_piece->buffer.get(),
                          read_piece->size, read_piece->error);
        continue;
      }

      if (auto* torrent_error = lt::alert_cast<lt::torrent_error_alert>(alert)) {
        fail("Torrent error: " + torrent_error->error.message());
        continue;
      }

      if (auto* tracker_reply = lt::alert_cast<lt::tracker_reply_alert>(alert)) {
        TORRVIEW_LOG_INFO("tracker replied peers=" << tracker_reply->num_peers);
        note_network_event("Tracker replied: " + std::to_string(tracker_reply->num_peers) +
                           " peers");
        continue;
      }

      if (auto* tracker_warning = lt::alert_cast<lt::tracker_warning_alert>(alert)) {
        TORRVIEW_LOG_WARNING("tracker warning: " << tracker_warning->warning_message());
        note_network_event("Tracker warning: " +
                           compact_alert_detail(tracker_warning->warning_message()));
        continue;
      }

      if (auto* tracker_error = lt::alert_cast<lt::tracker_error_alert>(alert)) {
        const char* failure = tracker_error->failure_reason();
        std::string detail =
            failure != nullptr && failure[0] != '\0' ? failure : tracker_error->error.message();
        TORRVIEW_LOG_WARNING("tracker error: " << detail << " message="
                                               << compact_alert_detail(tracker_error->message()));
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

  void set_cache_limit_mib(int limit_mib) {
    cache_policy_ = CachePolicy::from_limit_mib(limit_mib);
    set_memory_disk_cache_policy(cache_policy_);
    snapshot_.cache_limit_mib = cache_policy_.limit_mib();
    snapshot_.cache_bytes_limit = cache_policy_.max_bytes;
    TORRVIEW_LOG_INFO("torrent cache limit set to " << snapshot_.cache_limit_mib << " MiB");
    if (active_scheduler_ != nullptr) {
      active_scheduler_->set_policy(cache_policy_);
    }
  }

  const TorrentSnapshot& snapshot() const { return snapshot_; }

  std::string stream_uri_for_file(int file_index) const {
    if (!handle_.is_valid() || !snapshot_.has_metadata || file_index < 0) {
      return {};
    }

    return "torrview://torrent/" + std::to_string(stream_generation_) + "/" +
           std::to_string(file_index);
  }

  class StreamHandle final : public TorrentStreamReader {
  public:
    StreamHandle(std::shared_ptr<TorrentPieceStream> stream, Impl& owner)
        : stream_(std::move(stream)), owner_(owner) {}

    ~StreamHandle() override {
      stream_->cancel();
      owner_.forget_stream(stream_);
    }

    std::int64_t read(char* buffer, std::uint64_t bytes) override {
      return stream_->read(buffer, bytes);
    }

    std::int64_t seek(std::int64_t offset) override { return stream_->seek(offset); }

    std::int64_t size() const override { return stream_->size(); }

    void cancel() override { stream_->cancel(); }

  private:
    std::shared_ptr<TorrentPieceStream> stream_;
    Impl& owner_;
  };

  std::unique_ptr<TorrentStreamReader> open_torrent_stream(const std::string& uri,
                                                           std::string& error) {
    const auto parsed = parse_stream_uri(uri);
    if (!parsed.has_value()) {
      error = "Invalid torrview stream URI";
      TORRVIEW_LOG_ERROR("torrent stream open failed: " << error << " uri=" << uri);
      return {};
    }

    const auto [generation, file_index] = *parsed;
    if (generation != stream_generation_) {
      error = "Torrent stream URI is stale";
      TORRVIEW_LOG_ERROR("torrent stream open failed: " << error << " uri_generation="
                                                        << generation << " current_generation="
                                                        << stream_generation_);
      return {};
    }

    if (!handle_.is_valid()) {
      error = "Torrent is not loaded";
      TORRVIEW_LOG_ERROR("torrent stream open failed: " << error);
      return {};
    }

    auto info = handle_.torrent_file();
    if (info == nullptr || !info->is_valid()) {
      error = "Torrent metadata is not available";
      TORRVIEW_LOG_ERROR("torrent stream open failed: " << error);
      return {};
    }

    if (file_index < 0 || file_index >= info->num_files()) {
      error = "Torrent file index is out of range";
      TORRVIEW_LOG_ERROR("torrent stream open failed: " << error << " file_index=" << file_index
                                                        << " file_count=" << info->num_files());
      return {};
    }

    cancel_streams();

    const std::uint64_t stream_id = next_stream_id_++;
    TORRVIEW_LOG_INFO("torrent stream=" << stream_id << " creating for file=" << file_index
                                        << " generation=" << generation);
    auto scheduler =
        std::make_shared<StreamingScheduler>(handle_, info, file_index, cache_policy_, stream_id);
    scheduler->prepare_initial_priorities();
    active_scheduler_ = scheduler;

    auto stream =
        std::make_shared<TorrentPieceStream>(handle_, info, file_index, scheduler, stream_id);
    {
      std::lock_guard lock(streams_mutex_);
      active_streams_.push_back(stream);
    }
    return std::unique_ptr<TorrentStreamReader>(new StreamHandle(std::move(stream), *this));
  }

private:
  void clear_current() {
    cancel_streams();
    if (handle_.is_valid()) {
      session_.remove_torrent(handle_);
      handle_ = {};
    }
    ++stream_generation_;

    snapshot_ = {};
    snapshot_.available = true;
    snapshot_.state = TorrentLoadState::idle;
    snapshot_.status = "Ready";
    snapshot_.cache_limit_mib = cache_policy_.limit_mib();
    snapshot_.cache_bytes_limit = cache_policy_.max_bytes;
    active_scheduler_.reset();
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

  void notify_piece_finished(const lt::torrent_handle& handle, lt::piece_index_t piece) {
    std::vector<std::shared_ptr<TorrentPieceStream>> streams = active_stream_snapshot();
    for (const auto& stream : streams) {
      if (stream->owns(handle)) {
        stream->on_piece_finished(piece);
      }
    }
  }

  void notify_read_piece(const lt::torrent_handle& handle, lt::piece_index_t piece,
                         const char* data, int size, const lt::error_code& error) {
    std::vector<std::shared_ptr<TorrentPieceStream>> streams = active_stream_snapshot();
    for (const auto& stream : streams) {
      if (stream->owns(handle)) {
        stream->on_read_piece(piece, data, size, error);
      }
    }
  }

  std::vector<std::shared_ptr<TorrentPieceStream>> active_stream_snapshot() {
    std::lock_guard lock(streams_mutex_);
    std::vector<std::shared_ptr<TorrentPieceStream>> streams;
    for (auto it = active_streams_.begin(); it != active_streams_.end();) {
      if (auto stream = it->lock()) {
        streams.push_back(std::move(stream));
        ++it;
      } else {
        it = active_streams_.erase(it);
      }
    }
    return streams;
  }

  void forget_stream(const std::shared_ptr<TorrentPieceStream>& stream) {
    std::lock_guard lock(streams_mutex_);
    for (auto it = active_streams_.begin(); it != active_streams_.end();) {
      auto current = it->lock();
      if (!current || current == stream) {
        it = active_streams_.erase(it);
      } else {
        ++it;
      }
    }
  }

  void cancel_streams() {
    std::vector<std::shared_ptr<TorrentPieceStream>> streams = active_stream_snapshot();
    for (const auto& stream : streams) {
      stream->cancel();
    }
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
    snapshot_.cache_limit_mib = cache_policy_.limit_mib();
    snapshot_.cache_bytes_limit = cache_policy_.max_bytes;
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

    if (active_scheduler_ != nullptr) {
      const std::string blocked_reason = log_playback_status(status);
      active_scheduler_->check_stalled();
      SchedulerSnapshot scheduler = active_scheduler_->snapshot(snapshot_.peers,
                                                                snapshot_.download_rate);
      snapshot_.active_file_index = scheduler.active_file_index;
      snapshot_.cache_limit_mib = scheduler.cache_limit_mib;
      snapshot_.cache_bytes_used = scheduler.cache_bytes_used;
      snapshot_.cache_bytes_limit = scheduler.cache_bytes_limit;
      snapshot_.buffering = scheduler.buffering;
      snapshot_.stalled = scheduler.stalled;
      snapshot_.buffer_progress = scheduler.buffer_progress;
      snapshot_.buffer_status = std::move(scheduler.buffer_status);
      snapshot_.piece_states = std::move(scheduler.piece_states);
      if (!blocked_reason.empty()) {
        snapshot_.buffering = true;
        snapshot_.stalled = true;
        snapshot_.buffer_status = blocked_reason;
        snapshot_.status = blocked_reason;
      } else if (snapshot_.state == TorrentLoadState::ready && !snapshot_.buffer_status.empty()) {
        snapshot_.status = snapshot_.buffer_status;
      }
    }
  }

  std::string log_playback_status(const lt::torrent_status& status) {
    if (active_scheduler_ == nullptr) {
      return {};
    }

    const SchedulerDiagnostics scheduler = active_scheduler_->diagnostics();
    std::vector<int> availability;
    int urgent_availability = -1;
    int available_window_pieces = 0;
    int unavailable_window_pieces = 0;
    lt::download_priority_t urgent_priority = lt::dont_download;
    bool urgent_in_queue = false;
    int urgent_requested_blocks = 0;
    int urgent_finished_blocks = 0;

    try {
      handle_.piece_availability(availability);
      if (scheduler.urgent_piece >= 0 &&
          scheduler.urgent_piece < static_cast<int>(availability.size())) {
        urgent_availability = availability[static_cast<std::size_t>(scheduler.urgent_piece)];
      }
      for (int piece = scheduler.active_window.first_piece;
           piece < scheduler.active_window.end_piece &&
           piece < static_cast<int>(availability.size());
           ++piece) {
        if (availability[static_cast<std::size_t>(piece)] > 0) {
          ++available_window_pieces;
        } else {
          ++unavailable_window_pieces;
        }
      }
      if (scheduler.urgent_piece >= 0) {
        urgent_priority = handle_.piece_priority(lt::piece_index_t(scheduler.urgent_piece));
      }
    } catch (const std::exception& error) {
      TORRVIEW_LOG_WARNING("torrent availability query failed: " << error.what());
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - last_playback_status_log_ < std::chrono::seconds(2)) {
      if (urgent_availability == 0 && status.num_seeds == 0 && status.list_seeds == 0) {
        return "Waiting for first piece: no connected peer advertises it";
      }
      return {};
    }
    last_playback_status_log_ = now;

    lt::torrent_flags_t flags;
    std::vector<lt::partial_piece_info> queue;
    try {
      flags = handle_.flags();
      queue = handle_.get_download_queue();
    } catch (const std::exception& error) {
      TORRVIEW_LOG_WARNING("torrent playback status query failed: " << error.what());
      return {};
    }

    int queued_blocks = 0;
    int requested_blocks = 0;
    int writing_blocks = 0;
    int finished_blocks = 0;
    for (const lt::partial_piece_info& piece : queue) {
      queued_blocks += piece.blocks_in_piece;
      requested_blocks += piece.requested;
      writing_blocks += piece.writing;
      finished_blocks += piece.finished;
      if (static_cast<int>(piece.piece_index) == scheduler.urgent_piece) {
        urgent_in_queue = true;
        urgent_requested_blocks = piece.requested;
        urgent_finished_blocks = piece.finished;
      }
    }

    TORRVIEW_LOG_DEBUG("torrent playback status state=" << libtorrent_state_label(status.state)
                                                        << " paused="
                                                        << bool(flags & lt::torrent_flags::paused)
                                                        << " upload_mode="
                                                        << bool(flags & lt::torrent_flags::upload_mode)
                                                        << " peers=" << status.num_peers
                                                        << " seeds=" << status.num_seeds
                                                        << " connections=" << status.num_connections
                                                        << " candidates="
                                                        << status.connect_candidates
                                                        << " list_peers=" << status.list_peers
                                                        << " list_seeds=" << status.list_seeds
                                                        << " down=" << status.download_rate
                                                        << " payload_down="
                                                        << status.download_payload_rate
                                                        << " wanted_done="
                                                        << status.total_wanted_done << '/'
                                                        << status.total_wanted
                                                        << " queue_pieces=" << queue.size()
                                                        << " queue_blocks=" << queued_blocks
                                                        << " requested_blocks="
                                                        << requested_blocks
                                                        << " writing_blocks=" << writing_blocks
                                                        << " finished_blocks=" << finished_blocks
                                                        << " urgent_piece="
                                                        << scheduler.urgent_piece
                                                        << " urgent_availability="
                                                        << urgent_availability
                                                        << " urgent_priority="
                                                        << static_cast<int>(urgent_priority)
                                                        << " urgent_in_queue=" << urgent_in_queue
                                                        << " urgent_requested_blocks="
                                                        << urgent_requested_blocks
                                                        << " urgent_finished_blocks="
                                                        << urgent_finished_blocks
                                                        << " available_window_pieces="
                                                        << available_window_pieces
                                                        << " unavailable_window_pieces="
                                                        << unavailable_window_pieces);

    if (urgent_availability == 0 && status.num_seeds == 0 && status.list_seeds == 0) {
      if (now - last_unavailable_piece_warning_ >= std::chrono::seconds(10)) {
        last_unavailable_piece_warning_ = now;
        TORRVIEW_LOG_WARNING("torrent cannot start stream yet: urgent piece "
                             << scheduler.urgent_piece
                             << " is unavailable from connected peers and no seeds are known");
      }
      return "Waiting for first piece: no connected peer advertises it";
    }
    if (urgent_availability > 0 && !urgent_in_queue && urgent_priority > lt::dont_download) {
      TORRVIEW_LOG_WARNING("torrent urgent piece is available but not in download queue: piece="
                           << scheduler.urgent_piece << " availability=" << urgent_availability
                           << " priority=" << static_cast<int>(urgent_priority));
    }
    return {};
  }

  lt::session session_;
  lt::torrent_handle handle_;
  TorrentSnapshot snapshot_;
  CachePolicy cache_policy_;
  std::shared_ptr<StreamingScheduler> active_scheduler_;
  std::uint64_t stream_generation_ = 1;
  std::uint64_t next_stream_id_ = 1;
  std::mutex streams_mutex_;
  std::vector<std::weak_ptr<TorrentPieceStream>> active_streams_;
  std::string last_network_event_;
  std::chrono::steady_clock::time_point last_status_poll_;
  std::chrono::steady_clock::time_point last_playback_status_log_;
  std::chrono::steady_clock::time_point last_unavailable_piece_warning_;
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

  void set_cache_limit_mib(int limit_mib) {
    snapshot_.cache_limit_mib = CachePolicy::from_limit_mib(limit_mib).limit_mib();
  }

  void reset() {
    snapshot_ = {};
    snapshot_.available = false;
    snapshot_.state = TorrentLoadState::idle;
    snapshot_.status = "This build was configured without libtorrent";
    snapshot_.cache_limit_mib = 256;
    snapshot_.cache_bytes_limit = 256LL * 1024LL * 1024LL;
  }

  const TorrentSnapshot& snapshot() const { return snapshot_; }

  std::string stream_uri_for_file(int) const { return {}; }

  std::unique_ptr<TorrentStreamReader> open_torrent_stream(const std::string&, std::string& error) {
    error = "Torrent streaming requires a build with libtorrent-rasterbar.";
    return {};
  }

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

void TorrentService::set_cache_limit_mib(int limit_mib) { impl_->set_cache_limit_mib(limit_mib); }

const TorrentSnapshot& TorrentService::snapshot() const { return impl_->snapshot(); }

std::string TorrentService::stream_uri_for_file(int file_index) const {
  return impl_->stream_uri_for_file(file_index);
}

std::unique_ptr<TorrentStreamReader> TorrentService::open_torrent_stream(const std::string& uri,
                                                                         std::string& error) {
  return impl_->open_torrent_stream(uri, error);
}

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
