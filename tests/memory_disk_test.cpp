#include "memory_disk_io.hpp"

#include <libtorrent/file_storage.hpp>
#include <libtorrent/io_context.hpp>
#include <libtorrent/peer_request.hpp>
#include <libtorrent/storage_defs.hpp>

#include <cstdlib>
#include <iostream>

namespace {
void require(bool value, const char* message) {
  if (!value) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}
} // namespace

int main() {
  namespace lt = libtorrent;
  lt::io_context io;
  torrview::MemoryDiskIO disk(io);
  disk.set_cache_policy(
      {.max_bytes = 32768, .retain_before_bytes = 0, .retain_after_bytes = 32768});
  lt::file_storage files;
  files.add_file("unused.mkv", 65536);
  files.set_piece_length(16384);
  files.set_num_pieces(4);
  const std::string path = ".";
  const lt::aux::vector<lt::download_priority_t, lt::file_index_t> priorities;
  lt::storage_params params(files, nullptr, path, lt::storage_mode_sparse, priorities, {});
  auto storage = disk.new_torrent(params, {});
  const std::string payload(16384, 'a');
  int callbacks = 0;
  auto write = [&](int piece, int offset, int length, bool valid) {
    disk.async_write(storage, {lt::piece_index_t(piece), offset, length}, payload.data(), {},
                     [&](const lt::storage_error& error) {
                       require(static_cast<bool>(error) != valid, "Unexpected write result");
                       ++callbacks;
                     });
    io.restart();
    io.run();
  };
  write(0, 0, 8192, true);
  require(disk.bytes_used() == 16384, "Partial write must allocate one piece");
  write(0, 4096, 8192, true);
  require(disk.bytes_used() == 16384, "Overlapping write double counted memory");
  char byte = 0;
  require(disk.read_bytes(storage, lt::piece_index_t(0), 10000, 1, &byte) && byte == 'a',
          "Written bytes were not retained");
  require(!disk.read_bytes(storage, lt::piece_index_t(0), 15000, 1, &byte),
          "Unwritten range was exposed");
  write(0, 16000, 1000, false);
  require(disk.bytes_used() == 16384, "Invalid write changed accounting");
  write(1, 0, 16384, true);
  disk.set_retained_window(storage, {.first_piece = 1, .end_piece = 2});
  write(2, 0, 16384, true);
  require(disk.bytes_used() <= 32768, "Eviction exceeded cache budget");
  require(disk.read_bytes(storage, lt::piece_index_t(1), 0, 1, &byte), "Retained piece evicted");
  require(callbacks == 5, "A write callback was lost or duplicated");
  storage.reset();
  require(disk.bytes_used() == 0, "Removing torrent leaked accounted bytes");

  const auto session_a = torrview::next_memory_disk_session_id();
  const auto session_b = torrview::next_memory_disk_session_id();
  torrview::MemoryDiskIO first(io, session_a);
  torrview::MemoryDiskIO second(io, session_b);
  const auto hash = (lt::sha1_hash::max)();
  lt::storage_params other_params(files, nullptr, path, lt::storage_mode_sparse, priorities, hash);
  auto first_zero = first.new_torrent(params, {});
  auto first_one = first.new_torrent(other_params, {});
  auto second_zero = second.new_torrent(other_params, {});
  auto seed = [&](torrview::MemoryDiskIO& target, lt::storage_index_t index, char value) {
    const std::string bytes(16384, value);
    target.async_write(
        index, {lt::piece_index_t(0), 0, 16384}, bytes.data(), {},
        [](const lt::storage_error& error) { require(!error, "Seed write failed"); });
    io.restart();
    io.run();
  };
  seed(first, first_zero, 'z');
  seed(first, first_one, 'a');
  seed(second, second_zero, 'b');
  require(torrview::read_memory_disk_bytes(session_a, hash, lt::piece_index_t(0), 0, 1, &byte) &&
              byte == 'a',
          "Hash lookup incorrectly assumed storage slot zero");
  require(torrview::read_memory_disk_bytes(session_b, hash, lt::piece_index_t(0), 0, 1, &byte) &&
              byte == 'b',
          "A stream read another session's payload");
  const auto second_limit = second.cache_policy().max_bytes;
  torrview::set_memory_disk_cache_policy(session_a, {.max_bytes = 16384});
  require(second.cache_policy().max_bytes == second_limit,
          "Cache settings crossed session boundaries");
  first_one.reset();
  require(!torrview::read_memory_disk_bytes(session_a, hash, lt::piece_index_t(0), 0, 1, &byte),
          "Removed storage resolved to an unrelated torrent");
}
