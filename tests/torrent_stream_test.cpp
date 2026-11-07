#include "torrent_service.hpp"

#include <libtorrent/bencode.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/file_storage.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>

namespace {
void require(bool value, const char* message) {
  if (!value) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}
} // namespace

int main() {
  using namespace std::chrono_literals;
  namespace lt = libtorrent;
  const auto directory =
      std::filesystem::temp_directory_path() /
      ("torrview-test-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(directory);
  {
    lt::file_storage files;
    files.add_file("sample.mkv", 32768);
    lt::create_torrent torrent(files, 16384, lt::create_torrent::v1_only);
    const std::string hash(20, 'x');
    torrent.set_hash(lt::piece_index_t(0), lt::sha1_hash(hash.data()));
    torrent.set_hash(lt::piece_index_t(1), lt::sha1_hash(hash.data()));
    std::vector<char> metadata;
    lt::bencode(std::back_inserter(metadata), torrent.generate());
    const auto path = directory / "input.torrent";
    std::ofstream output(path, std::ios::binary);
    output.write(metadata.data(), static_cast<std::streamsize>(metadata.size()));
    output.close();
    torrview::TorrentService service(directory.string());
    service.set_cache_limit_mib(64);
    service.set_suspended(true);
    service.load_torrent_file(path.string());
    std::string uri;
    for (int i = 0; i < 300 && uri.empty(); ++i) {
      service.process_alerts();
      uri = service.stream_uri_for_file(0);
      std::this_thread::sleep_for(10ms);
    }
    require(!uri.empty(), "Torrent was not added");
    const auto saved = service.snapshot();
    require(saved.metadata && !saved.metadata->empty(), "Metadata not exported");
    service.reset();
    std::filesystem::remove(path);
    service.load_torrent_data(*saved.metadata, path.string());
    uri.clear();
    for (int i = 0; i < 300 && uri.empty(); ++i) {
      service.process_alerts();
      uri = service.stream_uri_for_file(0);
      std::this_thread::sleep_for(10ms);
    }
    require(!uri.empty(), "Saved metadata failed to restore after deleting source file");
    require(service.snapshot().identity == saved.identity, "Torrent identity changed on restore");
    std::string error;
    auto reader = service.open_torrent_stream(uri, error);
    require(reader != nullptr, error.c_str());
    require(reader->size() == 32768, "File bounds changed");
    require(reader->seek(-1) == -1, "Negative seek accepted");
    require(reader->seek(32769) == -1, "Seek beyond EOF accepted");
    require(reader->seek(32768) == 32768, "EOF seek failed");
    char byte = 0;
    require(reader->read(&byte, 1) == 0, "EOF read failed");
    for (int iteration = 0; iteration < 2; ++iteration) {
      require(reader->seek(0) == 0, "Seek after cancellation failed");
      auto read = std::async(std::launch::async, [&] { return reader->read(&byte, 1); });
      require(read.wait_for(100ms) == std::future_status::timeout, "Missing piece did not block");
      if (iteration == 0) {
        reader->cancel();
      } else {
        service.reset();
      }
      require(read.wait_for(2s) == std::future_status::ready,
              "Cancellation failed to unblock read");
      require(read.get() < 0, "Cancelled read should fail");
    }
    reader.reset();
    require(!std::filesystem::exists(directory / "sample.mkv"), "Payload written to disk");
  }
  std::filesystem::remove_all(directory);
}
