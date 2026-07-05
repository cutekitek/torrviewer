#include "app.hpp"

#include "config.hpp"
#include "logger.hpp"

#include <thorvg.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace torrview {
namespace {

struct DialogResultQueue {
  std::mutex mutex;
  std::vector<std::string> paths;
  std::vector<std::string> errors;
};

DialogResultQueue g_dialog_results;

std::string sdl_error(const char* action) {
  std::string message = action;
  message += ": ";
  message += SDL_GetError();
  return message;
}

const char* available(bool value) { return value ? "yes" : "no"; }

const TorrentFileInfo* find_torrent_file(const TorrentSnapshot& snapshot, int file_index) {
  for (const TorrentFileInfo& file : snapshot.files) {
    if (file.index == file_index) {
      return &file;
    }
  }
  return nullptr;
}

std::vector<char> read_binary_file(const char* path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    throw std::runtime_error(std::string("Failed to open font: ") + path);
  }

  const std::streamsize size = file.tellg();
  if (size <= 0 || static_cast<unsigned long long>(size) >
                       static_cast<unsigned long long>(std::numeric_limits<uint32_t>::max())) {
    throw std::runtime_error(std::string("Invalid font size: ") + path);
  }

  std::vector<char> data(static_cast<std::size_t>(size));
  file.seekg(0, std::ios::beg);
  if (!file.read(data.data(), size)) {
    throw std::runtime_error(std::string("Failed to read font: ") + path);
  }

  return data;
}

void SDLCALL open_file_dialog_callback(void*, const char* const* filelist, int) {
  std::lock_guard<std::mutex> lock(g_dialog_results.mutex);

  if (filelist == nullptr) {
    g_dialog_results.errors.emplace_back(SDL_GetError());
    return;
  }

  if (*filelist == nullptr) {
    return;
  }

  g_dialog_results.paths.emplace_back(*filelist);
}

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

#if defined(TORRVIEW_HAVE_THORVG)
constexpr bool has_thorvg = true;
#else
constexpr bool has_thorvg = false;
#endif

constexpr SDL_DialogFileFilter source_filters[] = {
    {"Torrent and video files", "torrent;mkv;mp4;webm;avi;mov;m4v;mpg;mpeg;ts;m2ts;flv;wmv;ogv"},
    {"Video files", "mkv;mp4;webm;avi;mov;m4v;mpg;mpeg;ts;m2ts;flv;wmv;ogv"},
    {"Torrent files", "torrent"},
    {"All files", "*"},
};

constexpr float player_controls_reveal_band = 72.0F;
constexpr auto player_controls_hide_delay = std::chrono::milliseconds(2400);

} // namespace

Application::~Application() {
  player_.reset();

  if (context_ != nullptr) {
    destroy_text_renderer();
  }

  if (thorvg_initialized_) {
    tvg::Initializer::term();
  }

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

void Application::initialize() {
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
    TORRVIEW_LOG_WARNING("SDL_GL_SetSwapInterval failed: " << SDL_GetError());
  }

  update_window_metrics();
  load_basic_gl_api();
  initialize_text_renderer();
  initialize_clay();
  player_ = std::make_unique<MpvPlayer>();
  player_->set_torrent_stream_provider(&torrent_service_);
  torrent_service_.set_cache_limit_mib(cache_limit_mib_);
  if (player_->available()) {
    player_->initialize();
  }

  SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, true);
  SDL_SetEventEnabled(SDL_EVENT_DROP_TEXT, true);
  SDL_SetEventEnabled(SDL_EVENT_DROP_BEGIN, true);
  SDL_SetEventEnabled(SDL_EVENT_DROP_COMPLETE, true);
  SDL_SetEventEnabled(SDL_EVENT_DROP_POSITION, true);

  if (!SDL_StartTextInput(window_)) {
    TORRVIEW_LOG_WARNING("SDL_StartTextInput failed: " << SDL_GetError());
  }

  update_window_title();
  log_startup();
}

void Application::run() {
  running_ = true;
  while (running_) {
    event_manager_.pump(*this);
    consume_dialog_results();
    render();
    SDL_Delay(1);
  }
}

void Application::request_quit() { running_ = false; }

void Application::window_metrics_changed() { update_window_metrics(); }

void Application::key_down(const SDL_KeyboardEvent& key) {
  if (key.repeat) {
    return;
  }

  const bool command = (key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;

  if (key.key == SDLK_ESCAPE) {
    if (is_fullscreen()) {
      toggle_fullscreen();
      return;
    }
    running_ = false;
    return;
  }

  if (command && key.key == SDLK_O) {
    show_open_source_dialog();
    return;
  }

  if (command && key.key == SDLK_V) {
    paste_clipboard();
    return;
  }

  if (player_ != nullptr && player_->has_file()) {
    switch (key.key) {
    case SDLK_SPACE:
      player_->toggle_pause();
      break;
    case SDLK_LEFT:
      player_->seek_relative(-10.0);
      break;
    case SDLK_RIGHT:
      player_->seek_relative(30.0);
      break;
    case SDLK_HOME:
      player_->seek_absolute(0.0);
      break;
    case SDLK_END:
      player_->seek_absolute(player_->snapshot().duration);
      break;
    case SDLK_PAGEUP:
      player_->seek_relative(300.0);
      break;
    case SDLK_PAGEDOWN:
      player_->seek_relative(-300.0);
      break;
    case SDLK_UP:
      player_->adjust_volume(5.0);
      break;
    case SDLK_DOWN:
      player_->adjust_volume(-5.0);
      break;
    case SDLK_A:
      player_->select_audio_track_relative(1);
      break;
    case SDLK_F:
    case SDLK_F11:
      toggle_fullscreen();
      break;
    default:
      break;
    }
  }
}

void Application::mouse_motion(const SDL_MouseMotionEvent& motion) {
  pointer_.x = motion.x;
  pointer_.y = motion.y;
  pointer_.buttons = motion.state;
  if (dragging_volume_) {
    update_volume_from_pointer();
  }
}

void Application::mouse_button(const SDL_MouseButtonEvent& button) {
  pointer_.x = button.x;
  pointer_.y = button.y;
  if (button.button != SDL_BUTTON_LEFT) {
    if (button.down) {
      pointer_.buttons |= SDL_BUTTON_MASK(button.button);
    } else {
      pointer_.buttons &= ~SDL_BUTTON_MASK(button.button);
    }
    return;
  }

  if (button.down) {
    pointer_.buttons |= SDL_BUTTON_MASK(button.button);
    if (clay_context_ != nullptr && player_ != nullptr && player_->has_file()) {
      Clay_SetCurrentContext(clay_context_);
      if (player_overlay_.pointer_over_volume_bar()) {
        dragging_volume_ = true;
        update_volume_from_pointer();
      }
    }
    return;
  }

  pointer_.buttons &= ~SDL_BUTTON_MASK(button.button);
  if (dragging_volume_) {
    update_volume_from_pointer();
    dragging_volume_ = false;
    return;
  }

  if (clay_context_ == nullptr) {
    return;
  }

  Clay_SetCurrentContext(clay_context_);
  if (player_ != nullptr && player_->has_file()) {
    handle_player_click();
    return;
  }

  if (torrent_service_.snapshot().state != TorrentLoadState::idle) {
    handle_file_browser_click();
    return;
  }

  if (Clay_PointerOver(title_page_.source_panel_id())) {
    show_open_source_dialog();
  }
}

void Application::mouse_wheel(const SDL_MouseWheelEvent& wheel) {
  pointer_.x = wheel.mouse_x;
  pointer_.y = wheel.mouse_y;
  pointer_.wheel_x += wheel.x;
  pointer_.wheel_y += wheel.y;
}

void Application::drop_file(const char* path) { accept_input(parse_file_input(path)); }

void Application::drop_text(const char* text) { accept_input(parse_text_input(text)); }

void Application::drop_begin() { drag_active_ = true; }

void Application::drop_complete() { drag_active_ = false; }

void Application::drop_position(float x, float y) {
  pointer_.x = x;
  pointer_.y = y;
}

void Application::set_gl_attribute(SDL_GLAttr attribute, int value) {
  if (!SDL_GL_SetAttribute(attribute, value)) {
    throw std::runtime_error(sdl_error("SDL_GL_SetAttribute failed"));
  }
}

template <typename Function> Function Application::load_gl_proc(const char* name) {
  auto* pointer = SDL_GL_GetProcAddress(name);
  if (pointer == nullptr) {
    throw std::runtime_error(std::string("Missing OpenGL function: ") + name);
  }
  return reinterpret_cast<Function>(pointer);
}

void Application::load_basic_gl_api() {
  basic_gl_.viewport = load_gl_proc<GlViewportProc>("glViewport");
  basic_gl_.scissor = load_gl_proc<GlScissorProc>("glScissor");
  basic_gl_.clear_color = load_gl_proc<GlClearColorProc>("glClearColor");
  basic_gl_.clear = load_gl_proc<GlClearProc>("glClear");
  basic_gl_.enable = load_gl_proc<GlEnableProc>("glEnable");
  basic_gl_.disable = load_gl_proc<GlDisableProc>("glDisable");
}

void Application::initialize_text_renderer() {
  if (tvg::Initializer::init(0) != tvg::Result::Success) {
    throw std::runtime_error("ThorVG initialization failed");
  }
  thorvg_initialized_ = true;

  font_data_ = read_binary_file(TORRVIEW_FONT_PATH);
  if (tvg::Text::load(font_name_, font_data_.data(), static_cast<uint32_t>(font_data_.size()),
                      "ttf", true) != tvg::Result::Success) {
    throw std::runtime_error("ThorVG failed to load bundled Noto Sans font");
  }

  clay_renderer_ = std::make_unique<ui::ClayRenderer>(context_, basic_gl_, metrics_, font_name_);
}

void Application::destroy_text_renderer() {
  clay_renderer_.reset();
  if (thorvg_initialized_) {
    tvg::Text::load(font_name_, nullptr, 0);
  }
}

void Application::initialize_clay() {
  const uint32_t clay_memory_size = Clay_MinMemorySize();
  clay_memory_.resize(clay_memory_size);
  clay_arena_ = Clay_CreateArenaWithCapacityAndMemory(clay_memory_.size(), clay_memory_.data());

  clay_context_ =
      Clay_Initialize(clay_arena_,
                      {.width = static_cast<float>(metrics_.logical_width),
                       .height = static_cast<float>(metrics_.logical_height)},
                      {.errorHandlerFunction = ui::handle_clay_error, .userData = nullptr});
  if (clay_context_ == nullptr) {
    throw std::runtime_error("Clay initialization failed");
  }

  text_measure_state_.font_name = font_name_;
  Clay_SetMeasureTextFunction(ui::measure_text, &text_measure_state_);
  title_page_.initialize_ids();
  file_browser_page_.initialize_ids();
  player_overlay_.initialize_ids();
  last_frame_time_ = Clock::now();
  player_controls_last_hover_ = last_frame_time_;
}

void Application::show_open_source_dialog() {
  SDL_ShowOpenFileDialog(open_file_dialog_callback, nullptr, window_, source_filters, 4, nullptr,
                         false);
}

void Application::paste_clipboard() {
  char* clipboard = SDL_GetClipboardText();
  if (clipboard == nullptr) {
    TORRVIEW_LOG_ERROR("Clipboard paste failed: " << SDL_GetError());
    return;
  }

  std::string text = clipboard;
  SDL_free(clipboard);

  if (!trim(text).empty()) {
    accept_input(parse_text_input(text));
  }
}

void Application::consume_dialog_results() {
  std::vector<std::string> paths;
  std::vector<std::string> errors;

  {
    std::lock_guard<std::mutex> lock(g_dialog_results.mutex);
    paths.swap(g_dialog_results.paths);
    errors.swap(g_dialog_results.errors);
  }

  for (const std::string& error : errors) {
    TORRVIEW_LOG_ERROR("Open file dialog failed: " << error);
  }

  for (const std::string& path : paths) {
    accept_input(parse_file_input(path));
  }
}

void Application::accept_input(ParsedInput input) {
  if (input.value.empty()) {
    return;
  }

  last_input_ = std::move(input);
  if (last_input_->kind == InputKind::local_media_file) {
    torrent_service_.reset();
    selected_torrent_file_index_.reset();
    if (player_ == nullptr || !player_->available()) {
      TORRVIEW_LOG_ERROR("Local playback requires libmpv, but this build was configured without it");
    } else {
      try {
        player_->load_file(last_input_->value);
        player_controls_visible_ = true;
        player_controls_last_hover_ = Clock::now();
      } catch (const std::exception& error) {
        TORRVIEW_LOG_ERROR("Local playback failed: " << error.what());
      }
    }
  } else if (last_input_->kind == InputKind::torrent_file ||
             last_input_->kind == InputKind::magnet_link) {
    if (player_ != nullptr && player_->has_file()) {
      player_->stop();
    }
    selected_torrent_file_index_.reset();
    try {
      if (last_input_->kind == InputKind::torrent_file) {
        torrent_service_.load_torrent_file(last_input_->value);
      } else {
        torrent_service_.load_magnet(last_input_->value);
      }
    } catch (const std::exception& error) {
      TORRVIEW_LOG_ERROR("Torrent metadata load failed: " << error.what());
    }
  } else {
    torrent_service_.reset();
    selected_torrent_file_index_.reset();
  }

  TORRVIEW_LOG_INFO("Input: " << input_kind_label(last_input_->kind) << " - "
                              << compact_value(last_input_->value));
  update_window_title();
}

void Application::handle_file_browser_click() {
  switch (file_browser_page_.hit_test_click()) {
  case ui::FileBrowserAction::open_source:
    show_open_source_dialog();
    break;
  case ui::FileBrowserAction::select_file:
    selected_torrent_file_index_ = file_browser_page_.selected_file_index();
    TORRVIEW_LOG_INFO("Selected torrent file index " << *selected_torrent_file_index_);
    if (player_ == nullptr || !player_->available()) {
      TORRVIEW_LOG_ERROR("Torrent playback requires libmpv, but this build was configured without it");
    } else {
      const std::string uri = torrent_service_.stream_uri_for_file(*selected_torrent_file_index_);
      if (uri.empty()) {
        TORRVIEW_LOG_ERROR("Torrent stream is not available for selected file");
      } else {
        try {
          player_->load_file(uri);
          player_controls_visible_ = true;
          player_controls_last_hover_ = Clock::now();
        } catch (const std::exception& error) {
          TORRVIEW_LOG_ERROR("Torrent playback failed: " << error.what());
        }
      }
    }
    update_window_title();
    break;
  case ui::FileBrowserAction::none:
    break;
  }
}

void Application::handle_player_click() {
  if (!player_controls_visible_) {
    return;
  }

  handle_player_action(player_overlay_.hit_test_click());
}

void Application::handle_player_action(ui::PlayerOverlayAction action) {
  if (player_ == nullptr) {
    return;
  }

  switch (action) {
  case ui::PlayerOverlayAction::toggle_pause:
    player_->toggle_pause();
    break;
  case ui::PlayerOverlayAction::seek_back:
    player_->seek_relative(-10.0);
    break;
  case ui::PlayerOverlayAction::seek_forward:
    player_->seek_relative(30.0);
    break;
  case ui::PlayerOverlayAction::seek_absolute:
    player_->seek_absolute(player_overlay_.selected_seek_seconds());
    break;
  case ui::PlayerOverlayAction::set_volume:
    player_->set_volume(player_overlay_.volume_from_pointer(pointer_.x));
    break;
  case ui::PlayerOverlayAction::toggle_audio_menu:
    break;
  case ui::PlayerOverlayAction::toggle_buffer_menu:
    break;
  case ui::PlayerOverlayAction::select_audio_track:
    player_->select_audio_track(player_overlay_.selected_audio_track_index());
    break;
  case ui::PlayerOverlayAction::set_cache_size:
    cache_limit_mib_ = player_overlay_.selected_cache_size_mib();
    torrent_service_.set_cache_limit_mib(cache_limit_mib_);
    TORRVIEW_LOG_INFO("Cache limit set to " << cache_limit_mib_ << " MiB");
    break;
  case ui::PlayerOverlayAction::fullscreen:
    toggle_fullscreen();
    break;
  case ui::PlayerOverlayAction::stop:
    player_->stop();
    update_window_title();
    break;
  case ui::PlayerOverlayAction::none:
    break;
  }
}

void Application::update_volume_from_pointer() {
  if (player_ == nullptr) {
    return;
  }

  player_->set_volume(player_overlay_.volume_from_pointer(pointer_.x));
}

void Application::update_player_controls_visibility(Clock::time_point now) {
  if (dragging_volume_) {
    player_controls_visible_ = true;
    player_controls_last_hover_ = now;
    return;
  }

  const bool pointer_in_reveal_band =
      pointer_.y >= static_cast<float>(metrics_.logical_height) - player_controls_reveal_band;
  if (!player_controls_visible_ && pointer_in_reveal_band) {
    player_controls_visible_ = true;
    player_controls_last_hover_ = now;
    return;
  }

  if (!player_controls_visible_) {
    return;
  }

  if (pointer_in_reveal_band || player_overlay_.pointer_over_controls()) {
    player_controls_last_hover_ = now;
    return;
  }

  if (now - player_controls_last_hover_ >= player_controls_hide_delay) {
    player_controls_visible_ = false;
    player_overlay_.close_menus();
  }
}

void Application::return_to_torrent_file_browser_after_eof() {
  if (player_ == nullptr) {
    return;
  }

  const PlaybackSnapshot snapshot = player_->snapshot();
  if (!snapshot.eof_reached || !snapshot.path.starts_with("torrview://")) {
    return;
  }

  TORRVIEW_LOG_INFO("Torrent playback ended; returning to file browser");
  player_->stop();
  player_controls_visible_ = true;
  player_overlay_.close_menus();
  update_window_title();
}

void Application::toggle_fullscreen() {
  const bool target = !is_fullscreen();
  if (!SDL_SetWindowFullscreen(window_, target)) {
    TORRVIEW_LOG_ERROR("SDL_SetWindowFullscreen failed: " << SDL_GetError());
  }
}

bool Application::is_fullscreen() const {
  return window_ != nullptr && (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
}

void Application::update_window_metrics() {
  if (!SDL_GetWindowSize(window_, &metrics_.logical_width, &metrics_.logical_height)) {
    throw std::runtime_error(sdl_error("SDL_GetWindowSize failed"));
  }

  if (!SDL_GetWindowSizeInPixels(window_, &metrics_.pixel_width, &metrics_.pixel_height)) {
    throw std::runtime_error(sdl_error("SDL_GetWindowSizeInPixels failed"));
  }

  metrics_.display_scale = SDL_GetWindowDisplayScale(window_);
  if (metrics_.display_scale <= 0.0F) {
    metrics_.display_scale = 1.0F;
  }
}

void Application::update_window_title() {
  std::ostringstream title;
  title << "Torrview";

  if (last_input_.has_value()) {
    title << " - " << input_kind_label(last_input_->kind) << ": "
          << compact_value(last_input_->value, 72);
    if (selected_torrent_file_index_.has_value()) {
      const TorrentSnapshot& torrent = torrent_service_.snapshot();
      const TorrentFileInfo* file = find_torrent_file(torrent, *selected_torrent_file_index_);
      title << " - "
            << compact_value(file != nullptr ? file->path
                                             : "file " + std::to_string(*selected_torrent_file_index_),
                             72);
    }
  } else {
    title << " - Drop .torrent, paste magnet, or press Ctrl+O";
  }

  SDL_SetWindowTitle(window_, title.str().c_str());
}

void Application::render() {
  basic_gl_.viewport(0, 0, metrics_.pixel_width, metrics_.pixel_height);
  basic_gl_.disable(GL_DEPTH_TEST);
  basic_gl_.disable(GL_SCISSOR_TEST);
  basic_gl_.clear_color(12.0F / 255.0F, 13.0F / 255.0F, 15.0F / 255.0F, 1.0F);
  basic_gl_.clear(GL_COLOR_BUFFER_BIT);

  const auto now = Clock::now();
  const float delta_time = std::chrono::duration<float>(now - last_frame_time_).count();
  last_frame_time_ = now;

  if (player_ != nullptr) {
    player_->process_events();
  }
  return_to_torrent_file_browser_after_eof();
  torrent_service_.process_alerts();

  Clay_SetCurrentContext(clay_context_);
  Clay_SetLayoutDimensions({.width = static_cast<float>(metrics_.logical_width),
                            .height = static_cast<float>(metrics_.logical_height)});
  Clay_SetPointerState({.x = pointer_.x, .y = pointer_.y},
                       (pointer_.buttons & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) != 0);
  Clay_UpdateScrollContainers(true, {.x = pointer_.wheel_x * 24.0F, .y = -pointer_.wheel_y * 24.0F},
                              delta_time);
  pointer_.wheel_x = 0.0F;
  pointer_.wheel_y = 0.0F;

  if (player_ != nullptr && player_->has_file()) {
    render_player_screen(delta_time);
  } else if (torrent_service_.snapshot().state != TorrentLoadState::idle) {
    render_file_browser_screen(delta_time);
  } else {
    render_title_screen(delta_time);
  }

  SDL_GL_SwapWindow(window_);
  if (player_ != nullptr && player_->has_file()) {
    player_->report_swap();
  }
}

void Application::render_title_screen(float delta_time) {
  title_page_.begin_frame();
  Clay_BeginLayout();
  title_page_.build(metrics_, drag_active_, last_input_);
  Clay_RenderCommandArray commands = Clay_EndLayout(delta_time);
  clay_renderer_->render(commands);
}

void Application::render_file_browser_screen(float delta_time) {
  file_browser_page_.begin_frame();
  Clay_BeginLayout();
  file_browser_page_.build(metrics_, torrent_service_.snapshot(), selected_torrent_file_index_);
  Clay_RenderCommandArray commands = Clay_EndLayout(delta_time);
  clay_renderer_->render(commands);
}

void Application::render_player_screen(float delta_time) {
  basic_gl_.viewport(0, 0, metrics_.pixel_width, metrics_.pixel_height);
  player_->render(metrics_);
  basic_gl_.viewport(0, 0, metrics_.pixel_width, metrics_.pixel_height);
  basic_gl_.disable(GL_DEPTH_TEST);
  basic_gl_.disable(GL_SCISSOR_TEST);

  player_overlay_.begin_frame();
  Clay_BeginLayout();
  player_overlay_.build(metrics_, player_->snapshot(), torrent_service_.snapshot(), is_fullscreen(),
                        player_controls_visible_);
  Clay_RenderCommandArray commands = Clay_EndLayout(delta_time);
  update_player_controls_visibility(Clock::now());
  clay_renderer_->render(commands);
}

void Application::log_startup() const {
  TORRVIEW_LOG_INFO("Torrview " << TORRVIEW_VERSION);
  TORRVIEW_LOG_INFO("Dependencies: SDL3=yes OpenGL=yes Clay=yes"
                    << " libmpv=" << available(has_mpv)
                    << " libtorrent=" << available(has_libtorrent)
                    << " freetype=" << available(has_freetype)
                    << " fontconfig=" << available(has_fontconfig)
                    << " thorvg=" << available(has_thorvg));
  TORRVIEW_LOG_INFO("Input shell: drop .torrent files, local video files, or magnet text; press "
                    "Ctrl+O to open a source file, Ctrl+V to paste a magnet link");
  TORRVIEW_LOG_INFO("Playback controls: Space play/pause, Left/Right seek, PageUp/PageDown long "
                    "seek, Up/Down volume, A audio track, F/F11 fullscreen");
}

} // namespace torrview
