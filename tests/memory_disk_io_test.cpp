#include "memory_disk_io.hpp"

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/download_priority.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/io_context.hpp>
#include <libtorrent/peer_request.hpp>

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {
namespace lt = libtorrent;

void run_io(lt::io_context& io_context) {
  io_context.restart();
  io_context.run();
}

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "memory_disk_io_test: " << message << '\n';
    std::exit(1);
  }
}

lt::file_storage make_files() {
  lt::file_storage files;
  files.set_name("memory-test");
  files.set_piece_length(lt::default_block_size);
  files.add_file("payload.bin", lt::default_block_size * 6);
  files.set_num_pieces(6);
  return files;
}

void write_piece(torrview::MemoryDiskIO& disk, lt::io_context& io_context,
                 lt::storage_index_t storage, lt::piece_index_t piece,
                 const std::array<char, lt::default_block_size>& block) {
  bool write_done = false;
  disk.async_write(storage, {piece, 0, static_cast<int>(block.size())}, block.data(), {},
                   [&](lt::storage_error const& error) {
                     require(!error, "write returned an error");
                     write_done = true;
                   });
  run_io(io_context);
  require(write_done, "write callback was not called");
}

bool read_piece_start(torrview::MemoryDiskIO& disk, lt::io_context& io_context,
                      lt::storage_index_t storage, lt::piece_index_t piece) {
  bool read_done = false;
  bool read_ok = false;
  disk.async_read(storage, {piece, 0, 16},
                  [&](lt::disk_buffer_holder buffer, lt::storage_error const& error) {
                    read_ok = !error && buffer && buffer.size() == 16;
                    read_done = true;
                  });
  run_io(io_context);
  require(read_done, "read callback was not called");
  return read_ok;
}

} // namespace

int main() {
  const torrview::CachePolicy default_policy;
  require(default_policy.max_bytes == 256LL * torrview::CachePolicy::mib,
          "default cache limit should be 256 MiB");
  const torrview::PieceWindow default_window = torrview::compute_retained_piece_window(
      40LL * torrview::CachePolicy::mib, 256, static_cast<int>(torrview::CachePolicy::mib),
      default_policy);
  require(default_window.first_piece == 0 && default_window.end_piece == 232,
          "default retained window should keep 64 MiB behind and 192 MiB ahead");

  lt::io_context io_context;
  torrview::MemoryDiskIO disk(io_context);

  lt::file_storage files = make_files();
  const std::string save_path = ".";
  lt::aux::vector<lt::download_priority_t, lt::file_index_t> priorities;
  lt::storage_params params(files, nullptr, save_path, lt::storage_mode_sparse, priorities,
                            lt::sha1_hash{});
  lt::storage_holder holder = disk.new_torrent(params, {});
  const auto storage = static_cast<lt::storage_index_t>(holder);

  std::array<char, lt::default_block_size> block{};
  for (std::size_t index = 0; index < block.size(); ++index) {
    block[index] = static_cast<char>(index % 251U);
  }

  write_piece(disk, io_context, storage, lt::piece_index_t{0}, block);
  require(disk.bytes_used() == static_cast<std::int64_t>(block.size()),
          "memory usage did not track the allocated piece");

  bool read_done = false;
  std::vector<char> read_back;
  disk.async_read(storage, {lt::piece_index_t{0}, 7, 4096},
                  [&](lt::disk_buffer_holder buffer, lt::storage_error const& error) {
                    require(!error, "read returned an error");
                    require(buffer && buffer.size() == 4096, "read returned wrong buffer size");
                    read_back.assign(buffer.data(), buffer.data() + buffer.size());
                    read_done = true;
                  });
  run_io(io_context);
  require(read_done, "read callback was not called");
  require(std::memcmp(read_back.data(), block.data() + 7, read_back.size()) == 0,
          "read bytes did not match written data");

  bool missing_read_done = false;
  disk.async_read(storage, {lt::piece_index_t{1}, 0, 16},
                  [&](lt::disk_buffer_holder buffer, lt::storage_error const& error) {
                    require(static_cast<bool>(error), "missing read should return an error");
                    require(!buffer, "missing read returned a buffer");
                    missing_read_done = true;
                  });
  run_io(io_context);
  require(missing_read_done, "missing read callback was not called");

  bool hash_done = false;
  std::vector<lt::sha256_hash> block_hashes(1);
  disk.async_hash(storage, lt::piece_index_t{0}, block_hashes,
                  lt::disk_interface::v1_hash,
                  [&](lt::piece_index_t piece, lt::sha1_hash const& hash,
                      lt::storage_error const& error) {
                    require(!error, "hash returned an error");
                    require(piece == lt::piece_index_t{0}, "hash returned wrong piece");
                    require(hash == lt::hasher(lt::span<char const>{block.data(), block.size()})
                                        .final(),
                            "SHA-1 hash mismatch");
                    require(block_hashes[0] ==
                                lt::hasher256(
                                    lt::span<char const>{block.data(), block.size()})
                                    .final(),
                            "SHA-256 block hash mismatch");
                    hash_done = true;
                  });
  run_io(io_context);
  require(hash_done, "hash callback was not called");

  bool hash2_done = false;
  disk.async_hash2(storage, lt::piece_index_t{0}, 0, {},
                   [&](lt::piece_index_t piece, lt::sha256_hash const& hash,
                       lt::storage_error const& error) {
                     require(!error, "hash2 returned an error");
                     require(piece == lt::piece_index_t{0}, "hash2 returned wrong piece");
                     require(hash == block_hashes[0], "hash2 mismatch");
                     hash2_done = true;
                   });
  run_io(io_context);
  require(hash2_done, "hash2 callback was not called");

  disk.set_cache_policy({static_cast<std::int64_t>(block.size() * 2),
                         static_cast<std::int64_t>(block.size()),
                         static_cast<std::int64_t>(block.size())});
  const torrview::PieceWindow retained =
      disk.update_retained_window(storage, static_cast<std::int64_t>(block.size() * 3));
  require(retained.first_piece == 2 && retained.end_piece == 4,
          "retained window should be computed from playback byte offset");

  write_piece(disk, io_context, storage, lt::piece_index_t{1}, block);
  write_piece(disk, io_context, storage, lt::piece_index_t{2}, block);
  write_piece(disk, io_context, storage, lt::piece_index_t{3}, block);
  require(disk.bytes_used() == static_cast<std::int64_t>(block.size() * 2),
          "cache eviction should hold memory near the configured limit");
  require(!read_piece_start(disk, io_context, storage, lt::piece_index_t{0}),
          "evicted piece should read as a storage miss");
  require(read_piece_start(disk, io_context, storage, lt::piece_index_t{2}),
          "retained piece should remain readable");
  torrview::MemoryDiskCacheStatus status = disk.cache_status(storage);
  require(status.evicted_read_misses > 0, "evicted read miss was not tracked");

  disk.update_retained_window(storage, 0);
  write_piece(disk, io_context, storage, lt::piece_index_t{0}, block);
  require(read_piece_start(disk, io_context, storage, lt::piece_index_t{0}),
          "rewritten evicted piece should satisfy the backward seek rebuffer path");

  bool delete_done = false;
  disk.async_delete_files(storage, {}, [&](lt::storage_error const& error) {
    require(!error, "delete returned an error");
    delete_done = true;
  });
  run_io(io_context);
  require(delete_done, "delete callback was not called");
  require(disk.bytes_used() == 0, "delete did not release memory");

  return 0;
}
