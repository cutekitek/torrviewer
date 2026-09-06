#include "mpv_presentation.hpp"

#include "config.hpp"
#include "logger.hpp"

#include <stdexcept>

#if defined(TORRVIEW_HAVE_MPV)
#include <SDL3/SDL.h>
#include <mpv/client.h>
#include <mpv/render_gl.h>
#endif

namespace torrview {
namespace {
class DesktopPresentation final : public MpvPresentation {
public:
  ~DesktopPresentation() override {
#if defined(TORRVIEW_HAVE_MPV)
    if (context_ != nullptr) {
      mpv_render_context_free(context_);
    }
#endif
  }
  void configure(mpv_handle* handle) override {
#if defined(TORRVIEW_HAVE_MPV)
    check(mpv_set_option_string(handle, "vo", "libmpv"));
#else
    (void)handle;
#endif
  }
  void initialize(mpv_handle* handle) override {
#if defined(TORRVIEW_HAVE_MPV)
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
#else
    (void)handle;
#endif
  }
  void render(int width, int height) override {
#if defined(TORRVIEW_HAVE_MPV)
    if (context_ == nullptr || width <= 0 || height <= 0) {
      return;
    }
    (void)mpv_render_context_update(context_);
    mpv_opengl_fbo fbo = {.fbo = 0, .w = width, .h = height, .internal_format = 0};
    int flip = 1;
    mpv_render_param params[] = {{MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
                                 {MPV_RENDER_PARAM_FLIP_Y, &flip},
                                 { MPV_RENDER_PARAM_INVALID,
                                   nullptr }};
    const int result = mpv_render_context_render(context_, params);
    if (result < 0) {
      TORRVIEW_LOG_ERROR("mpv render: " << mpv_error_string(result));
    }
#else
    (void)width;
    (void)height;
#endif
  }
  void report_swap() override {
#if defined(TORRVIEW_HAVE_MPV)
    if (context_ != nullptr) {
      mpv_render_context_report_swap(context_);
    }
#endif
  }
  void set_surface(void*, int, int) override {}

private:
#if defined(TORRVIEW_HAVE_MPV)
  static void check(int result) {
    if (result < 0) {
      throw std::runtime_error(mpv_error_string(result));
    }
  }
  mpv_render_context* context_ = nullptr;
#endif
};
} // namespace

std::unique_ptr<MpvPresentation> make_mpv_presentation() {
  return std::make_unique<DesktopPresentation>();
}
} // namespace torrview
