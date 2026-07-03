#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace torrview {

class TorrentStreamReader {
public:
  TorrentStreamReader() = default;
  TorrentStreamReader(const TorrentStreamReader&) = delete;
  TorrentStreamReader& operator=(const TorrentStreamReader&) = delete;
  virtual ~TorrentStreamReader() = default;

  virtual std::int64_t read(char* buffer, std::uint64_t bytes) = 0;
  virtual std::int64_t seek(std::int64_t offset) = 0;
  virtual std::int64_t size() const = 0;
  virtual void cancel() = 0;
};

class TorrentStreamProvider {
public:
  TorrentStreamProvider() = default;
  TorrentStreamProvider(const TorrentStreamProvider&) = delete;
  TorrentStreamProvider& operator=(const TorrentStreamProvider&) = delete;
  virtual ~TorrentStreamProvider() = default;

  virtual std::unique_ptr<TorrentStreamReader> open_torrent_stream(const std::string& uri,
                                                                   std::string& error) = 0;
};

} // namespace torrview
