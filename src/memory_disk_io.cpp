#include "memory_disk_io.hpp"

#if defined(TORRVIEW_HAVE_LIBTORRENT)

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/aux_/vector.hpp>
#include <libtorrent/download_priority.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/io_context.hpp>
#include <libtorrent/peer_request.hpp>

#include <boost/asio/error.hpp>
#include <boost/system/errc.hpp>

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace torrview {
namespace lt = libtorrent;

namespace {

struct WriteRange {
  int begin = 0;
  int end = 0;
};

lt::storage_error make_storage_error(boost::system::errc::errc_t error,
                                     lt::operation_t operation) {
  return lt::storage_error(lt::error_code(error, boost::system::generic_category()), operation);
}

lt::storage_error make_eof_error(lt::operation_t operation) {
  return lt::storage_error(lt::error_code(boost::asio::error::eof), operation);
}

void merge_written_range(std::vector<WriteRange>& ranges, WriteRange next) {
  if (next.begin >= next.end) {
    return;
  }

  ranges.push_back(next);
  std::sort(ranges.begin(), ranges.end(),
            [](const WriteRange& left, const WriteRange& right) {
              return left.begin < right.begin;
            });

  std::vector<WriteRange> merged;
  merged.reserve(ranges.size());
  for (const WriteRange& range : ranges) {
    if (merged.empty() || merged.back().end < range.begin) {
      merged.push_back(range);
    } else {
      merged.back().end = std::max(merged.back().end, range.end);
    }
  }
  ranges = std::move(merged);
}

bool covers_range(const std::vector<WriteRange>& ranges, int begin, int end) {
  if (begin >= end) {
    return true;
  }

  for (const WriteRange& range : ranges) {
    if (range.begin <= begin && range.end >= end) {
      return true;
    }
    if (range.begin > begin) {
      return false;
    }
  }
  return false;
}

struct MemoryPiece {
  std::vector<char> bytes;
  std::vector<WriteRange> written;
};

class MemoryTorrentStorage final {
public:
  explicit MemoryTorrentStorage(const lt::file_storage& files) : files_(files) {}

  [[nodiscard]] std::int64_t write(const lt::peer_request& request, char const* buffer,
                                   lt::storage_error& error) {
    if (!valid_request(request, error, lt::operation_t::file_write)) {
      return 0;
    }
    if (buffer == nullptr && request.length > 0) {
      error = make_storage_error(boost::system::errc::invalid_argument,
                                 lt::operation_t::file_write);
      return 0;
    }

    const int size = piece_size(request.piece);
    MemoryPiece& piece = pieces_[request.piece];
    std::int64_t allocated = 0;
    if (piece.bytes.empty()) {
      piece.bytes.resize(static_cast<std::size_t>(size));
      allocated = size;
    }

    std::memcpy(piece.bytes.data() + request.start, buffer,
                static_cast<std::size_t>(request.length));
    merge_written_range(piece.written, {request.start, request.start + request.length});
    return allocated;
  }

  [[nodiscard]] lt::disk_buffer_holder read(const lt::peer_request& request,
                                            lt::buffer_allocator_interface& allocator,
                                            lt::storage_error& error) const {
    if (!valid_request(request, error, lt::operation_t::file_read)) {
      return {};
    }

    auto found = pieces_.find(request.piece);
    if (found == pieces_.end() ||
        !covers_range(found->second.written, request.start, request.start + request.length)) {
      error = make_eof_error(lt::operation_t::file_read);
      return {};
    }

    const auto length = static_cast<std::size_t>(request.length);
    auto* copy = new char[length];
    std::memcpy(copy, found->second.bytes.data() + request.start,
                length);
    return lt::disk_buffer_holder(allocator, copy, request.length);
  }

  [[nodiscard]] lt::sha1_hash hash(lt::piece_index_t piece,
                                   lt::span<lt::sha256_hash> block_hashes,
                                   lt::disk_job_flags_t flags, lt::storage_error& error) const {
    const MemoryPiece* data = full_piece(piece, error, lt::operation_t::file_read);
    if (data == nullptr) {
      return {};
    }

    if (!block_hashes.empty()) {
      const int blocks = files_.blocks_in_piece2(piece);
      const int size = files_.piece_size2(piece);
      const int count = std::min(blocks, static_cast<int>(block_hashes.size()));
      int offset = 0;
      for (int index = 0; index < count; ++index) {
        const int length = std::min(lt::default_block_size, size - offset);
        lt::span<char const> block{data->bytes.data() + offset, length};
        block_hashes[index] = lt::hasher256(block).final();
        offset += length;
      }
    }

    if ((flags & lt::disk_interface::v1_hash) == lt::disk_interface::v1_hash) {
      lt::span<char const> piece_bytes{data->bytes.data(), piece_size(piece)};
      return lt::hasher(piece_bytes).final();
    }
    return {};
  }

  [[nodiscard]] lt::sha256_hash hash2(lt::piece_index_t piece, int offset,
                                      lt::storage_error& error) const {
    const int size = piece_size(piece);
    if (offset < 0 || offset >= size) {
      error = make_storage_error(boost::system::errc::invalid_argument,
                                 lt::operation_t::file_read);
      return {};
    }

    const int length = std::min(lt::default_block_size, size - offset);
    auto found = pieces_.find(piece);
    if (found == pieces_.end() || !covers_range(found->second.written, offset, offset + length)) {
      error = make_eof_error(lt::operation_t::file_read);
      return {};
    }

    lt::span<char const> block{found->second.bytes.data() + offset, length};
    return lt::hasher256(block).final();
  }

  [[nodiscard]] std::int64_t clear_piece(lt::piece_index_t piece) {
    auto found = pieces_.find(piece);
    if (found == pieces_.end()) {
      return 0;
    }
    const std::int64_t released = static_cast<std::int64_t>(found->second.bytes.size());
    pieces_.erase(found);
    return released;
  }

  [[nodiscard]] std::int64_t clear_all() {
    std::int64_t released = 0;
    for (const auto& [piece, data] : pieces_) {
      (void)piece;
      released += static_cast<std::int64_t>(data.bytes.size());
    }
    pieces_.clear();
    return released;
  }

private:
  [[nodiscard]] int piece_size(lt::piece_index_t piece) const { return files_.piece_size(piece); }

  [[nodiscard]] bool valid_request(const lt::peer_request& request, lt::storage_error& error,
                                   lt::operation_t operation) const {
    if (request.length < 0 || request.start < 0 || request.piece < lt::piece_index_t{0} ||
        request.piece >= files_.end_piece()) {
      error = make_storage_error(boost::system::errc::invalid_argument, operation);
      return false;
    }

    const int size = piece_size(request.piece);
    if (request.start > size || request.length > size - request.start) {
      error = make_eof_error(operation);
      return false;
    }
    return true;
  }

  [[nodiscard]] const MemoryPiece* full_piece(lt::piece_index_t piece, lt::storage_error& error,
                                              lt::operation_t operation) const {
    if (piece < lt::piece_index_t{0} || piece >= files_.end_piece()) {
      error = make_storage_error(boost::system::errc::invalid_argument, operation);
      return nullptr;
    }

    auto found = pieces_.find(piece);
    if (found == pieces_.end() || !covers_range(found->second.written, 0, piece_size(piece))) {
      error = make_eof_error(operation);
      return nullptr;
    }
    return &found->second;
  }

  lt::file_storage files_;
  std::map<lt::piece_index_t, MemoryPiece> pieces_;
};

lt::storage_index_t pop_slot(std::vector<lt::storage_index_t>& slots) {
  lt::storage_index_t slot = slots.back();
  slots.pop_back();
  return slot;
}

} // namespace

class MemoryDiskIO::Impl final {
public:
  explicit Impl(lt::io_context& io_context) : io_context_(io_context) {}

  [[nodiscard]] std::int64_t bytes_used() const {
    std::lock_guard lock(mutex_);
    return bytes_used_;
  }

  lt::storage_holder new_torrent(lt::storage_params const& params, lt::disk_interface& owner) {
    std::lock_guard lock(mutex_);
    const lt::storage_index_t index =
        free_slots_.empty() ? storages_.end_index() : pop_slot(free_slots_);

    auto storage = std::make_unique<MemoryTorrentStorage>(params.files);
    if (index == storages_.end_index()) {
      storages_.emplace_back(std::move(storage));
    } else {
      storages_[index] = std::move(storage);
    }
    return lt::storage_holder(index, owner);
  }

  void remove_torrent(lt::storage_index_t index) {
    std::lock_guard lock(mutex_);
    MemoryTorrentStorage* storage = find_storage_locked(index);
    if (storage == nullptr) {
      return;
    }
    bytes_used_ -= storage->clear_all();
    storages_[index].reset();
    free_slots_.push_back(index);
  }

  void async_read(lt::storage_index_t index, const lt::peer_request& request,
                  lt::buffer_allocator_interface& allocator,
                  std::function<void(lt::disk_buffer_holder, lt::storage_error const&)> handler) {
    lt::storage_error error;
    lt::disk_buffer_holder buffer;
    {
      std::lock_guard lock(mutex_);
      if (MemoryTorrentStorage* storage = find_storage_locked(index); storage != nullptr) {
        buffer = storage->read(request, allocator, error);
      } else {
        error = make_storage_error(boost::system::errc::no_such_file_or_directory,
                                   lt::operation_t::file_read);
      }
    }

    lt::post(io_context_, [handler = std::move(handler), buffer = std::move(buffer), error]() mutable {
      handler(std::move(buffer), error);
    });
  }

  bool async_write(lt::storage_index_t index, const lt::peer_request& request, const char* buffer,
                   std::function<void(lt::storage_error const&)> handler) {
    lt::storage_error error;
    {
      std::lock_guard lock(mutex_);
      if (MemoryTorrentStorage* storage = find_storage_locked(index); storage != nullptr) {
        bytes_used_ += storage->write(request, buffer, error);
      } else {
        error = make_storage_error(boost::system::errc::no_such_file_or_directory,
                                   lt::operation_t::file_write);
      }
    }

    lt::post(io_context_, [handler = std::move(handler), error]() { handler(error); });
    return false;
  }

  void async_hash(lt::storage_index_t index, lt::piece_index_t piece,
                  lt::span<lt::sha256_hash> block_hashes, lt::disk_job_flags_t flags,
                  std::function<void(lt::piece_index_t, lt::sha1_hash const&,
                                     lt::storage_error const&)> handler) {
    lt::storage_error error;
    lt::sha1_hash hash;
    {
      std::lock_guard lock(mutex_);
      if (MemoryTorrentStorage* storage = find_storage_locked(index); storage != nullptr) {
        hash = storage->hash(piece, block_hashes, flags, error);
      } else {
        error = make_storage_error(boost::system::errc::no_such_file_or_directory,
                                   lt::operation_t::file_read);
      }
    }

    lt::post(io_context_, [handler = std::move(handler), piece, hash, error]() {
      handler(piece, hash, error);
    });
  }

  void async_hash2(lt::storage_index_t index, lt::piece_index_t piece, int offset,
                   std::function<void(lt::piece_index_t, lt::sha256_hash const&,
                                      lt::storage_error const&)> handler) {
    lt::storage_error error;
    lt::sha256_hash hash;
    {
      std::lock_guard lock(mutex_);
      if (MemoryTorrentStorage* storage = find_storage_locked(index); storage != nullptr) {
        hash = storage->hash2(piece, offset, error);
      } else {
        error = make_storage_error(boost::system::errc::no_such_file_or_directory,
                                   lt::operation_t::file_read);
      }
    }

    lt::post(io_context_, [handler = std::move(handler), piece, hash, error]() {
      handler(piece, hash, error);
    });
  }

  void delete_files(lt::storage_index_t index,
                    std::function<void(lt::storage_error const&)> handler) {
    {
      std::lock_guard lock(mutex_);
      if (MemoryTorrentStorage* storage = find_storage_locked(index); storage != nullptr) {
        bytes_used_ -= storage->clear_all();
      }
    }
    lt::post(io_context_, [handler = std::move(handler)]() { handler({}); });
  }

  void clear_piece(lt::storage_index_t index, lt::piece_index_t piece,
                   std::function<void(lt::piece_index_t)> handler) {
    {
      std::lock_guard lock(mutex_);
      if (MemoryTorrentStorage* storage = find_storage_locked(index); storage != nullptr) {
        bytes_used_ -= storage->clear_piece(piece);
      }
    }
    lt::post(io_context_, [handler = std::move(handler), piece]() { handler(piece); });
  }

  void post_void(std::function<void()> handler) {
    if (handler) {
      lt::post(io_context_, std::move(handler));
    }
  }

  lt::io_context& io_context() { return io_context_; }

private:
  [[nodiscard]] MemoryTorrentStorage* find_storage_locked(lt::storage_index_t index) {
    if (index < lt::storage_index_t{0} || index >= storages_.end_index()) {
      return nullptr;
    }
    return storages_[index].get();
  }

  lt::io_context& io_context_;
  mutable std::mutex mutex_;
  lt::aux::vector<std::unique_ptr<MemoryTorrentStorage>, lt::storage_index_t> storages_;
  std::vector<lt::storage_index_t> free_slots_;
  std::int64_t bytes_used_ = 0;
};

MemoryDiskIO::MemoryDiskIO(lt::io_context& io_context)
    : impl_(std::make_unique<Impl>(io_context)) {}

MemoryDiskIO::~MemoryDiskIO() = default;

std::int64_t MemoryDiskIO::bytes_used() const { return impl_->bytes_used(); }

lt::storage_holder MemoryDiskIO::new_torrent(lt::storage_params const& params,
                                             std::shared_ptr<void> const&) {
  return impl_->new_torrent(params, *this);
}

void MemoryDiskIO::remove_torrent(lt::storage_index_t storage) { impl_->remove_torrent(storage); }

void MemoryDiskIO::async_read(
    lt::storage_index_t storage, lt::peer_request const& request,
    std::function<void(lt::disk_buffer_holder, lt::storage_error const&)> handler,
    lt::disk_job_flags_t) {
  impl_->async_read(storage, request, *this, std::move(handler));
}

bool MemoryDiskIO::async_write(lt::storage_index_t storage, lt::peer_request const& request,
                               char const* buffer, std::shared_ptr<lt::disk_observer>,
                               std::function<void(lt::storage_error const&)> handler,
                               lt::disk_job_flags_t) {
  return impl_->async_write(storage, request, buffer, std::move(handler));
}

void MemoryDiskIO::async_hash(
    lt::storage_index_t storage, lt::piece_index_t piece, lt::span<lt::sha256_hash> v2,
    lt::disk_job_flags_t flags,
    std::function<void(lt::piece_index_t, lt::sha1_hash const&, lt::storage_error const&)>
        handler) {
  impl_->async_hash(storage, piece, v2, flags, std::move(handler));
}

void MemoryDiskIO::async_hash2(
    lt::storage_index_t storage, lt::piece_index_t piece, int offset, lt::disk_job_flags_t,
    std::function<void(lt::piece_index_t, lt::sha256_hash const&, lt::storage_error const&)>
        handler) {
  impl_->async_hash2(storage, piece, offset, std::move(handler));
}

void MemoryDiskIO::async_move_storage(
    lt::storage_index_t, std::string path, lt::move_flags_t,
    std::function<void(lt::status_t, std::string const&, lt::storage_error const&)> handler) {
  lt::post(impl_->io_context(), [handler = std::move(handler), path = std::move(path)]() {
    handler(lt::status_t::no_error, path, {});
  });
}

void MemoryDiskIO::async_release_files(lt::storage_index_t, std::function<void()> handler) {
  impl_->post_void(std::move(handler));
}

void MemoryDiskIO::async_check_files(
    lt::storage_index_t, lt::add_torrent_params const*,
    lt::aux::vector<std::string, lt::file_index_t>,
    std::function<void(lt::status_t, lt::storage_error const&)> handler) {
  lt::post(impl_->io_context(), [handler = std::move(handler)]() {
    handler(lt::status_t::no_error, {});
  });
}

void MemoryDiskIO::async_stop_torrent(lt::storage_index_t, std::function<void()> handler) {
  impl_->post_void(std::move(handler));
}

void MemoryDiskIO::async_rename_file(
    lt::storage_index_t, lt::file_index_t index, std::string name,
    std::function<void(std::string const&, lt::file_index_t, lt::storage_error const&)> handler) {
  lt::post(impl_->io_context(), [handler = std::move(handler), index, name = std::move(name)]() {
    handler(name, index, {});
  });
}

void MemoryDiskIO::async_delete_files(
    lt::storage_index_t storage, lt::remove_flags_t,
    std::function<void(lt::storage_error const&)> handler) {
  impl_->delete_files(storage, std::move(handler));
}

void MemoryDiskIO::async_set_file_priority(
    lt::storage_index_t,
    lt::aux::vector<lt::download_priority_t, lt::file_index_t> priorities,
    std::function<void(lt::storage_error const&,
                       lt::aux::vector<lt::download_priority_t, lt::file_index_t>)> handler) {
  lt::post(impl_->io_context(),
           [handler = std::move(handler), priorities = std::move(priorities)]() mutable {
             handler({}, std::move(priorities));
           });
}

void MemoryDiskIO::async_clear_piece(lt::storage_index_t storage, lt::piece_index_t piece,
                                     std::function<void(lt::piece_index_t)> handler) {
  impl_->clear_piece(storage, piece, std::move(handler));
}

void MemoryDiskIO::update_stats_counters(lt::counters&) const {}

std::vector<lt::open_file_state> MemoryDiskIO::get_status(lt::storage_index_t) const { return {}; }

void MemoryDiskIO::abort(bool) {}

void MemoryDiskIO::submit_jobs() {}

void MemoryDiskIO::settings_updated() {}

void MemoryDiskIO::free_disk_buffer(char* buffer) { delete[] buffer; }

std::unique_ptr<lt::disk_interface> create_memory_disk_io(lt::io_context& io_context,
                                                          lt::settings_interface const&,
                                                          lt::counters&) {
  return std::make_unique<MemoryDiskIO>(io_context);
}

} // namespace torrview

#endif
