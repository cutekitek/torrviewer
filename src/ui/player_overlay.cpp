#include "ui/player_overlay.hpp"

#include "../app.hpp"
#include "logger.hpp"
#include "ui/render_context.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace torrview::ui {
namespace {

constexpr float control_size = 36.0F;
constexpr float volume_bar_width = 96.0F;
constexpr float controls_reveal_band = 72.0F;
constexpr auto controls_hide_delay = std::chrono::milliseconds(2400);
constexpr auto bitrate_sample_interval = std::chrono::milliseconds(500);
constexpr std::size_t max_bitrate_samples = 96;
constexpr float bitrate_graph_height = 72.0F;
constexpr float stat_key_width = 76.0F;

const TorrentFileInfo* find_torrent_file(const TorrentSnapshot& torrent, int file_index) {
  for (const TorrentFileInfo& file : torrent.files) {
    if (file.index == file_index) {
      return &file;
    }
  }
  return nullptr;
}

std::string filename_from_path(std::string_view value) {
  if (value.empty()) {
    return "Local video";
  }
  std::filesystem::path path{std::u8string(value.begin(), value.end())};
  const std::u8string filename = path.filename().u8string();
  return filename.empty() ? std::string(value) : std::string(filename.begin(), filename.end());
}

std::string playback_title(const PlaybackSnapshot& snapshot, const TorrentSnapshot& torrent) {
  if (snapshot.path.starts_with("torrview://")) {
    if (const TorrentFileInfo* file = find_torrent_file(torrent, torrent.active_file_index);
        file != nullptr) {
      return filename_from_path(file->path);
    }
    if (!snapshot.media_title.empty()) {
      return snapshot.media_title;
    }
  }
  return filename_from_path(snapshot.path);
}

std::string format_bitrate(double bits_per_second) {
  const double value = std::max(0.0, bits_per_second);
  std::ostringstream out;
  if (value >= 1000.0 * 1000.0) {
    out << std::fixed << std::setprecision(value >= 10.0 * 1000.0 * 1000.0 ? 1 : 2)
        << value / (1000.0 * 1000.0) << " Mb/s";
  } else if (value >= 1000.0) {
    out << std::fixed << std::setprecision(0) << value / 1000.0 << " kb/s";
  } else if (value > 0.0) {
    out << std::fixed << std::setprecision(0) << value << " b/s";
  } else {
    out << "\xE2\x80\x93";
  }
  return out.str();
}

std::string metadata_value(std::string_view value) {
  return value.empty() ? std::string("\xE2\x80\x93") : std::string(value);
}

Clay_Color piece_color(TorrentPieceState state) {
  switch (state) {
  case TorrentPieceState::missing:
    return theme::piece_missing;
  case TorrentPieceState::requested:
  case TorrentPieceState::partial:
    return theme::signal;
  case TorrentPieceState::verified:
  case TorrentPieceState::retained:
    return theme::piece_have;
  case TorrentPieceState::evictable:
    return theme::piece_evictable;
  case TorrentPieceState::evicted:
    return theme::piece_evicted;
  case TorrentPieceState::failed:
    return theme::fault;
  }
  return theme::piece_missing;
}

int piece_state_rank(TorrentPieceState state) {
  switch (state) {
  case TorrentPieceState::failed:
    return 7;
  case TorrentPieceState::requested:
  case TorrentPieceState::partial:
    return 6;
  case TorrentPieceState::retained:
    return 5;
  case TorrentPieceState::verified:
    return 4;
  case TorrentPieceState::evictable:
    return 3;
  case TorrentPieceState::evicted:
    return 2;
  case TorrentPieceState::missing:
    return 1;
  }
  return 0;
}

TorrentPieceState dominant_piece_state(const std::vector<TorrentPieceState>& states,
                                       std::size_t begin, std::size_t end) {
  TorrentPieceState result = TorrentPieceState::missing;
  int best_rank = -1;
  for (std::size_t index = begin; index < end; ++index) {
    const int rank = piece_state_rank(states[index]);
    if (rank > best_rank) {
      best_rank = rank;
      result = states[index];
    }
  }
  return result;
}

bool has_torrent_playback_state(const TorrentSnapshot& torrent) {
  return torrent.active_file_index >= 0 || !torrent.piece_states.empty() || torrent.buffering ||
         torrent.stalled || torrent.state == TorrentLoadState::error ||
         torrent.state == TorrentLoadState::unavailable;
}

} // namespace

PlayerOverlay::PlayerOverlay(Application& app, Settings& settings, PointerState& pointer,
                             Clay_Context* clay_context, MpvPlayer* player,
                             TorrentService& torrent_service)
    : app_(app), settings_(settings), pointer_(pointer), clay_context_(clay_context),
      player_(player), torrent_service_(torrent_service), ui_(settings),
      controls_last_hover_(std::chrono::steady_clock::now()) {
  initialize_ids();
}

void PlayerOverlay::open() {
  controls_visible_ = true;
  controls_last_hover_ = std::chrono::steady_clock::now();
}

void PlayerOverlay::draw(RenderContext& render_context) {
  if (!has_player_file()) {
    return;
  }

  return_to_file_browser_after_eof();
  if (!has_player_file()) {
    return;
  }

  if (render_context.clay_context != nullptr) {
    Clay_SetCurrentContext(render_context.clay_context);
  }

  const BasicGlApi& gl = render_context.gl_api;
  gl.viewport(0, 0, render_context.metrics.pixel_width, render_context.metrics.pixel_height);
  player_->render(render_context.metrics.pixel_width, render_context.metrics.pixel_height);

  gl.viewport(0, 0, render_context.metrics.pixel_width, render_context.metrics.pixel_height);
  gl.disable(GL_DEPTH_TEST);
  gl.disable(GL_SCISSOR_TEST);

  const PlaybackSnapshot snapshot = player_->snapshot();
  update_bitrate_history(snapshot);
  begin_frame();
  build(render_context.metrics, snapshot, torrent_service_.snapshot(), app_.is_fullscreen(),
        controls_visible());
  update_controls_visibility(render_context.metrics);
}

bool PlayerOverlay::handle_event(const SDL_Event& event) {
  switch (event.type) {
  case SDL_EVENT_KEY_DOWN:
    return key_down(event.key);
  case SDL_EVENT_TEXT_INPUT:
    text_input(event.text);
    return cache_size_input_focused();
  case SDL_EVENT_MOUSE_MOTION:
    mouse_motion(event.motion);
    return true;
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP:
    mouse_button(event.button);
    return true;
  default:
    break;
  }
  return false;
}

void PlayerOverlay::close() {
  close_menus();
  dragging_volume_ = false;
  ui_.begin_frame();
  audio_track_row_ids_.clear();
}

void PlayerOverlay::initialize_ids() {
  ids_initialized_ = true;
  root_id_ = CLAY_ID("PlayerOverlayRoot");
  control_panel_id_ = CLAY_ID("PlayerControlPanel");
  timeline_id_ = CLAY_ID("PlayerTimeline");
  play_id_ = CLAY_ID("PlayerPlay");
  seek_back_id_ = CLAY_ID("PlayerSeekBack");
  seek_forward_id_ = CLAY_ID("PlayerSeekForward");
  volume_bar_id_ = CLAY_ID("PlayerVolumeBar");
  audio_button_id_ = CLAY_ID("PlayerAudioButton");
  audio_menu_id_ = CLAY_ID("PlayerAudioTrackMenu");
  buffer_button_id_ = CLAY_ID("PlayerBufferButton");
  buffer_menu_id_ = CLAY_ID("PlayerBufferMenu");
  buffer_input_id_ = CLAY_ID("PlayerBufferInput");
  buffer_apply_id_ = CLAY_ID("PlayerBufferApply");
  debug_button_id_ = CLAY_ID("PlayerDebugButton");
  fullscreen_id_ = CLAY_ID("PlayerFullscreen");
  stop_id_ = CLAY_ID("PlayerStop");
}

void PlayerOverlay::begin_frame() {
  ui_.begin_frame();
  audio_track_row_ids_.clear();
}

const char* PlayerOverlay::icon_path(Icon icon) const {
  switch (icon) {
  case Icon::play:
    return "icons/play.svg";
  case Icon::pause:
    return "icons/pause.svg";
  case Icon::rewind:
    return "icons/rewind.svg";
  case Icon::forward:
    return "icons/forward.svg";
  case Icon::volume:
    return "icons/volume.svg";
  case Icon::fullscreen:
    return "icons/fullscreen.svg";
  case Icon::window:
    return "icons/window.svg";
  case Icon::close:
    return "icons/close.svg";
  case Icon::audio:
    return "icons/audio.svg";
  }

  return "";
}

void PlayerOverlay::control_button(Clay_ElementId id, Icon icon_value, float glyph) {
  ui_.icon_button(id, icon_path(icon_value), false, control_size, glyph);
}

void PlayerOverlay::text_button(Clay_ElementId id, std::string_view label, bool active) {
  const bool hovered = Clay_PointerOver(id);
  CLAY(id, {.layout = {.sizing = {.width = CLAY_SIZING_FIT(0.0F),
                                  .height = CLAY_SIZING_FIXED(control_size)},
                       .padding = {10, 10, 0, 0},
                       .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
            .backgroundColor = active ? theme::lifted : (hovered ? theme::raised : theme::clear),
            .cornerRadius = CLAY_CORNER_RADIUS(theme::radius_control)}) {
    ui_.text(label, theme::size_body, active || hovered ? theme::lamp : theme::ash);
  }
}

void PlayerOverlay::audio_track_row(Clay_ElementId id, std::string_view label, bool active) {
  const bool hovered = Clay_PointerOver(id);
  CLAY(id, {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                  .height = CLAY_SIZING_FIXED(ui_.line(theme::size_body) + 14.0F)},
                       .padding = {12, 12, 0, 0},
                       .childGap = 10,
                       .childAlignment = {.x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER}},
            .backgroundColor = hovered ? theme::rule : theme::clear,
            .cornerRadius = CLAY_CORNER_RADIUS(theme::radius_control)}) {
    CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(6.0F),
                                        .height = CLAY_SIZING_FIXED(6.0F)}},
                  .backgroundColor = active ? theme::lamp : theme::clear,
                  .cornerRadius = CLAY_CORNER_RADIUS(3.0F)}) {}
    ui_.text(label, theme::size_body, active ? theme::lamp : theme::ash);
  }
}

// One rail carries both the playhead and the torrent's piece map underneath it, so what has
// arrived and where playback is can be read in a single glance.
void PlayerOverlay::timeline(const PlaybackSnapshot& snapshot, const TorrentSnapshot& torrent,
                             float width) {
  const float progress =
      snapshot.duration > 0.0
          ? static_cast<float>(std::clamp(snapshot.time_pos / snapshot.duration, 0.0, 1.0))
          : 0.0F;
  const bool hovered = Clay_PointerOver(timeline_id_);
  const float rail = hovered ? 6.0F : 4.0F;
  const bool pieces = !torrent.piece_states.empty();

  CLAY(timeline_id_,
       {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F), .height = CLAY_SIZING_FIXED(18.0F)},
                   .childGap = 2,
                   .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                   .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
    CLAY(CLAY_ID("PlayerRail"), {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                                       .height = CLAY_SIZING_FIXED(rail)}},
                                 .backgroundColor = {255.0F, 255.0F, 255.0F, 46.0F},
                                 .cornerRadius = CLAY_CORNER_RADIUS(rail * 0.5F)}) {
      CLAY(CLAY_ID("PlayerProgress"),
           {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(std::max(rail, width * progress)),
                                  .height = CLAY_SIZING_GROW(0.0F)}},
            .backgroundColor = theme::lamp,
            .cornerRadius = CLAY_CORNER_RADIUS(rail * 0.5F)}) {}
    }
    if (pieces) {
      piece_rail(torrent, width);
    }
    CLAY(CLAY_ID("PlayerPlayhead"),
         {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(12.0F),
                                .height = CLAY_SIZING_FIXED(12.0F)}},
          .backgroundColor = theme::lamp,
          .cornerRadius = CLAY_CORNER_RADIUS(6.0F),
          .floating = {
              .offset = {std::clamp(width * progress, 0.0F, width) - 6.0F, pieces ? -2.5F : 0.0F},
              .parentId = timeline_id_.id,
              .zIndex = 4,
              .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_CENTER,
                               .parent = CLAY_ATTACH_POINT_LEFT_CENTER},
              .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
              .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID,
              .clipTo = CLAY_CLIP_TO_NONE}}) {}
  }
}

void PlayerOverlay::piece_rail(const TorrentSnapshot& torrent, float width) {
  CLAY(CLAY_ID("PlayerPieceRail"), {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                                          .height = CLAY_SIZING_FIXED(3.0F)}}}) {
    const std::size_t piece_count = torrent.piece_states.size();
    const std::size_t max_segments =
        static_cast<std::size_t>(std::clamp(width / 4.0F, 32.0F, 320.0F));
    const std::size_t segments = std::min(piece_count, max_segments);
    for (std::size_t segment = 0; segment < segments; ++segment) {
      const std::size_t begin = (segment * piece_count) / segments;
      const std::size_t end = std::max(begin + 1, ((segment + 1) * piece_count) / segments);
      const TorrentPieceState state = dominant_piece_state(torrent.piece_states, begin, end);
      CLAY(Clay_GetElementIdWithIndex(CLAY_STRING("PlayerPieceRailSegment"),
                                      static_cast<uint32_t>(segment)),
           {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                  .height = CLAY_SIZING_GROW(0.0F)}},
            .backgroundColor = piece_color(state)}) {}
    }
  }
}

void PlayerOverlay::status_overlay(const PlaybackSnapshot& snapshot,
                                   const TorrentSnapshot& torrent) {
  std::string heading;
  std::string detail;
  Clay_Color accent = theme::lamp;

  if (torrent.state == TorrentLoadState::error || torrent.state == TorrentLoadState::unavailable) {
    heading = "The torrent stopped working";
    detail = !torrent.error.empty() ? torrent.error : torrent.status;
    accent = theme::fault;
  } else if (torrent.stalled) {
    heading = "No peers are sending this part";
    detail = torrent.buffer_status.empty() ? torrent.status : torrent.buffer_status;
    accent = theme::fault;
  } else if (torrent.buffering) {
    heading = "Buffering";
    detail = torrent.buffer_status.empty() ? torrent.status : torrent.buffer_status;
    // The service reports "Buffering - 6 peers - 16 KiB/s"; the heading already says it.
    if (detail.starts_with("Buffering - ")) {
      detail.erase(0, 12);
    }
    for (std::size_t at = detail.find(" - "); at != std::string::npos;
         at = detail.find(" - ", at)) {
      detail.replace(at, 3, ", ");
    }
    accent = theme::signal;
  } else if (snapshot.status.find(':') != std::string::npos) {
    heading = "This video cannot be played";
    detail = snapshot.status;
    accent = theme::fault;
  } else if (snapshot.status == "Loading" || !snapshot.loaded) {
    heading = "Opening";
    detail = snapshot.status == "Loading" ? std::string() : snapshot.status;
  } else {
    return;
  }

  CLAY(CLAY_ID("PlayerCenterStatusOverlay"),
       {.layout = {.sizing = {.width = CLAY_SIZING_FIT(0.0F, 480.0F),
                              .height = CLAY_SIZING_FIT(0.0F)},
                   .padding = {20, 20, 14, 16},
                   .childGap = 4,
                   .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER},
                   .layoutDirection = CLAY_TOP_TO_BOTTOM},
        .backgroundColor = theme::veil,
        .cornerRadius = CLAY_CORNER_RADIUS(theme::radius_menu),
        .floating = {.offset = {0.0F, -36.0F},
                     .parentId = root_id_.id,
                     .zIndex = 15,
                     .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER,
                                      .parent = CLAY_ATTACH_POINT_CENTER_CENTER},
                     .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
                     .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID,
                     .clipTo = CLAY_CLIP_TO_NONE}}) {
    ui_.text(ui_.keep(heading), theme::size_lead, accent, theme::font_semibold);
    if (!detail.empty()) {
      ui_.text(ui_.keep(detail), theme::size_caption, theme::ash, theme::font_regular,
               CLAY_TEXT_WRAP_WORDS);
    }
  }
}

void PlayerOverlay::stat_row(std::string_view key, std::string value, Clay_Color color) {
  CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F)}, .childGap = 8}}) {
    CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(stat_key_width)}}}) {
      ui_.text(key, theme::size_caption, theme::smoke);
    }
    CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F)}}}) {
      ui_.text(ui_.keep(std::move(value)), theme::size_caption, color, theme::font_regular,
               CLAY_TEXT_WRAP_WORDS);
    }
  }
}

void PlayerOverlay::debug_overlay(const PlaybackSnapshot& snapshot, const TorrentSnapshot& torrent,
                                  float max_width) {
  const PlaybackDiagnostics& diagnostics = snapshot.diagnostics;
  const VideoPlaybackMetadata& video = diagnostics.video;
  const AudioPlaybackMetadata& audio = diagnostics.audio;

  std::ostringstream video_line;
  video_line << metadata_value(video.codec);
  if (!video.codec_profile.empty()) {
    video_line << ' ' << video.codec_profile;
  }
  if (video.width > 0 && video.height > 0) {
    video_line << ", " << video.width << "\xC3\x97" << video.height;
  }
  if (video.fps > 0.0) {
    video_line << ", " << std::fixed << std::setprecision(2) << video.fps << " fps";
  }

  std::ostringstream pixel_line;
  pixel_line << metadata_value(video.pixel_format);
  if (!video.hardware_pixel_format.empty()) {
    pixel_line << " via " << video.hardware_pixel_format;
  }
  if (!video.color_matrix.empty()) {
    pixel_line << ", " << video.color_matrix;
  }

  std::ostringstream audio_line;
  audio_line << metadata_value(audio.codec);
  if (!audio.codec_profile.empty()) {
    audio_line << ' ' << audio.codec_profile;
  }
  if (audio.sample_rate > 0) {
    audio_line << ", " << std::fixed << std::setprecision(1)
               << static_cast<double>(audio.sample_rate) / 1000.0 << " kHz";
  }
  if (!audio.channels.empty()) {
    audio_line << ", " << audio.channels;
  } else if (audio.channel_count > 0) {
    audio_line << ", " << audio.channel_count << " ch";
  }
  if (!audio.sample_format.empty()) {
    audio_line << ", " << audio.sample_format;
  }

  std::ostringstream decoder_line;
  decoder_line << "video " << metadata_value(video.decoder) << ", audio "
               << metadata_value(audio.decoder) << ", hardware "
               << metadata_value(diagnostics.hardware_decoder);

  std::ostringstream timing_line;
  timing_line << std::showpos << std::fixed << std::setprecision(3) << diagnostics.av_sync << " s"
              << std::noshowpos << ", dropped " << diagnostics.dropped_frames << " / "
              << diagnostics.decoder_dropped_frames;

  const float panel_width = std::max(240.0F, max_width);

  CLAY(CLAY_ID("PlayerDebugOverlay"),
       {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(panel_width),
                              .height = CLAY_SIZING_FIT(0.0F)},
                   .padding = {16, 16, 14, 16},
                   .childGap = 5,
                   .layoutDirection = CLAY_TOP_TO_BOTTOM},
        .backgroundColor = theme::veil,
        .cornerRadius = CLAY_CORNER_RADIUS(theme::radius_menu),
        .floating = {.offset = {16.0F, 16.0F},
                     .parentId = root_id_.id,
                     .zIndex = 18,
                     .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_TOP,
                                      .parent = CLAY_ATTACH_POINT_LEFT_TOP},
                     .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
                     .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID,
                     .clipTo = CLAY_CLIP_TO_NONE}}) {
    CLAY(CLAY_ID("PlayerDebugHeader"), {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F)},
                                                   .padding = {0, 0, 0, 6},
                                                   .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
      ui_.text("Playback stats", theme::size_body, theme::lamp, theme::font_semibold);
      ui_.spacer();
      ui_.text(snapshot.paused ? "Paused" : "Playing", theme::size_caption, theme::smoke);
    }
    stat_row("Container", metadata_value(diagnostics.file_format), theme::lamp);
    stat_row("Video", video_line.str(), theme::lamp);
    stat_row("Pixels", pixel_line.str(), theme::ash);
    stat_row("Audio", audio_line.str(), theme::lamp);
    stat_row("Decoders", decoder_line.str(), theme::ash);
    stat_row("A/V sync", timing_line.str(), theme::ash);
    if (has_torrent_playback_state(torrent)) {
      std::ostringstream network_line;
      network_line << torrent.peers << " peers, "
                   << format_bitrate(static_cast<double>(torrent.download_rate) * 8.0)
                   << ", buffer " << format_size(torrent.cache_bytes_used) << " of "
                   << format_size(torrent.cache_bytes_limit);
      stat_row("Swarm", network_line.str(), torrent.stalled ? theme::fault : theme::ash);
    }

    CLAY(CLAY_ID("PlayerDebugBitrateLegend"),
         {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F)},
                     .padding = {0, 0, 8, 2},
                     .childGap = 6,
                     .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
      ui_.text("Bitrate", theme::size_caption, theme::smoke);
      ui_.gap(8.0F, 1.0F);
      const auto swatch = [](Clay_Color color) {
        CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(8.0F),
                                            .height = CLAY_SIZING_FIXED(8.0F)}},
                      .backgroundColor = color,
                      .cornerRadius = CLAY_CORNER_RADIUS(2.0F)}) {}
      };
      swatch(theme::piece_have);
      ui_.text(ui_.keep("video " + format_bitrate(video.bitrate_bps)), theme::size_caption,
               theme::ash);
      ui_.gap(8.0F, 1.0F);
      swatch(theme::signal);
      ui_.text(ui_.keep("audio " + format_bitrate(audio.bitrate_bps)), theme::size_caption,
               theme::ash);
    }
    bitrate_graph(panel_width - 32.0F);
  }
}

void PlayerOverlay::bitrate_graph(float width) {
  const std::size_t max_columns = static_cast<std::size_t>(std::clamp(width / 5.0F, 24.0F, 96.0F));
  const float column_width = std::max(1.0F, (width - static_cast<float>(max_columns - 1)) /
                                                static_cast<float>(max_columns));
  const std::size_t first =
      bitrate_history_.size() > max_columns ? bitrate_history_.size() - max_columns : 0;
  double peak_bps = 1.0;
  for (std::size_t index = first; index < bitrate_history_.size(); ++index) {
    peak_bps =
        std::max(peak_bps, bitrate_history_[index].video_bps + bitrate_history_[index].audio_bps);
  }

  CLAY(CLAY_ID("PlayerDebugBitrateGraph"),
       {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                              .height = CLAY_SIZING_FIXED(bitrate_graph_height)},
                   .childGap = 1,
                   .childAlignment = {.x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_BOTTOM}},
        .backgroundColor = {255.0F, 255.0F, 255.0F, 10.0F},
        .cornerRadius = CLAY_CORNER_RADIUS(2.0F)}) {
    for (std::size_t index = first; index < bitrate_history_.size(); ++index) {
      const BitrateSample& sample = bitrate_history_[index];
      const float video_height =
          sample.video_bps > 0.0
              ? std::max(1.0F,
                         bitrate_graph_height * static_cast<float>(sample.video_bps / peak_bps))
              : 0.0F;
      const float audio_height =
          sample.audio_bps > 0.0
              ? std::max(1.0F,
                         bitrate_graph_height * static_cast<float>(sample.audio_bps / peak_bps))
              : 0.0F;
      const uint32_t column = static_cast<uint32_t>(index - first);
      CLAY(Clay_GetElementIdWithIndex(CLAY_STRING("PlayerDebugBitrateColumn"), column),
           {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(column_width),
                                  .height = CLAY_SIZING_GROW(0.0F)},
                       .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_BOTTOM},
                       .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
        if (video_height > 0.0F) {
          CLAY(Clay_GetElementIdWithIndex(CLAY_STRING("PlayerDebugVideoBitrate"), column),
               {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                      .height = CLAY_SIZING_FIXED(video_height)}},
                .backgroundColor = theme::piece_have}) {}
        }
        if (audio_height > 0.0F) {
          CLAY(Clay_GetElementIdWithIndex(CLAY_STRING("PlayerDebugAudioBitrate"), column),
               {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                      .height = CLAY_SIZING_FIXED(audio_height)}},
                .backgroundColor = theme::signal}) {}
        }
      }
    }
  }
}

void PlayerOverlay::update_bitrate_history(const PlaybackSnapshot& snapshot) {
  if (snapshot.path != bitrate_history_path_) {
    bitrate_history_path_ = snapshot.path;
    bitrate_history_.clear();
    last_bitrate_sample_ = {};
  }
  if (snapshot.path.empty()) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  if (last_bitrate_sample_.time_since_epoch().count() != 0 &&
      now - last_bitrate_sample_ < bitrate_sample_interval) {
    return;
  }
  last_bitrate_sample_ = now;

  const auto sanitize = [](double value) {
    return std::isfinite(value) && value > 0.0 ? value : 0.0;
  };
  bitrate_history_.push_back({
      .video_bps = sanitize(snapshot.diagnostics.video.bitrate_bps),
      .audio_bps = sanitize(snapshot.diagnostics.audio.bitrate_bps),
  });
  while (bitrate_history_.size() > max_bitrate_samples) {
    bitrate_history_.pop_front();
  }
}

void PlayerOverlay::build(const WindowMetrics& metrics, const PlaybackSnapshot& snapshot,
                          const TorrentSnapshot& torrent, bool fullscreen, bool controls_visible) {
  const float width = static_cast<float>(metrics.logical_width);
  if (!controls_visible) {
    close_menus();
  }
  last_duration_ = snapshot.duration;
  last_rendered_cache_size_mib_ = std::max(1, torrent.cache_limit_mib);
  if (!buffer_input_focused_ && (buffer_input_text_.empty() || !buffer_menu_open_ ||
                                 selected_cache_size_mib_ != last_rendered_cache_size_mib_)) {
    selected_cache_size_mib_ = last_rendered_cache_size_mib_;
    buffer_input_text_ = std::to_string(selected_cache_size_mib_);
  }
  constexpr float side_padding = 24.0F;
  const float rail_width = std::max(120.0F, width - side_padding * 2.0F);
  const bool torrent_playback = has_torrent_playback_state(torrent);
  const std::string title = playback_title(snapshot, torrent);
  const std::string cache_usage =
      torrent.cache_bytes_limit > 0
          ? format_size(torrent.cache_bytes_used) + " of " + format_size(torrent.cache_bytes_limit)
          : std::to_string(torrent.cache_limit_mib) + " MiB";
  const std::string audio_label =
      snapshot.audio_tracks > 1
          ? "Audio " + std::to_string(std::max(0, snapshot.active_audio_index) + 1) + " of " +
                std::to_string(snapshot.audio_tracks)
          : "Audio";
  const int volume_percent = static_cast<int>(std::lround(std::clamp(snapshot.volume, 0.0, 100.0)));
  const float volume_progress = static_cast<float>(std::clamp(snapshot.volume / 100.0, 0.0, 1.0));
  if (snapshot.audio_track_list.empty()) {
    audio_menu_open_ = false;
  }
  const bool show_audio_menu = audio_menu_open_ && !snapshot.audio_track_list.empty();
  const bool show_buffer_menu = buffer_menu_open_ && controls_visible;
  const float debug_overlay_width = std::max(240.0F, std::min(460.0F, width - 32.0F));

  std::string stream_line;
  Clay_Color stream_color = theme::ash;
  if (torrent_playback) {
    stream_line = std::to_string(torrent.peers) + (torrent.peers == 1 ? " peer" : " peers") + ", " +
                  cache_usage + " buffered";
    stream_color =
        torrent.stalled ? theme::fault : (torrent.buffering ? theme::signal : theme::ash);
  }

  CLAY(root_id_,
       {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F), .height = CLAY_SIZING_GROW(0.0F)},
                   .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_BOTTOM}},
        .backgroundColor = theme::clear}) {
    status_overlay(snapshot, torrent);
    if (debug_overlay_enabled_) {
      debug_overlay(snapshot, torrent, debug_overlay_width);
    }
    if (controls_visible) {
      CLAY(control_panel_id_,
           {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F), .height = CLAY_SIZING_FIT(0.0F)},
                       .padding = {static_cast<uint16_t>(side_padding),
                                   static_cast<uint16_t>(side_padding), 14, 12},
                       .childGap = 6,
                       .layoutDirection = CLAY_TOP_TO_BOTTOM},
            .backgroundColor = theme::scrim}) {
        CLAY(CLAY_ID("PlayerInfoRow"), {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F)},
                                                   .childGap = 16,
                                                   .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
          ui_.text(ui_.keep(truncate_middle(
                       title, ui_.chars_for_width(rail_width * 0.6F, theme::size_lead))),
                   theme::size_lead, theme::lamp, theme::font_semibold);
          ui_.spacer();
          if (!stream_line.empty()) {
            ui_.text(ui_.keep(stream_line), theme::size_caption, stream_color);
          }
        }

        timeline(snapshot, torrent, rail_width);

        CLAY(CLAY_ID("PlayerControlsRow"),
             {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F)},
                         .childGap = 2,
                         .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
          control_button(play_id_,
                         snapshot.paused || snapshot.eof_reached ? Icon::play : Icon::pause, 22.0F);
          control_button(seek_back_id_, Icon::rewind);
          control_button(seek_forward_id_, Icon::forward);
          ui_.gap(10.0F, 1.0F);
          ui_.text(ui_.keep(format_clock(snapshot.time_pos)), theme::size_body, theme::lamp);
          ui_.text(ui_.keep("  /  " + format_clock(snapshot.duration)), theme::size_body,
                   theme::smoke);
          ui_.spacer();
          ui_.icon("icons/volume.svg", 18.0F);
          ui_.gap(8.0F, 1.0F);
          CLAY(volume_bar_id_, {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(volume_bar_width),
                                                      .height = CLAY_SIZING_FIXED(control_size)},
                                           .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
            CLAY(CLAY_ID("PlayerVolumeRail"),
                 {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                        .height = CLAY_SIZING_FIXED(4.0F)}},
                  .backgroundColor = {255.0F, 255.0F, 255.0F, 46.0F},
                  .cornerRadius = CLAY_CORNER_RADIUS(2.0F)}) {
              CLAY(CLAY_ID("PlayerVolumeProgress"),
                   {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(
                                              std::max(4.0F, volume_bar_width * volume_progress)),
                                          .height = CLAY_SIZING_GROW(0.0F)}},
                    .backgroundColor = theme::lamp,
                    .cornerRadius = CLAY_CORNER_RADIUS(2.0F)}) {}
            }
          }
          CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(40.0F)},
                                   .childAlignment = {.x = CLAY_ALIGN_X_RIGHT}}}) {
            ui_.text(ui_.keep(std::to_string(volume_percent)), theme::size_caption, theme::ash);
          }
          ui_.gap(14.0F, 1.0F);
          text_button(audio_button_id_, ui_.keep(audio_label), show_audio_menu);
          if (torrent_playback) {
            text_button(buffer_button_id_, "Buffer", show_buffer_menu);
          }
          text_button(debug_button_id_, "Stats", debug_overlay_enabled_);
          ui_.gap(6.0F, 1.0F);
          control_button(fullscreen_id_, fullscreen ? Icon::window : Icon::fullscreen, 18.0F);
          control_button(stop_id_, Icon::close, 18.0F);
        }

        if (show_audio_menu) {
          CLAY(audio_menu_id_,
               {.layout = {.sizing = {.width = CLAY_SIZING_FIT(220.0F, 420.0F),
                                      .height = CLAY_SIZING_FIT(0.0F)},
                           .padding = {4, 4, 4, 4},
                           .childGap = 2,
                           .layoutDirection = CLAY_TOP_TO_BOTTOM},
                .backgroundColor = theme::lifted,
                .cornerRadius = CLAY_CORNER_RADIUS(theme::radius_menu),
                .floating = {.offset = {0.0F, -8.0F},
                             .parentId = audio_button_id_.id,
                             .zIndex = 20,
                             .attachPoints = {.element = CLAY_ATTACH_POINT_RIGHT_BOTTOM,
                                              .parent = CLAY_ATTACH_POINT_RIGHT_TOP},
                             .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE,
                             .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID,
                             .clipTo = CLAY_CLIP_TO_NONE}}) {
            for (std::size_t index = 0; index < snapshot.audio_track_list.size(); ++index) {
              const Clay_ElementId row_id = Clay_GetElementIdWithIndex(
                  CLAY_STRING("PlayerAudioTrackRow"), static_cast<uint32_t>(index));
              audio_track_row_ids_.push_back(row_id);
              const bool active = static_cast<int>(index) == snapshot.active_audio_index;
              audio_track_row(row_id, ui_.keep(snapshot.audio_track_list[index].label), active);
            }
          }
        }

        if (show_buffer_menu) {
          CLAY(buffer_menu_id_,
               {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(300.0F),
                                      .height = CLAY_SIZING_FIT(0.0F)},
                           .padding = {16, 16, 14, 16},
                           .childGap = 6,
                           .layoutDirection = CLAY_TOP_TO_BOTTOM},
                .backgroundColor = theme::lifted,
                .cornerRadius = CLAY_CORNER_RADIUS(theme::radius_menu),
                .floating = {.offset = {0.0F, -8.0F},
                             .parentId = buffer_button_id_.id,
                             .zIndex = 20,
                             .attachPoints = {.element = CLAY_ATTACH_POINT_RIGHT_BOTTOM,
                                              .parent = CLAY_ATTACH_POINT_RIGHT_TOP},
                             .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE,
                             .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID,
                             .clipTo = CLAY_CLIP_TO_NONE}}) {
            ui_.text("Stream buffer", theme::size_body, theme::lamp, theme::font_semibold);
            ui_.text(ui_.keep("In memory: " + cache_usage), theme::size_caption, theme::ash);
            ui_.gap(1.0F, 6.0F);
            CLAY(CLAY_ID("PlayerCacheInputRow"),
                 {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F)},
                             .childGap = 8,
                             .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
              ui_.field(buffer_input_id_, ui_.keep(buffer_input_text_), {}, "MiB",
                        buffer_input_focused_, 0.0F);
              ui_.button(buffer_apply_id_, "Apply", ButtonKind::primary);
            }
          }
        }
      }
    }
  }
}

PlayerOverlayAction PlayerOverlay::hit_test_click() {
  if (Clay_PointerOver(timeline_id_)) {
    selected_seek_seconds_ = seek_seconds_from_pointer(Clay_GetPointerState().position.x);
    close_menus();
    return PlayerOverlayAction::seek_absolute;
  }
  if (Clay_PointerOver(play_id_)) {
    close_menus();
    return PlayerOverlayAction::toggle_pause;
  }
  if (Clay_PointerOver(seek_back_id_)) {
    close_menus();
    return PlayerOverlayAction::seek_back;
  }
  if (Clay_PointerOver(seek_forward_id_)) {
    close_menus();
    return PlayerOverlayAction::seek_forward;
  }
  if (Clay_PointerOver(volume_bar_id_)) {
    close_menus();
    return PlayerOverlayAction::set_volume;
  }
  if (Clay_PointerOver(audio_button_id_)) {
    audio_menu_open_ = !audio_menu_open_;
    buffer_menu_open_ = false;
    return PlayerOverlayAction::toggle_audio_menu;
  }
  if (Clay_PointerOver(buffer_button_id_)) {
    buffer_menu_open_ = !buffer_menu_open_;
    buffer_input_focused_ = buffer_menu_open_;
    if (buffer_menu_open_) {
      buffer_input_text_ = std::to_string(last_rendered_cache_size_mib_);
      selected_cache_size_mib_ = last_rendered_cache_size_mib_;
    }
    audio_menu_open_ = false;
    return PlayerOverlayAction::toggle_buffer_menu;
  }
  if (Clay_PointerOver(debug_button_id_)) {
    debug_overlay_enabled_ = !debug_overlay_enabled_;
    close_menus();
    return PlayerOverlayAction::toggle_debug_overlay;
  }
  if (Clay_PointerOver(buffer_input_id_)) {
    buffer_input_focused_ = true;
    return PlayerOverlayAction::focus_cache_size;
  }
  if (Clay_PointerOver(buffer_apply_id_)) {
    commit_cache_size_text();
    close_menus();
    return PlayerOverlayAction::set_cache_size;
  }
  for (std::size_t index = 0; index < audio_track_row_ids_.size(); ++index) {
    if (Clay_PointerOver(audio_track_row_ids_[index])) {
      selected_audio_track_index_ = index;
      close_menus();
      return PlayerOverlayAction::select_audio_track;
    }
  }
  if (Clay_PointerOver(fullscreen_id_)) {
    close_menus();
    return PlayerOverlayAction::fullscreen;
  }
  if (Clay_PointerOver(stop_id_)) {
    close_menus();
    return PlayerOverlayAction::stop;
  }

  close_menus();
  return PlayerOverlayAction::none;
}

bool PlayerOverlay::key_down(const SDL_KeyboardEvent& key) {
  if (key.repeat) {
    return true;
  }

  const bool command = (key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;

  if (cache_size_input_focused()) {
    if (command && key.key == SDLK_V) {
      char* clipboard = SDL_GetClipboardText();
      if (clipboard != nullptr) {
        append_cache_size_text(clipboard);
        SDL_free(clipboard);
      }
      return true;
    }
    if (key.key == SDLK_BACKSPACE) {
      backspace_cache_size_text();
      return true;
    }
    if (key.key == SDLK_RETURN || key.key == SDLK_KP_ENTER) {
      commit_cache_size_text();
      dispatch_action(PlayerOverlayAction::set_cache_size);
      return true;
    }
    if (key.key == SDLK_ESCAPE) {
      close_menus();
      return true;
    }
  }

  if (key.key == SDLK_ESCAPE) {
    if (app_.is_fullscreen()) {
      app_.toggle_fullscreen();
    } else {
      app_.request_quit();
    }
    return true;
  }

  if (!has_player_file()) {
    return false;
  }

  switch (key.key) {
  case SDLK_SPACE:
    dispatch_action(PlayerOverlayAction::toggle_pause);
    return true;
  case SDLK_LEFT:
    dispatch_action(PlayerOverlayAction::seek_back);
    return true;
  case SDLK_RIGHT:
    dispatch_action(PlayerOverlayAction::seek_forward);
    return true;
  case SDLK_HOME:
    player_->seek_absolute(0.0);
    return true;
  case SDLK_END:
    player_->seek_absolute(player_->snapshot().duration);
    return true;
  case SDLK_PAGEUP:
    player_->seek_relative(300.0);
    return true;
  case SDLK_PAGEDOWN:
    player_->seek_relative(-300.0);
    return true;
  case SDLK_UP:
    player_->adjust_volume(5.0);
    return true;
  case SDLK_DOWN:
    player_->adjust_volume(-5.0);
    return true;
  case SDLK_A:
    player_->select_audio_track_relative(1);
    return true;
  case SDLK_F:
  case SDLK_F11:
    dispatch_action(PlayerOverlayAction::fullscreen);
    return true;
  default:
    break;
  }
  return false;
}

void PlayerOverlay::text_input(const SDL_TextInputEvent& text_event) {
  if (text_event.text != nullptr && cache_size_input_focused()) {
    append_cache_size_text(text_event.text);
  }
}

void PlayerOverlay::mouse_motion(const SDL_MouseMotionEvent& motion) {
  pointer_.x = motion.x;
  pointer_.y = motion.y;
  pointer_.buttons = motion.state;
  if (dragging_volume_) {
    update_volume_from_pointer();
  }
}

void PlayerOverlay::mouse_button(const SDL_MouseButtonEvent& button) {
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
    if (clay_context_ != nullptr && has_player_file()) {
      Clay_SetCurrentContext(clay_context_);
      if (pointer_over_volume_bar()) {
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

  if (clay_context_ != nullptr) {
    Clay_SetCurrentContext(clay_context_);
  }

  if (has_player_file() && controls_visible()) {
    dispatch_action(hit_test_click());
  }
}

void PlayerOverlay::dispatch_action(PlayerOverlayAction action) {
  if (action == PlayerOverlayAction::none) {
    return;
  }

  if (player_ == nullptr) {
    return;
  }

  switch (action) {
  case PlayerOverlayAction::toggle_pause:
    player_->toggle_pause();
    break;
  case PlayerOverlayAction::seek_back:
    player_->seek_relative(-10.0);
    break;
  case PlayerOverlayAction::seek_forward:
    player_->seek_relative(30.0);
    break;
  case PlayerOverlayAction::seek_absolute:
    player_->seek_absolute(selected_seek_seconds());
    break;
  case PlayerOverlayAction::set_volume:
    update_volume_from_pointer();
    break;
  case PlayerOverlayAction::select_audio_track:
    player_->select_audio_track(selected_audio_track_index());
    break;
  case PlayerOverlayAction::set_cache_size:
    settings_.values().buffer_size_mib = selected_cache_size_mib();
    settings_.replace(settings_.values());
    settings_.save();
    torrent_service_.set_cache_limit_mib(settings_.values().buffer_size_mib);
    TORRVIEW_LOG_INFO("Cache limit set to " << settings_.values().buffer_size_mib << " MiB");
    break;
  case PlayerOverlayAction::fullscreen:
    app_.toggle_fullscreen();
    break;
  case PlayerOverlayAction::stop:
    app_.save_progress();
    player_->stop();
    app_.update_window_title();
    if (torrent_service_.snapshot().state != TorrentLoadState::idle) {
      app_.navigate<FileBrowserPage>();
    } else {
      app_.navigate<TitlePage>();
    }
    break;
  case PlayerOverlayAction::toggle_audio_menu:
  case PlayerOverlayAction::toggle_buffer_menu:
  case PlayerOverlayAction::toggle_debug_overlay:
  case PlayerOverlayAction::focus_cache_size:
  case PlayerOverlayAction::none:
    break;
  }
}

void PlayerOverlay::update_volume_from_pointer() {
  if (player_ == nullptr) {
    return;
  }

  player_->set_volume(volume_from_pointer(pointer_.x));
}

void PlayerOverlay::update_controls_visibility(const WindowMetrics& metrics) {
  const auto now = std::chrono::steady_clock::now();
  if (dragging_volume_) {
    controls_visible_ = true;
    controls_last_hover_ = now;
    return;
  }

  const bool pointer_in_reveal_band =
      pointer_.y >= static_cast<float>(metrics.logical_height) - controls_reveal_band;
  if (!controls_visible_ && pointer_in_reveal_band) {
    controls_visible_ = true;
    controls_last_hover_ = now;
    return;
  }

  if (!controls_visible_) {
    return;
  }

  if (pointer_in_reveal_band || pointer_over_controls()) {
    controls_last_hover_ = now;
    return;
  }

  if (now - controls_last_hover_ >= controls_hide_delay) {
    controls_visible_ = false;
    close_menus();
  }
}

void PlayerOverlay::return_to_file_browser_after_eof() {
  if (player_ == nullptr) {
    return;
  }

  const PlaybackSnapshot snapshot = player_->snapshot();
  if (!snapshot.eof_reached || !snapshot.path.starts_with("torrview://")) {
    return;
  }

  TORRVIEW_LOG_INFO("Torrent playback ended; returning to file browser");
  app_.save_progress();
  player_->stop();
  controls_visible_ = true;
  close_menus();
  app_.navigate<FileBrowserPage>();
  app_.update_window_title();
}

bool PlayerOverlay::controls_visible() const { return controls_visible_; }

bool PlayerOverlay::has_player_file() const { return player_ != nullptr && player_->has_file(); }

double PlayerOverlay::volume_from_pointer(float pointer_x) const {
  const Clay_ElementData data = Clay_GetElementData(volume_bar_id_);
  if (!data.found || data.boundingBox.width <= 0.0F) {
    return 100.0;
  }

  const float local_x = std::clamp(pointer_x - data.boundingBox.x, 0.0F, data.boundingBox.width);
  return static_cast<double>(local_x / data.boundingBox.width) * 100.0;
}

double PlayerOverlay::seek_seconds_from_pointer(float pointer_x) const {
  const Clay_ElementData data = Clay_GetElementData(timeline_id_);
  if (!data.found || data.boundingBox.width <= 0.0F || last_duration_ <= 0.0) {
    return 0.0;
  }

  const float local_x = std::clamp(pointer_x - data.boundingBox.x, 0.0F, data.boundingBox.width);
  return std::clamp(static_cast<double>(local_x / data.boundingBox.width) * last_duration_, 0.0,
                    last_duration_);
}

double PlayerOverlay::selected_seek_seconds() const { return selected_seek_seconds_; }

bool PlayerOverlay::pointer_over_volume_bar() const { return Clay_PointerOver(volume_bar_id_); }

bool PlayerOverlay::pointer_over_controls() const {
  return Clay_PointerOver(control_panel_id_) || Clay_PointerOver(audio_menu_id_) ||
         Clay_PointerOver(buffer_menu_id_);
}

std::size_t PlayerOverlay::selected_audio_track_index() const {
  return selected_audio_track_index_;
}

int PlayerOverlay::selected_cache_size_mib() const { return selected_cache_size_mib_; }

bool PlayerOverlay::cache_size_input_focused() const { return buffer_input_focused_; }

void PlayerOverlay::append_cache_size_text(std::string_view value) {
  if (!buffer_input_focused_) {
    return;
  }
  for (char character : value) {
    if (character < '0' || character > '9' || buffer_input_text_.size() >= 7) {
      continue;
    }
    if (buffer_input_text_ == "0") {
      buffer_input_text_.clear();
    }
    buffer_input_text_.push_back(character);
  }
}

void PlayerOverlay::backspace_cache_size_text() {
  if (buffer_input_focused_ && !buffer_input_text_.empty()) {
    buffer_input_text_.pop_back();
  }
}

void PlayerOverlay::commit_cache_size_text() {
  try {
    selected_cache_size_mib_ = std::clamp(std::stoi(buffer_input_text_), 1, 1024 * 1024);
  } catch (const std::exception&) {
    selected_cache_size_mib_ = std::max(1, last_rendered_cache_size_mib_);
  }
  buffer_input_text_ = std::to_string(selected_cache_size_mib_);
  buffer_input_focused_ = false;
}

void PlayerOverlay::close_audio_menu() { audio_menu_open_ = false; }

void PlayerOverlay::close_menus() {
  audio_menu_open_ = false;
  buffer_menu_open_ = false;
  buffer_input_focused_ = false;
}

} // namespace torrview::ui
