#include "mpv_presentation.hpp"

#include <mpv/client.h>

#include <cstdint>
#include <stdexcept>
#include <string>

namespace torrview {
namespace {
void check(int result) {
  if (result < 0) {
    throw std::runtime_error(mpv_error_string(result));
  }
}

class AndroidPresentation final : public MpvPresentation {
public:
  void configure(mpv_handle* handle) override {
    // No Surface exists during engine creation. Attach it before selecting gpu output.
    check(mpv_set_option_string(handle, "vo", "null"));
    check(mpv_set_option_string(handle, "gpu-context", "android"));
    check(mpv_set_option_string(handle, "hwdec", "mediacodec,mediacodec-copy,auto"));
    check(mpv_set_option_string(handle, "ao", "audiotrack,opensles"));
    // Custom torrent streams are not classified as network sources by mpv's cache=auto.
    // Enable seekable demuxer ranges within the existing bounded RAM cache.
    check(mpv_set_option_string(handle, "cache", "yes"));
    check(mpv_set_option_string(handle, "demuxer-max-bytes", "16MiB"));
    check(mpv_set_option_string(handle, "demuxer-max-back-bytes", "4MiB"));
  }
  void initialize(mpv_handle* handle) override { handle_ = handle; }
  void set_surface(void* surface, int width, int height) override {
    if (handle_ == nullptr) {
      return;
    }
    // Synchronous vo replacement tears down the old ANativeWindow before JNI releases it.
    check(mpv_set_property_string(handle_, "vo", "null"));
    auto window = static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(surface));
    check(mpv_set_property(handle_, "wid", MPV_FORMAT_INT64, &window));
    const auto dimensions = std::to_string(width) + "x" + std::to_string(height);
    check(mpv_set_property_string(handle_, "android-surface-size", dimensions.c_str()));
    if (surface != nullptr) {
      // SurfaceView can retire its BufferQueue while mpv is starting a VO asynchronously.
      // Keep demux/audio alive during that gap; the next surface reinitializes GPU output.
      check(mpv_set_property_string(handle_, "vo", "gpu,null"));
    }
  }
  void render(int, int) override {}
  void report_swap() override {}

private:
  mpv_handle* handle_ = nullptr;
};
} // namespace

std::unique_ptr<MpvPresentation> make_mpv_presentation() {
  return std::make_unique<AndroidPresentation>();
}
} // namespace torrview
