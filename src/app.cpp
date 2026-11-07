#include "app.hpp"

#include "config.hpp"
#include "logger.hpp"
#include "profiler.hpp"
#include "resources.hpp"

#include <thorvg.h>

#include <algorithm>
#include <cstdint>
#include <exception>
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

#if defined(TORRVIEW_HAVE_TRACY)
constexpr bool has_tracy = true;
#else
constexpr bool has_tracy = false;
#endif

constexpr SDL_DialogFileFilter source_filters[] = {
    {"Torrent and video files", "torrent;mkv;mp4;webm;avi;mov;m4v;mpg;mpeg;ts;m2ts;flv;wmv;ogv"},
    {"Video files", "mkv;mp4;webm;avi;mov;m4v;mpg;mpeg;ts;m2ts;flv;wmv;ogv"},
    {"Torrent files", "torrent"},
    {"All files", "*"},
};

constexpr auto render_frame_interval = std::chrono::milliseconds(16);

} // namespace

Application::~Application() {
  save_progress();
  library_.reset();
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
  const char* base_path = SDL_GetBasePath();
  Settings::instance().load(base_path);
  const auto library_path =
      (Settings::instance().path().parent_path() / "torrview.sqlite").u8string();
  library_ = std::make_unique<Library>(std::string(library_path.begin(), library_path.end()));

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
  apply_settings();
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
  navigate<ui::TitlePage>();
}

void Application::run() {
  running_ = true;
  while (running_) {
    const auto frame_started = Clock::now();
    event_manager_.pump(*this);
    consume_dialog_results();
    draw();
    const auto frame_elapsed = Clock::now() - frame_started;
    if (frame_elapsed < render_frame_interval) {
      SDL_Delay(static_cast<Uint32>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                        render_frame_interval - frame_elapsed)
                                        .count()));
    }
  }
}

void Application::request_quit() { running_ = false; }

void Application::window_metrics_changed() { update_window_metrics(); }

void Application::key_down(const SDL_KeyboardEvent& key) {
  SDL_Event event{};
  event.type = SDL_EVENT_KEY_DOWN;
  event.key = key;

  if (ui_app_.handle_event(event)) {
    return;
  }

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
    show_source_dialog();
    return;
  }

  if (command && key.key == SDLK_V) {
    paste_clipboard();
    return;
  }
}

void Application::text_input(const SDL_TextInputEvent& text) {
  if (text.text == nullptr) {
    return;
  }

  SDL_Event event{};
  event.type = SDL_EVENT_TEXT_INPUT;
  event.text = text;
  ui_app_.handle_event(event);
}

void Application::mouse_motion(const SDL_MouseMotionEvent& motion) {
  pointer_.x = motion.x;
  pointer_.y = motion.y;
  pointer_.buttons = motion.state;
  SDL_Event event{};
  event.type = SDL_EVENT_MOUSE_MOTION;
  event.motion = motion;
  ui_app_.handle_event(event);
}

void Application::mouse_button(const SDL_MouseButtonEvent& button) {
  pointer_.x = button.x;
  pointer_.y = button.y;
  if (button.down) {
    pointer_.buttons |= SDL_BUTTON_MASK(button.button);
  } else {
    pointer_.buttons &= ~SDL_BUTTON_MASK(button.button);
  }

  SDL_Event event{};
  event.type = button.down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
  event.button = button;
  ui_app_.handle_event(event);
}

void Application::mouse_wheel(const SDL_MouseWheelEvent& wheel) {
  pointer_.x = wheel.mouse_x;
  pointer_.y = wheel.mouse_y;
  const float direction = wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
  pointer_.wheel_x += wheel.x * direction;
  pointer_.wheel_y += wheel.y * direction;
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

  const auto* font = resources::find("fonts/NotoSans-Regular.ttf");
  if (font == nullptr ||
      font->size > static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())) {
    throw std::runtime_error("Bundled Noto Sans font resource is missing or too large");
  }

  if (tvg::Text::load(font_name_, reinterpret_cast<const char*>(font->data),
                      static_cast<uint32_t>(font->size), font->mime_type.data(),
                      true) != tvg::Result::Success) {
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
  text_measure_state_.text.reset(tvg::Text::gen());
  Clay_SetMeasureTextFunction(ui::measure_text, &text_measure_state_);
  register_pages();
  last_frame_time_ = Clock::now();
}

void Application::register_pages() {
  ui_app_.register_page<ui::TitlePage>([this] {
    return std::make_unique<ui::TitlePage>(*this, Settings::instance(), drag_active_, last_input_);
  });
  ui_app_.register_page<ui::FileBrowserPage>([this] {
    return std::make_unique<ui::FileBrowserPage>(*this, Settings::instance(), player_.get(),
                                                 torrent_service_);
  });
  ui_app_.register_page<ui::SettingsPage>([this] {
    return std::make_unique<ui::SettingsPage>(*this, Settings::instance(), player_.get(),
                                              torrent_service_);
  });
  ui_app_.register_page<ui::PlayerOverlay>([this] {
    return std::make_unique<ui::PlayerOverlay>(*this, Settings::instance(), pointer_, clay_context_,
                                               player_.get(), torrent_service_);
  });
}

float Application::text_width(std::string_view value, uint16_t font_size) {
  Clay_TextElementConfig config{};
  config.fontSize = font_size;
  return ui::measure_text({.length = static_cast<int32_t>(value.size()),
                           .chars = value.data(),
                           .baseChars = value.data()},
                          &config, &text_measure_state_)
      .width;
}

void Application::show_source_dialog() {
  SDL_ShowOpenFileDialog(open_file_dialog_callback, nullptr, window_, source_filters, 4, nullptr,
                         false);
}

TorrentRuntimeSettings Application::runtime_settings_from_app_settings() const {
  TorrentRuntimeSettings runtime;
  const AppSettings& app_settings = Settings::instance().values();
  runtime.upload_speed_kib = app_settings.upload_speed_kib;
  runtime.max_connections = app_settings.max_connections;
  runtime.proxy_url = app_settings.proxy_url;
  runtime.proxy_peer_connections = app_settings.proxy_peer_connections;
  runtime.proxy_tracker_connections = app_settings.proxy_tracker_connections;
  runtime.proxy_dns = app_settings.proxy_dns;
  return runtime;
}

void Application::apply_settings() {
  Settings::instance().replace(Settings::instance().values());
  torrent_service_.set_cache_limit_mib(Settings::instance().values().buffer_size_mib);
  torrent_service_.set_runtime_settings(runtime_settings_from_app_settings());
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

  save_progress();
  active_key_.clear();
  playing_file_ = -1;
  resume_pending_ = false;
  metadata_saved_ = false;
  library_status_.clear();
  last_input_ = std::move(input);
  if (last_input_->kind == InputKind::local_media_file) {
    torrent_service_.reset();
    if (player_ == nullptr || !player_->available()) {
      TORRVIEW_LOG_ERROR(
          "Local playback requires libmpv, but this build was configured without it");
    } else {
      try {
        player_->load_file(last_input_->value);
        navigate<ui::PlayerOverlay>();
      } catch (const std::exception& error) {
        TORRVIEW_LOG_ERROR("Local playback failed: " << error.what());
      }
    }
  } else if (last_input_->kind == InputKind::torrent_file ||
             last_input_->kind == InputKind::magnet_link) {
    if (player_ != nullptr && player_->has_file()) {
      player_->stop();
    }
    try {
      if (last_input_->kind == InputKind::torrent_file) {
        torrent_service_.load_torrent_file(last_input_->value);
      } else {
        torrent_service_.load_magnet(last_input_->value);
      }
      const auto torrent = torrent_service_.snapshot();
      if (torrent.state == TorrentLoadState::loading_metadata || torrent.has_metadata) {
        active_key_ = torrent.identity.empty() ? "source:" + last_input_->value : torrent.identity;
        if (!active_entry()) {
          LibraryEntry entry;
          entry.key = active_key_;
          entry.source = last_input_->value;
          entry.name = torrent.name.empty() ? "Magnet (waiting for metadata)" : torrent.name;
          library_->entries.insert(library_->entries.begin(), std::move(entry));
          library_->save();
        }
      }
      navigate<ui::FileBrowserPage>();
    } catch (const std::exception& error) {
      TORRVIEW_LOG_ERROR("Torrent metadata load failed: " << error.what());
    }
  } else {
    torrent_service_.reset();
    navigate<ui::TitlePage>();
  }

  TORRVIEW_LOG_INFO("Input: " << input_kind_label(last_input_->kind) << " - "
                              << compact_value(last_input_->value));
  update_window_title();
}

LibraryEntry* Application::active_entry() {
  if (!library_)
    return nullptr;
  for (auto& entry : library_->entries)
    if (entry.key == active_key_)
      return &entry;
  return nullptr;
}

void Application::save_progress() {
  auto* entry = active_entry();
  if (!entry || !player_ || playing_file_ < 0)
    return;
  const auto playback = player_->snapshot();
  if (!playback.loaded || !playback.file_ready || !playback.path.starts_with("torrview://") ||
      !playback.position_valid || playback.seeking || playback.duration <= 0)
    return;
  const auto old = entry->progress.find(playing_file_);
  if (old != entry->progress.end() && old->second.position == playback.time_pos &&
      old->second.duration == playback.duration && entry->last_file == playing_file_)
    return;
  entry->last_file = playing_file_;
  entry->progress[playing_file_] = {playback.time_pos, playback.duration};
  library_->save_progress(entry->key, playing_file_, entry->progress[playing_file_]);
  progress_saved_ = Clock::now();
}

void Application::update_library() {
  if (!library_)
    return;
  const auto torrent = torrent_service_.snapshot();
  if (!metadata_saved_ && torrent.has_metadata && torrent.metadata) {
    const auto provisional_key = active_key_;
    active_key_ = torrent.identity;
    if (provisional_key != active_key_) {
      auto provisional = std::find_if(library_->entries.begin(), library_->entries.end(),
                                      [&](const auto& e) { return e.key == provisional_key; });
      if (provisional != library_->entries.end()) {
        if (!active_entry()) {
          provisional->key = active_key_;
          if (provisional->metadata.empty())
            provisional->name = torrent.name;
        } else
          library_->entries.erase(provisional);
      }
    }
    auto* entry = active_entry();
    if (!entry) {
      LibraryEntry fresh;
      fresh.key = active_key_;
      fresh.name = torrent.name;
      fresh.source = torrent.source;
      library_->entries.insert(library_->entries.begin(), std::move(fresh));
      entry = active_entry();
    }
    entry->files = torrent.files;
    entry->metadata = *torrent.metadata;
    metadata_saved_ = true;
    library_->save();
  }
  if (resume_pending_ && torrent.has_metadata) {
    if (auto* entry = active_entry()) {
      int index = entry->last_file;
      if (index < 0) {
        for (const auto& file : entry->files)
          if (file.likely_video) {
            index = file.index;
            break;
          }
      }
      if (index < 0) {
        resume_pending_ = false;
        library_status_ = "No video files in this torrent";
      } else if (!torrent_service_.stream_uri_for_file(index).empty()) {
        resume_pending_ = false;
        play_torrent_file(index);
      }
    }
  }
  if (Clock::now() - progress_saved_ >= std::chrono::seconds(1))
    save_progress();
}

void Application::open_library(const std::string& key, bool resume) {
  save_progress();
  active_key_ = key;
  auto* entry = active_entry();
  if (!entry)
    return;
  last_input_ = parse_text_input(entry->source);
  playing_file_ = -1;
  resume_pending_ = resume;
  metadata_saved_ = true;
  library_status_.clear();
  try {
    if (player_ && player_->has_file())
      player_->stop();
    if (!entry->metadata.empty())
      torrent_service_.load_torrent_data(entry->metadata, entry->source);
    else {
      metadata_saved_ = false;
      if (is_magnet_link(entry->source))
        torrent_service_.load_magnet(entry->source);
      else
        torrent_service_.load_torrent_file(entry->source);
    }
    navigate<ui::FileBrowserPage>();
    update_window_title();
  } catch (const std::exception& e) {
    library_status_ = e.what();
    resume_pending_ = false;
  }
}

TorrentSnapshot Application::browsing_snapshot() const {
  auto snapshot = torrent_service_.snapshot();
  if (library_)
    for (const auto& entry : library_->entries)
      if (entry.key == active_key_) {
        snapshot.name = entry.name;
        if (snapshot.files.empty()) {
          snapshot.files = entry.files;
          snapshot.video_files.clear();
          for (const auto& file : entry.files)
            if (file.likely_video)
              snapshot.video_files.push_back(file);
        }
        break;
      }
  if (!library_status_.empty()) {
    snapshot.status = library_status_;
    snapshot.error = library_status_;
  }
  return snapshot;
}

VideoProgress Application::file_progress(int index) const {
  if (library_)
    for (const auto& entry : library_->entries)
      if (entry.key == active_key_) {
        auto p = entry.progress.find(index);
        if (p != entry.progress.end())
          return p->second;
      }
  return {};
}

void Application::play_torrent_file(int index) {
  save_progress();
  resume_pending_ = false;
  if (!player_ || !player_->available()) {
    library_status_ = "Playback requires libmpv";
    return;
  }
  const auto snapshot = browsing_snapshot();
  const auto* file = find_torrent_file(snapshot, index);
  if (!file || !file->likely_video) {
    library_status_ = "Select a video file to play";
    return;
  }
  const auto uri = torrent_service_.stream_uri_for_file(index);
  if (uri.empty()) {
    library_status_ = "Torrent is still connecting. Try again shortly.";
    return;
  }
  try {
    player_->load_file(uri, true, file_progress(index).position);
    playing_file_ = index;
    progress_saved_ = Clock::now();
    library_status_.clear();
    navigate<ui::PlayerOverlay>();
    update_window_title();
  } catch (const std::exception& e) {
    library_status_ = e.what();
  }
}

void Application::rename_library(const std::string& key, const std::string& name) {
  if (trim(name).empty())
    return;
  for (auto& entry : library_->entries)
    if (entry.key == key)
      entry.name = trim(name);
  library_->save();
}

void Application::delete_library(const std::string& key) {
  if (key == active_key_) {
    active_key_.clear();
    playing_file_ = -1;
    resume_pending_ = false;
    if (player_ && player_->has_file())
      player_->stop();
    torrent_service_.reset();
    metadata_saved_ = true;
  }
  std::erase_if(library_->entries, [&](const auto& entry) { return entry.key == key; });
  library_->save();
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
    const TorrentSnapshot& torrent = torrent_service_.snapshot();
    if (torrent.active_file_index >= 0) {
      const TorrentFileInfo* file = find_torrent_file(torrent, torrent.active_file_index);
      title << " - " << compact_value(file != nullptr ? file->path : "torrent playback", 72);
    }
  } else {
    title << " - Drop .torrent, paste magnet, or press Ctrl+O";
  }

  SDL_SetWindowTitle(window_, title.str().c_str());
}

void Application::draw() {
  basic_gl_.viewport(0, 0, metrics_.pixel_width, metrics_.pixel_height);
  basic_gl_.disable(GL_DEPTH_TEST);
  basic_gl_.disable(GL_SCISSOR_TEST);
  basic_gl_.clear_color(12.0F / 255.0F, 13.0F / 255.0F, 15.0F / 255.0F, 1.0F);
  basic_gl_.clear(GL_COLOR_BUFFER_BIT);

  const auto now = Clock::now();
  current_delta_time_ = std::chrono::duration<float>(now - last_frame_time_).count();
  last_frame_time_ = now;

  if (player_ != nullptr) {
    player_->process_events();
  }
  torrent_service_.process_alerts();
  update_library();

  Clay_SetCurrentContext(clay_context_);
  Clay_SetLayoutDimensions({.width = static_cast<float>(metrics_.logical_width),
                            .height = static_cast<float>(metrics_.logical_height)});
  Clay_SetPointerState({.x = pointer_.x, .y = pointer_.y},
                       (pointer_.buttons & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) != 0);
  // SDL reports wheel-up as positive y; Clay's scroll position is <= 0 and a positive delta scrolls
  // toward the top, so the y sign passes through unchanged. Clay scales the delta by 10 (~60px).
  Clay_UpdateScrollContainers(true, {.x = -pointer_.wheel_x * 6.0F, .y = pointer_.wheel_y * 6.0F},
                              current_delta_time_);
  pointer_.wheel_x = 0.0F;
  pointer_.wheel_y = 0.0F;

  ui::RenderContext render_context(clay_context_, metrics_, context_, basic_gl_);
  Clay_BeginLayout();
  ui_app_.draw(render_context);
  Clay_RenderCommandArray commands = Clay_EndLayout(current_delta_time_);
  clay_renderer_->render(commands);

  SDL_GL_SwapWindow(window_);
  if (player_ != nullptr && player_->has_file()) {
    player_->report_swap();
  }
  TORRVIEW_PROFILE_FRAME();
}

void Application::log_startup() const {
  TORRVIEW_LOG_INFO("Torrview " << TORRVIEW_VERSION);
  TORRVIEW_LOG_INFO("Dependencies: SDL3=yes OpenGL=yes Clay=yes"
                    << " libmpv=" << available(has_mpv) << " libtorrent="
                    << available(has_libtorrent) << " freetype=" << available(has_freetype)
                    << " fontconfig=" << available(has_fontconfig)
                    << " thorvg=" << available(has_thorvg) << " tracy=" << available(has_tracy));
  TORRVIEW_LOG_INFO("Input shell: drop .torrent files, local video files, or magnet text; press "
                    "Ctrl+O to open a source file, Ctrl+V to paste a magnet link");
  TORRVIEW_LOG_INFO("Playback controls: Space play/pause, Left/Right seek, PageUp/PageDown long "
                    "seek, Up/Down volume, A audio track, F/F11 fullscreen");
}

} // namespace torrview
