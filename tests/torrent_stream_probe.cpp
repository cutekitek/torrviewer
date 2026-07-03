#include "cache_policy.hpp"
#include "logger.hpp"
#include "memory_disk_io.hpp"

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/download_priority.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
namespace lt = libtorrent;

bool has_video_extension(std::string_view path) {
  std::filesystem::path file_path{std::string(path)};
  std::string extension = file_path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return extension == ".mkv" || extension == ".mp4" || extension == ".webm" ||
         extension == ".avi" || extension == ".mov" || extension == ".m4v" ||
         extension == ".ts" || extension == ".m2ts";
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

lt::settings_pack make_settings(std::string_view listen_interfaces) {
  lt::settings_pack settings;
  settings.set_str(lt::settings_pack::user_agent, "TorrviewProbe");
  settings.set_str(lt::settings_pack::listen_interfaces, std::string(listen_interfaces));
  settings.set_bool(lt::settings_pack::enable_dht, true);
  settings.set_bool(lt::settings_pack::enable_lsd, false);
  settings.set_bool(lt::settings_pack::enable_upnp, false);
  settings.set_bool(lt::settings_pack::enable_natpmp, false);
  settings.set_int(lt::settings_pack::connections_limit, 80);
  settings.set_int(lt::settings_pack::upload_rate_limit,
                   env_int_or_default("TORRVIEW_PROBE_UPLOAD_LIMIT", 16 * 1024));
  settings.set_int(lt::settings_pack::unchoke_slots_limit,
                   env_int_or_default("TORRVIEW_PROBE_UNCHOKE_SLOTS", 4));
  settings.set_int(lt::settings_pack::num_optimistic_unchoke_slots,
                   env_int_or_default("TORRVIEW_PROBE_OPTIMISTIC_UNCHOKE_SLOTS", 1));
  settings.set_int(lt::settings_pack::alert_mask,
                   lt::alert_category::error | lt::alert_category::status |
                       lt::alert_category::tracker | lt::alert_category::dht |
                       lt::alert_category::peer | lt::alert_category::storage |
                       lt::alert_category::piece_progress | lt::alert_category::connect);
  return settings;
}

int largest_video_file(const lt::torrent_info& info) {
  const lt::file_storage& files = info.files();
  int selected = -1;
  std::int64_t selected_size = -1;
  for (int index = 0; index < files.num_files(); ++index) {
    const lt::file_index_t file_index(index);
    if (files.pad_file_at(file_index) || !has_video_extension(files.file_path(file_index))) {
      continue;
    }
    const std::int64_t size = files.file_size(file_index);
    if (size > selected_size) {
      selected = index;
      selected_size = size;
    }
  }
  return selected;
}

std::optional<int> parse_int(const char* value) {
  if (value == nullptr) {
    return std::nullopt;
  }
  try {
    return std::stoi(value);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

struct QueueSummary {
  bool urgent_in_queue = false;
  int queue_pieces = 0;
  int requested_blocks = 0;
  int writing_blocks = 0;
  int finished_blocks = 0;
  int urgent_requested_blocks = 0;
  int urgent_finished_blocks = 0;
};

struct PeerSummary {
  int peers = 0;
  int connecting = 0;
  int handshake = 0;
  int seeds = 0;
  int remote_choked = 0;
  int interesting = 0;
  int upload_only = 0;
  int with_pieces = 0;
  int total_reported_pieces = 0;
};

PeerSummary summarize_peers(const std::vector<lt::peer_info>& peers) {
  PeerSummary summary;
  summary.peers = static_cast<int>(peers.size());
  for (const lt::peer_info& peer : peers) {
    if (bool(peer.flags & lt::peer_info::connecting)) {
      ++summary.connecting;
    }
    if (bool(peer.flags & lt::peer_info::handshake)) {
      ++summary.handshake;
    }
    if (bool(peer.flags & lt::peer_info::seed)) {
      ++summary.seeds;
    }
    if (bool(peer.flags & lt::peer_info::remote_choked)) {
      ++summary.remote_choked;
    }
    if (bool(peer.flags & lt::peer_info::interesting)) {
      ++summary.interesting;
    }
    if (bool(peer.flags & lt::peer_info::upload_only)) {
      ++summary.upload_only;
    }
    if (peer.num_pieces > 0) {
      ++summary.with_pieces;
      summary.total_reported_pieces += peer.num_pieces;
    }
  }
  return summary;
}

enum class StorageMode {
  memory,
  disk,
};

std::string_view storage_mode_name(StorageMode mode) {
  switch (mode) {
  case StorageMode::memory:
    return "memory";
  case StorageMode::disk:
    return "disk";
  }
  return "unknown";
}

std::optional<StorageMode> parse_storage_mode(std::string_view value) {
  if (value == "memory") {
    return StorageMode::memory;
  }
  if (value == "disk") {
    return StorageMode::disk;
  }
  return std::nullopt;
}

QueueSummary summarize_queue(const std::vector<lt::partial_piece_info>& queue,
                             int urgent_piece) {
  QueueSummary summary;
  summary.queue_pieces = static_cast<int>(queue.size());
  for (const lt::partial_piece_info& piece : queue) {
    summary.requested_blocks += piece.requested;
    summary.writing_blocks += piece.writing;
    summary.finished_blocks += piece.finished;
    if (static_cast<int>(piece.piece_index) == urgent_piece) {
      summary.urgent_in_queue = true;
      summary.urgent_requested_blocks = piece.requested;
      summary.urgent_finished_blocks = piece.finished;
    }
  }
  return summary;
}

void print_usage(const char* program) {
  std::cerr << "usage: " << program
            << " <torrent-file> [file-index] [seconds] [memory|disk]\n";
}

} // namespace

int main(int argc, char** argv) {
  const char* torrent_path = argc >= 2 ? argv[1] : std::getenv("TORRVIEW_PROBE_TORRENT");
  if (torrent_path == nullptr || torrent_path[0] == '\0') {
    print_usage(argv[0]);
    return 77;
  }

  const int duration_seconds = argc >= 4 ? parse_int(argv[3]).value_or(30) : 30;
  const char* listen_env = std::getenv("TORRVIEW_PROBE_LISTEN");
  const std::string listen_interfaces =
      listen_env != nullptr && listen_env[0] != '\0' ? listen_env : "0.0.0.0:0,[::]:0";
  StorageMode storage_mode = StorageMode::memory;
  if (argc >= 5) {
    const std::optional<StorageMode> parsed_mode = parse_storage_mode(argv[4]);
    if (!parsed_mode.has_value()) {
      print_usage(argv[0]);
      return 1;
    }
    storage_mode = *parsed_mode;
  } else if (const char* env_mode = std::getenv("TORRVIEW_PROBE_STORAGE");
             env_mode != nullptr && env_mode[0] != '\0') {
    const std::optional<StorageMode> parsed_mode = parse_storage_mode(env_mode);
    if (!parsed_mode.has_value()) {
      std::cerr << "invalid TORRVIEW_PROBE_STORAGE: " << env_mode << '\n';
      print_usage(argv[0]);
      return 1;
    }
    storage_mode = *parsed_mode;
  }

  lt::error_code error;
  auto info = std::make_shared<lt::torrent_info>(torrent_path, error);
  if (error) {
    std::cerr << "failed to load torrent: " << error.message() << '\n';
    return 1;
  }

  int selected_file = argc >= 3 ? parse_int(argv[2]).value_or(-1) : -1;
  if (selected_file < 0) {
    selected_file = largest_video_file(*info);
  }
  if (selected_file < 0 || selected_file >= info->num_files()) {
    std::cerr << "invalid file index. torrent has " << info->num_files() << " files\n";
    return 1;
  }

  const lt::file_storage& files = info->files();
  const lt::file_index_t file_index(selected_file);
  const std::int64_t file_offset = files.file_offset(file_index);
  const std::int64_t file_size = files.file_size(file_index);
  const int piece_length = files.piece_length();
  const int urgent_piece = static_cast<int>(file_offset / piece_length);
  const torrview::CachePolicy policy;
  torrview::PieceWindow window = torrview::compute_retained_piece_window(
      file_offset, static_cast<int>(files.end_piece()), piece_length, policy);
  const int file_end_piece =
      static_cast<int>((file_offset + file_size + piece_length - 1) / piece_length);
  const int file_first_piece = urgent_piece;
  window.first_piece = std::clamp(window.first_piece, urgent_piece, file_end_piece);
  window.end_piece = std::clamp(window.end_piece, window.first_piece, file_end_piece);

  std::cout << "torrent: " << info->name() << '\n';
  std::cout << "storage=" << storage_mode_name(storage_mode) << '\n';
  std::cout << "private=" << info->priv() << '\n';
  std::cout << "listen_interfaces=" << listen_interfaces << '\n';
  for (const lt::announce_entry& tracker : info->trackers()) {
    std::cout << "tracker tier=" << tracker.tier << " url=" << tracker.url << '\n';
  }
  std::cout << "selected_file=" << selected_file << " path=" << files.file_path(file_index)
            << " size=" << file_size << " offset=" << file_offset
            << " piece_length=" << piece_length << " urgent_piece=" << urgent_piece
            << " window=[" << window.first_piece << ',' << window.end_piece << ")\n";

  lt::session_params session_params(make_settings(listen_interfaces));
  if (storage_mode == StorageMode::memory) {
    session_params.disk_io_constructor = torrview::create_memory_disk_io;
  }
  lt::session session(std::move(session_params));

  lt::add_torrent_params params;
  params.ti = info;
  params.save_path = (std::filesystem::temp_directory_path() / "torrview-probe").string();
  params.flags &= ~lt::torrent_flags::auto_managed;
  params.flags &= ~lt::torrent_flags::paused;
  params.flags |= lt::torrent_flags::default_dont_download;
  params.file_priorities.assign(static_cast<std::size_t>(info->num_files()), lt::dont_download);
  params.max_connections = 80;

  lt::torrent_handle handle = session.add_torrent(std::move(params), error);
  if (error) {
    std::cerr << "failed to add torrent: " << error.message() << '\n';
    return 1;
  }

  handle.unset_flags(lt::torrent_flags::paused | lt::torrent_flags::upload_mode);
  handle.resume();
  handle.force_reannounce();
  handle.clear_piece_deadlines();

  std::vector<lt::download_priority_t> file_priorities(static_cast<std::size_t>(info->num_files()),
                                                       lt::dont_download);
  file_priorities[static_cast<std::size_t>(selected_file)] = lt::top_priority;
  handle.prioritize_files(file_priorities);

  for (int piece = urgent_piece; piece < window.end_piece; ++piece) {
    const lt::piece_index_t piece_index(piece);
    handle.piece_priority(piece_index, lt::top_priority);
    handle.set_piece_deadline(piece_index, (piece - urgent_piece) * 150);
  }
  for (int piece = file_first_piece; piece < file_end_piece; ++piece) {
    if (piece >= window.first_piece && piece < window.end_piece) {
      continue;
    }
    handle.piece_priority(lt::piece_index_t(piece), lt::low_priority);
  }
  handle.set_piece_deadline(lt::piece_index_t(urgent_piece), 0,
                            lt::torrent_handle::alert_when_available);

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(duration_seconds);
  bool urgent_seen_available = false;
  bool urgent_seen_queue = false;
  while (std::chrono::steady_clock::now() < deadline) {
    std::vector<lt::alert*> alerts;
    session.pop_alerts(&alerts);
    for (lt::alert* alert : alerts) {
      if (auto* tracker = lt::alert_cast<lt::tracker_reply_alert>(alert)) {
        std::cout << "tracker_reply peers=" << tracker->num_peers << '\n';
      } else if (auto* tracker_error = lt::alert_cast<lt::tracker_error_alert>(alert)) {
        const char* reason = tracker_error->failure_reason();
        std::cout << "tracker_error error=" << tracker_error->error.message()
                  << " reason=" << (reason != nullptr ? reason : "")
                  << " message=" << tracker_error->message() << '\n';
      } else if (auto* piece_finished = lt::alert_cast<lt::piece_finished_alert>(alert)) {
        std::cout << "piece_finished piece=" << static_cast<int>(piece_finished->piece_index)
                  << '\n';
      } else if (auto* read_piece = lt::alert_cast<lt::read_piece_alert>(alert)) {
        std::cout << "read_piece piece=" << static_cast<int>(read_piece->piece)
                  << " error=" << read_piece->error.message() << " size=" << read_piece->size
                  << '\n';
      } else if (auto* peer_error = lt::alert_cast<lt::peer_error_alert>(alert)) {
        std::cout << "peer_error error=" << peer_error->error.message()
                  << " message=" << peer_error->message() << '\n';
      } else if (auto* disconnected = lt::alert_cast<lt::peer_disconnected_alert>(alert)) {
        std::cout << "peer_disconnected error=" << disconnected->error.message()
                  << " reason=" << static_cast<int>(disconnected->reason)
                  << " message=" << disconnected->message() << '\n';
      } else if (auto* connected = lt::alert_cast<lt::peer_connect_alert>(alert)) {
        std::cout << "peer_connect message=" << connected->message() << '\n';
      }
    }

    lt::torrent_status status = handle.status();
    std::vector<int> availability;
    handle.piece_availability(availability);
    int available_torrent = 0;
    for (int piece_availability : availability) {
      if (piece_availability > 0) {
        ++available_torrent;
      }
    }
    const int urgent_availability =
        urgent_piece >= 0 && urgent_piece < static_cast<int>(availability.size())
            ? availability[static_cast<std::size_t>(urgent_piece)]
            : -1;
    int available_window = 0;
    for (int piece = window.first_piece;
         piece < window.end_piece && piece < static_cast<int>(availability.size()); ++piece) {
      if (availability[static_cast<std::size_t>(piece)] > 0) {
        ++available_window;
      }
    }
    int available_file = 0;
    int first_available_file_piece = -1;
    for (int piece = file_first_piece;
         piece < file_end_piece && piece < static_cast<int>(availability.size()); ++piece) {
      if (availability[static_cast<std::size_t>(piece)] > 0) {
        ++available_file;
        if (first_available_file_piece < 0) {
          first_available_file_piece = piece;
        }
      }
    }
    const int file_piece_count = std::max(0, file_end_piece - file_first_piece);
    const std::int64_t first_available_file_offset =
        first_available_file_piece >= 0
            ? static_cast<std::int64_t>(first_available_file_piece - file_first_piece) *
                  piece_length
            : -1;

    std::vector<lt::partial_piece_info> queue = handle.get_download_queue();
    std::vector<lt::peer_info> peers;
    handle.get_peer_info(peers);
    const QueueSummary queue_summary = summarize_queue(queue, urgent_piece);
    const PeerSummary peer_summary = summarize_peers(peers);
    urgent_seen_available = urgent_seen_available || urgent_availability > 0;
    urgent_seen_queue = urgent_seen_queue || queue_summary.urgent_in_queue;

    std::cout << "status peers=" << status.num_peers << " seeds=" << status.num_seeds
              << " connections=" << status.num_connections
              << " candidates=" << status.connect_candidates
              << " peer_info=" << peer_summary.peers
              << " peer_connecting=" << peer_summary.connecting
              << " peer_handshake=" << peer_summary.handshake
              << " peer_seeds=" << peer_summary.seeds
              << " peer_choked=" << peer_summary.remote_choked
              << " peer_interesting=" << peer_summary.interesting
              << " peer_upload_only=" << peer_summary.upload_only
              << " peer_with_pieces=" << peer_summary.with_pieces
              << " peer_reported_pieces=" << peer_summary.total_reported_pieces
              << " list_peers=" << status.list_peers << " list_seeds=" << status.list_seeds
              << " down=" << status.download_rate
              << " payload_down=" << status.download_payload_rate
              << " wanted=" << status.total_wanted_done << '/' << status.total_wanted
              << " available_torrent=" << available_torrent << '/' << availability.size()
              << " urgent_availability=" << urgent_availability
              << " available_window=" << available_window << '/' << (window.end_piece - window.first_piece)
              << " available_file=" << available_file << '/' << file_piece_count
              << " first_available_file_offset=" << first_available_file_offset
              << " queue_pieces=" << queue_summary.queue_pieces
              << " urgent_in_queue=" << queue_summary.urgent_in_queue
              << " urgent_requested_blocks=" << queue_summary.urgent_requested_blocks
              << " requested_blocks=" << queue_summary.requested_blocks << '\n';

    if (status.total_wanted_done > 0 || queue_summary.urgent_finished_blocks > 0) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }

  if (!urgent_seen_available) {
    std::cout << "result: urgent piece was never advertised by connected peers\n";
  } else if (!urgent_seen_queue) {
    std::cout << "result: urgent piece became available but was never queued\n";
  } else {
    std::cout << "result: urgent piece was available and queued\n";
  }
  return 0;
}
