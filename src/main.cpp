#include "config.hpp"

#include <SDL3/SDL.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace torrview {

namespace {

std::string sdl_error(const char* action) {
  std::string message = action;
  message += ": ";
  message += SDL_GetError();
  return message;
}

const char* available(bool value) { return value ? "yes" : "no"; }

#if defined(TORRVIEW_HAVE_MPV)
constexpr bool has_mpv = true;
#else
constexpr bool has_mpv = false;
#endif

#if defined(TORRVIEW_HAVE_LIBTORRENT)
constexpr bool has_libtorrent = true;
#else
constexpr bool has_libtorrent = false;
#endif

#if defined(TORRVIEW_HAVE_FREETYPE)
constexpr bool has_freetype = true;
#else
constexpr bool has_freetype = false;
#endif

#if defined(TORRVIEW_HAVE_FONTCONFIG)
constexpr bool has_fontconfig = true;
#else
constexpr bool has_fontconfig = false;
#endif

} // namespace

class Application {
public:
  Application() = default;

  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  ~Application() {
    if (context_ != nullptr) {
      SDL_GL_DestroyContext(context_);
    }

    if (window_ != nullptr) {
      SDL_DestroyWindow(window_);
    }

    if (sdl_initialized_) {
      SDL_Quit();
    }
  }

  void initialize() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
      throw std::runtime_error(sdl_error("SDL_Init failed"));
    }
    sdl_initialized_ = true;

    set_gl_attribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    set_gl_attribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    set_gl_attribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    set_gl_attribute(SDL_GL_DOUBLEBUFFER, 1);
    set_gl_attribute(SDL_GL_DEPTH_SIZE, 0);
    set_gl_attribute(SDL_GL_STENCIL_SIZE, 8);

    constexpr int initial_width = 1280;
    constexpr int initial_height = 720;
    constexpr SDL_WindowFlags window_flags =
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

    window_ = SDL_CreateWindow("Torrview", initial_width, initial_height, window_flags);
    if (window_ == nullptr) {
      throw std::runtime_error(sdl_error("SDL_CreateWindow failed"));
    }

    context_ = SDL_GL_CreateContext(window_);
    if (context_ == nullptr) {
      throw std::runtime_error(sdl_error("SDL_GL_CreateContext failed"));
    }

    if (!SDL_GL_MakeCurrent(window_, context_)) {
      throw std::runtime_error(sdl_error("SDL_GL_MakeCurrent failed"));
    }

    if (!SDL_GL_SetSwapInterval(1)) {
      std::cerr << "Warning: SDL_GL_SetSwapInterval failed: " << SDL_GetError() << '\n';
    }

    log_startup();
  }

  void run() {
    bool running = true;
    while (running) {
      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
          running = false;
        }
      }

      render();
      SDL_Delay(1);
    }
  }

private:
  void set_gl_attribute(SDL_GLAttr attribute, int value) {
    if (!SDL_GL_SetAttribute(attribute, value)) {
      throw std::runtime_error(sdl_error("SDL_GL_SetAttribute failed"));
    }
  }

  void render() {
    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(window_, &width, &height)) {
      throw std::runtime_error(sdl_error("SDL_GetWindowSizeInPixels failed"));
    }

    glViewport(0, 0, width, height);
    glClearColor(0.015F, 0.018F, 0.022F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    SDL_GL_SwapWindow(window_);
  }

  void log_startup() const {
    std::cout << "Torrview " << TORRVIEW_VERSION << '\n';
    std::cout << "Dependencies: SDL3=yes OpenGL=yes Clay=yes"
              << " libmpv=" << available(has_mpv) << " libtorrent=" << available(has_libtorrent)
              << " freetype=" << available(has_freetype)
              << " fontconfig=" << available(has_fontconfig) << '\n';
  }

  bool sdl_initialized_ = false;
  SDL_Window* window_ = nullptr;
  SDL_GLContext context_ = nullptr;
};

} // namespace torrview

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  try {
    torrview::Application app;
    app.initialize();
    app.run();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }

  return 0;
}
