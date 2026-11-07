#include "library.hpp"
#include "mpv_player.hpp"
#include "mpv_presentation.hpp"
#include <mpv/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace torrview {
// Exercise the real mpv event/seek logic without requiring a display or audio device.
class TestPresentation final : public MpvPresentation {
public:
  void configure(mpv_handle* handle) override {
    mpv_set_option_string(handle, "vo", "null");
    mpv_set_option_string(handle, "ao", "null");
  }
  void initialize(mpv_handle*) override {}
  void render(int, int) override {}
  void report_swap() override {}
  void set_surface(void*, int, int) override {}
};
std::unique_ptr<MpvPresentation> make_mpv_presentation() {
  return std::make_unique<TestPresentation>();
}
} // namespace torrview

namespace {
void require(bool ok, const char* message) {
  if (!ok)
    throw std::runtime_error(message);
}
std::vector<char> wav() {
  constexpr std::uint32_t data_size = 8000 * 2 * 120;
  std::vector<char> data(44 + data_size, 0);
  auto word = [&](std::size_t at, std::uint32_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
      data[at + i] = static_cast<char>((value >> (i * 8)) & 255);
  };
  std::memcpy(data.data(), "RIFF", 4);
  word(4, data_size + 36, 4);
  std::memcpy(data.data() + 8, "WAVEfmt ", 8);
  word(16, 16, 4);
  word(20, 1, 2);
  word(22, 1, 2);
  word(24, 8000, 4);
  word(28, 16000, 4);
  word(32, 2, 2);
  word(34, 16, 2);
  std::memcpy(data.data() + 36, "data", 4);
  word(40, data_size, 4);
  return data;
}
class Reader final : public torrview::TorrentStreamReader {
public:
  explicit Reader(const std::vector<char>& data) : data_(data) {}
  std::int64_t read(char* target, std::uint64_t bytes) override {
    if (cancelled_)
      return -1;
    const auto count = std::min(bytes, static_cast<std::uint64_t>(data_.size() - offset_));
    std::memcpy(target, data_.data() + offset_, static_cast<std::size_t>(count));
    offset_ += static_cast<std::size_t>(count);
    return static_cast<std::int64_t>(count);
  }
  std::int64_t seek(std::int64_t offset) override {
    if (offset < 0 || offset > size())
      return -1;
    offset_ = static_cast<std::size_t>(offset);
    return offset;
  }
  std::int64_t size() const override { return static_cast<std::int64_t>(data_.size()); }
  void cancel() override { cancelled_ = true; }

private:
  const std::vector<char>& data_;
  std::size_t offset_ = 0;
  std::atomic<bool> cancelled_ = false;
};
class Provider final : public torrview::TorrentStreamProvider {
public:
  std::vector<char> data = wav();
  std::unique_ptr<torrview::TorrentStreamReader> open_torrent_stream(const std::string&,
                                                                     std::string&) override {
    return std::make_unique<Reader>(data);
  }
};
void wait_position(torrview::MpvPlayer& player, double position) {
  for (int i = 0; i < 1000; ++i) {
    player.process_events();
    const auto s = player.snapshot();
    require(s.error.empty(), s.error.c_str());
    if (s.file_ready && s.position_valid && !s.seeking && std::abs(s.time_pos - position) < 0.05) {
      require(!s.eof_reached, "Stop event was mistaken for EOF during resume");
      require(s.paused, "Paused resume unexpectedly autoplayed");
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  throw std::runtime_error("Resume did not reach requested position: " +
                           std::to_string(player.snapshot().time_pos));
}
} // namespace

int main() {
  const auto path =
      std::filesystem::temp_directory_path() /
      ("torrview-resume-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sqlite");
  try {
    Provider provider;
    {
      torrview::Library library(path.string());
      torrview::MpvPlayer player;
      player.set_torrent_stream_provider(&provider);
      player.load_file("torrview://test/0", false, 12.375);
      wait_position(player, 12.375);
      // Stop and replace before draining old events, as library navigation does.
      player.stop();
      player.load_file("torrview://test/1", false, 61.625);
      wait_position(player, 61.625);
      torrview::LibraryEntry entry;
      entry.key = "test";
      entry.name = "Two episodes";
      entry.source = "magnet:?test";
      entry.files = {{0, "first.wav", "wav", 0, 0, true}, {1, "second.wav", "wav", 0, 0, true}};
      entry.last_file = 1;
      entry.progress[0] = {12.375, 120};
      entry.progress[1] = {player.snapshot().time_pos, player.snapshot().duration};
      library.entries.push_back(entry);
      library.save();
    }
    {
      torrview::Library library(path.string());
      const auto& entry = library.entries.at(0);
      require(entry.last_file == 1, "Wrong last episode restored");
      torrview::MpvPlayer player;
      player.set_torrent_stream_provider(&provider);
      player.load_file("torrview://test/" + std::to_string(entry.last_file), false,
                       entry.progress.at(entry.last_file).position);
      wait_position(player, 61.625);
      player.seek_absolute(0);
      wait_position(player, 0);
    }
    std::filesystem::remove(path);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    std::filesystem::remove(path);
    return 1;
  }
}
