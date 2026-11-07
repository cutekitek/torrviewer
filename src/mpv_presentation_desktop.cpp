#include "mpv_presentation.hpp"

#include "logger.hpp"

#include <stdexcept>

#include <SDL3/SDL.h>
#include <mpv/client.h>
#include <mpv/render_gl.h>

namespace torrview {
namespace {
class DesktopPresentation final : public MpvPresentation {
public:
  ~DesktopPresentation() override {
    if (context_ != nullptr) {
      mpv_render_context_free(context_);
    }
  }
  void configure(mpv_handle* handle) override {
    check(mpv_set_option_string(handle, "vo", "libmpv"));
  }
  void initialize(mpv_handle* handle) override {
    mpv_opengl_init_params gl = {
        .get_proc_address = [](void*, const char* name) -> void* {
          return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
        },
        .get_proc_address_ctx = nullptr,
    };
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    check(mpv_render_context_create(&context_, handle, params));
  }
  void render(int width, int height) override {
    if (context_ == nullptr || width <= 0 || height <= 0) {
      return;
    }
    (void)mpv_render_context_update(context_);
    mpv_opengl_fbo fbo = {.fbo = 0, .w = width, .h = height, .internal_format = 0};
    int flip = 1;
    mpv_render_param params[] = {{MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
                                 {MPV_RENDER_PARAM_FLIP_Y, &flip},
                                 {MPV_RENDER_PARAM_INVALID, nullptr}};
    const int result = mpv_render_context_render(context_, params);
    if (result < 0) {
      TORRVIEW_LOG_ERROR("mpv render: " << mpv_error_string(result));
    }
  }
  void report_swap() override {
    if (context_ != nullptr) {
      mpv_render_context_report_swap(context_);
    }
  }
  void set_surface(void*, int, int) override {}

private:
  static void check(int result) {
    if (result < 0) {
      throw std::runtime_error(mpv_error_string(result));
    }
  }
  mpv_render_context* context_ = nullptr;
};
} // namespace

std::unique_ptr<MpvPresentation> make_mpv_presentation() {
  return std::make_unique<DesktopPresentation>();
}
} // namespace torrview
