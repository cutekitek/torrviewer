#pragma once

#include "cache_policy.hpp"
#include "config.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#if defined(TORRVIEW_HAVE_LIBTORRENT)
#include <libtorrent/disk_interface.hpp>
#include <libtorrent/session_params.hpp>

namespace torrview {

struct MemoryDiskCacheStatus {
  std::int64_t bytes_used = 0;
  CachePolicy policy;
  bool has_retained_window = false;
  PieceWindow retained_window;
  int evicted_pieces = 0;
  std::vector<int> evicted_piece_indices;
  std::int64_t evicted_read_misses = 0;
  std::int64_t missing_read_misses = 0;
};

class MemoryDiskIO final : public libtorrent::disk_interface,
                           public libtorrent::buffer_allocator_interface {
public:
  explicit MemoryDiskIO(libtorrent::io_context& io_context);
  MemoryDiskIO(const MemoryDiskIO&) = delete;
  MemoryDiskIO& operator=(const MemoryDiskIO&) = delete;
  ~MemoryDiskIO() override;

  std::int64_t bytes_used() const;
  CachePolicy cache_policy() const;
  void set_cache_policy(CachePolicy policy);
  PieceWindow update_retained_window(libtorrent::storage_index_t storage,
                                     std::int64_t torrent_byte_offset);
  void set_retained_window(libtorrent::storage_index_t storage, PieceWindow window);
  void clear_retained_window(libtorrent::storage_index_t storage);
  MemoryDiskCacheStatus cache_status(libtorrent::storage_index_t storage) const;

  libtorrent::storage_holder new_torrent(libtorrent::storage_params const& params,
                                         std::shared_ptr<void> const& torrent) override;
  void remove_torrent(libtorrent::storage_index_t storage) override;

  void async_read(libtorrent::storage_index_t storage, libtorrent::peer_request const& request,
                  std::function<void(libtorrent::disk_buffer_holder,
                                     libtorrent::storage_error const&)> handler,
                  libtorrent::disk_job_flags_t flags = {}) override;
  bool async_write(libtorrent::storage_index_t storage, libtorrent::peer_request const& request,
                   char const* buffer, std::shared_ptr<libtorrent::disk_observer> observer,
                   std::function<void(libtorrent::storage_error const&)> handler,
                   libtorrent::disk_job_flags_t flags = {}) override;
  void async_hash(libtorrent::storage_index_t storage, libtorrent::piece_index_t piece,
                  libtorrent::span<libtorrent::sha256_hash> v2,
                  libtorrent::disk_job_flags_t flags,
                  std::function<void(libtorrent::piece_index_t, libtorrent::sha1_hash const&,
                                     libtorrent::storage_error const&)> handler) override;
  void async_hash2(libtorrent::storage_index_t storage, libtorrent::piece_index_t piece,
                   int offset, libtorrent::disk_job_flags_t flags,
                   std::function<void(libtorrent::piece_index_t, libtorrent::sha256_hash const&,
                                      libtorrent::storage_error const&)> handler) override;

  void async_move_storage(libtorrent::storage_index_t storage, std::string path,
                          libtorrent::move_flags_t flags,
                          std::function<void(libtorrent::status_t, std::string const&,
                                             libtorrent::storage_error const&)> handler) override;
  void async_release_files(libtorrent::storage_index_t storage,
                           std::function<void()> handler = std::function<void()>()) override;
  void async_check_files(libtorrent::storage_index_t storage,
                         libtorrent::add_torrent_params const* resume_data,
                         libtorrent::aux::vector<std::string, libtorrent::file_index_t> links,
                         std::function<void(libtorrent::status_t,
                                            libtorrent::storage_error const&)> handler) override;
  void async_stop_torrent(libtorrent::storage_index_t storage,
                          std::function<void()> handler = std::function<void()>()) override;
  void async_rename_file(libtorrent::storage_index_t storage, libtorrent::file_index_t index,
                         std::string name,
                         std::function<void(std::string const&, libtorrent::file_index_t,
                                            libtorrent::storage_error const&)> handler) override;
  void async_delete_files(libtorrent::storage_index_t storage, libtorrent::remove_flags_t options,
                          std::function<void(libtorrent::storage_error const&)> handler) override;
  void async_set_file_priority(
      libtorrent::storage_index_t storage,
      libtorrent::aux::vector<libtorrent::download_priority_t, libtorrent::file_index_t> priorities,
      std::function<void(
          libtorrent::storage_error const&,
          libtorrent::aux::vector<libtorrent::download_priority_t, libtorrent::file_index_t>)>
          handler) override;
  void async_clear_piece(libtorrent::storage_index_t storage, libtorrent::piece_index_t piece,
                         std::function<void(libtorrent::piece_index_t)> handler) override;

  void update_stats_counters(libtorrent::counters& counters) const override;
  std::vector<libtorrent::open_file_state>
  get_status(libtorrent::storage_index_t storage) const override;
  void abort(bool wait) override;
  void submit_jobs() override;
  void settings_updated() override;
  void free_disk_buffer(char* buffer) override;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

std::unique_ptr<libtorrent::disk_interface>
create_memory_disk_io(libtorrent::io_context& io_context,
                      libtorrent::settings_interface const& settings,
                      libtorrent::counters& counters);

void set_memory_disk_cache_policy(CachePolicy policy);
PieceWindow update_memory_disk_retained_window(std::int64_t torrent_byte_offset);
MemoryDiskCacheStatus memory_disk_cache_status();

} // namespace torrview
#endif
