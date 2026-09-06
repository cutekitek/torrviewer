#pragma once

#include <memory>

struct mpv_handle;

namespace torrview {

// Owned by MpvPlayer; destroyed before its mpv_handle. All calls use the player's owner thread.
class MpvPresentation {
public:
  virtual ~MpvPresentation() = default;
  virtual void configure(mpv_handle* handle) = 0;
  virtual void initialize(mpv_handle* handle) = 0;
  virtual void render(int width, int height) = 0;
  virtual void report_swap() = 0;
  // Android passes a JNI global Surface reference, owned by the Android bridge.
  virtual void set_surface(void* surface, int width, int height) = 0;
};

std::unique_ptr<MpvPresentation> make_mpv_presentation();

} // namespace torrview
