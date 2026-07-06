#include "ui/player_overlay.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>

namespace torrview::ui {
namespace {

constexpr Clay_Color color_text = {235.0F, 238.0F, 243.0F, 255.0F};
constexpr Clay_Color color_muted = {166.0F, 174.0F, 186.0F, 255.0F};
constexpr Clay_Color color_panel = {12.0F, 13.0F, 15.0F, 218.0F};
constexpr Clay_Color color_button = {34.0F, 38.0F, 44.0F, 232.0F};
constexpr Clay_Color color_button_emphasis = {76.0F, 176.0F, 154.0F, 242.0F};
constexpr Clay_Color color_menu = {24.0F, 27.0F, 31.0F, 242.0F};
constexpr Clay_Color color_rail = {67.0F, 73.0F, 83.0F, 255.0F};
constexpr Clay_Color color_progress = {76.0F, 176.0F, 154.0F, 255.0F};
constexpr Clay_Color color_requested = {223.0F, 179.0F, 83.0F, 255.0F};
constexpr Clay_Color color_evictable = {92.0F, 105.0F, 122.0F, 255.0F};
constexpr Clay_Color color_evicted = {42.0F, 47.0F, 55.0F, 255.0F};
constexpr Clay_Color color_failed = {214.0F, 94.0F, 82.0F, 255.0F};
constexpr Clay_Color color_overlay = {12.0F, 13.0F, 15.0F, 224.0F};
constexpr Clay_Color color_border = {198.0F, 203.0F, 214.0F, 255.0F};
constexpr float icon_button_size = 36.0F;
constexpr float volume_bar_width = 138.0F;

Clay_String clay_string(std::string_view value) {
  const std::size_t max_length = static_cast<std::size_t>(std::numeric_limits<int32_t>::max());
  const std::size_t length = std::min(value.size(), max_length);
  return {.isStaticallyAllocated = false,
          .length = static_cast<int32_t>(length),
          .chars = value.data()};
}

std::string format_time(double seconds) {
  if (seconds < 0.0 || !std::isfinite(seconds)) {
    seconds = 0.0;
  }

  const auto total = static_cast<int>(std::llround(seconds));
  const int hours = total / 3600;
  const int minutes = (total / 60) % 60;
  const int secs = total % 60;

  std::ostringstream out;
  if (hours > 0) {
    out << hours << ':';
    out << std::setw(2) << std::setfill('0') << minutes << ':';
  } else {
    out << minutes << ':';
  }
  out << std::setw(2) << std::setfill('0') << secs;
  return out.str();
}

const TorrentFileInfo* find_torrent_file(const TorrentSnapshot& torrent, int file_index) {
  for (const TorrentFileInfo& file : torrent.files) {
    if (file.index == file_index) {
      return &file;
    }
  }
  return nullptr;
}

std::string compact_title(std::string title) {
  constexpr std::size_t max_title_length = 46;
  if (title.size() <= max_title_length) {
    return title;
  }

  return title.substr(0, max_title_length - 3) + "...";
}

std::string filename_from_path(std::string_view value) {
  if (value.empty()) {
    return "Local playback";
  }

  std::filesystem::path path{std::string(value)};
  std::string filename = path.filename().string();
  if (filename.empty()) {
    filename = std::string(value);
  }

  return compact_title(std::move(filename));
}

std::string playback_title(const PlaybackSnapshot& snapshot, const TorrentSnapshot& torrent) {
  if (snapshot.path.starts_with("torrview://")) {
    if (const TorrentFileInfo* file = find_torrent_file(torrent, torrent.active_file_index);
        file != nullptr) {
      return filename_from_path(file->path);
    }

    if (!snapshot.media_title.empty()) {
      return compact_title(snapshot.media_title);
    }
  }

  return filename_from_path(snapshot.path);
}

std::string format_bytes(std::int64_t bytes) {
  constexpr double mib = 1024.0 * 1024.0;
  const double value = static_cast<double>(std::max<std::int64_t>(0, bytes));
  std::ostringstream out;
  out << std::fixed << std::setprecision(value >= 10.0 * mib ? 0 : 1) << value / mib << " MiB";
  return out.str();
}

Clay_Color piece_color(TorrentPieceState state) {
  switch (state) {
  case TorrentPieceState::missing:
    return color_rail;
  case TorrentPieceState::requested:
  case TorrentPieceState::partial:
    return color_requested;
  case TorrentPieceState::verified:
  case TorrentPieceState::retained:
    return color_progress;
  case TorrentPieceState::evictable:
    return color_evictable;
  case TorrentPieceState::evicted:
    return color_evicted;
  case TorrentPieceState::failed:
    return color_failed;
  }
  return color_rail;
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

void PlayerOverlay::initialize_ids() {
  root_id_ = Clay_GetElementId(CLAY_STRING("PlayerOverlayRoot"));
  control_panel_id_ = Clay_GetElementId(CLAY_STRING("PlayerControlPanel"));
  timeline_id_ = Clay_GetElementId(CLAY_STRING("PlayerTimeline"));
  play_id_ = Clay_GetElementId(CLAY_STRING("PlayerPlay"));
  seek_back_id_ = Clay_GetElementId(CLAY_STRING("PlayerSeekBack"));
  seek_forward_id_ = Clay_GetElementId(CLAY_STRING("PlayerSeekForward"));
  volume_bar_id_ = Clay_GetElementId(CLAY_STRING("PlayerVolumeBar"));
  audio_button_id_ = Clay_GetElementId(CLAY_STRING("PlayerAudioButton"));
  audio_menu_id_ = Clay_GetElementId(CLAY_STRING("PlayerAudioTrackMenu"));
  buffer_button_id_ = Clay_GetElementId(CLAY_STRING("PlayerBufferButton"));
  buffer_menu_id_ = Clay_GetElementId(CLAY_STRING("PlayerBufferMenu"));
  buffer_input_id_ = Clay_GetElementId(CLAY_STRING("PlayerBufferInput"));
  buffer_apply_id_ = Clay_GetElementId(CLAY_STRING("PlayerBufferApply"));
  fullscreen_id_ = Clay_GetElementId(CLAY_STRING("PlayerFullscreen"));
  stop_id_ = Clay_GetElementId(CLAY_STRING("PlayerStop"));
}

void PlayerOverlay::begin_frame() {
  frame_strings_.clear();
  audio_track_row_ids_.clear();
}

std::string_view PlayerOverlay::retain_frame_text(std::string value) {
  frame_strings_.push_back(std::move(value));
  return frame_strings_.back();
}

void PlayerOverlay::text(std::string_view value, uint16_t font_size, Clay_Color color,
                         Clay_TextElementConfigWrapMode wrap) {
  const uint16_t scaled_size = scaled_font_size(font_size);
  CLAY_TEXT(clay_string(value), CLAY_TEXT_CONFIG({.textColor = color,
                                                  .fontSize = scaled_size,
                                                  .lineHeight = static_cast<uint16_t>(std::lround(
                                                      static_cast<float>(scaled_size) * 1.25F)),
                                                  .wrapMode = wrap}));
}

uint16_t PlayerOverlay::scaled_font_size(uint16_t font_size) const {
  const float scale = static_cast<float>(std::clamp(font_size_, 9, 24)) / 13.0F;
  return static_cast<uint16_t>(std::clamp<int>(static_cast<int>(std::lround(font_size * scale)), 8,
                                               32));
}

float PlayerOverlay::line_height(uint16_t font_size) const {
  return static_cast<float>(scaled_font_size(font_size)) * 1.25F;
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

void PlayerOverlay::icon(Icon icon_value, float size) {
  CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(size),
                                      .height = CLAY_SIZING_FIXED(size)}},
                .image = {.imageData = const_cast<char*>(icon_path(icon_value))}}) {}
}

void PlayerOverlay::icon_button(Clay_ElementId id, Icon icon_value, bool emphasized) {
  const Clay_Color background = emphasized ? color_button_emphasis : color_button;
  const float button_size = std::max(icon_button_size, line_height(12) + 12.0F);
  CLAY(id,
       {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(button_size),
                              .height = CLAY_SIZING_FIXED(button_size)},
                   .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
        .backgroundColor = background,
        .cornerRadius = CLAY_CORNER_RADIUS(6.0F)}) {
    icon(icon_value);
  }
}

void PlayerOverlay::audio_track_row(Clay_ElementId id, std::string_view label, bool active) {
  const Clay_Color background = active ? color_button_emphasis : color_button;
  CLAY(id,
       {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                              .height = CLAY_SIZING_FIT(0.0F)},
                   .padding = {10, 10, 0, 0},
                   .childAlignment = {.x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER}},
        .backgroundColor = background,
        .cornerRadius = CLAY_CORNER_RADIUS(5.0F)}) {
    text(label, 12, active ? color_text : color_muted, CLAY_TEXT_WRAP_NONE);
  }
}

void PlayerOverlay::cache_size_input(std::string_view value, bool focused) {
  CLAY(buffer_input_id_,
       {.layout = {.sizing = {.width = CLAY_SIZING_GROW(160.0F),
                              .height = CLAY_SIZING_FIT(0.0F)},
                   .padding = {12, 12, 0, 0},
                   .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
        .backgroundColor = {14.0F, 15.0F, 17.0F, 255.0F},
        .cornerRadius = CLAY_CORNER_RADIUS(8.0F),
        .border = {.color = focused ? color_button_emphasis : color_border,
                   .width = {.left = 1, .right = 1, .top = 1, .bottom = 1}}}) {
    text(value.empty() ? std::string_view(" ") : value, 20, color_text, CLAY_TEXT_WRAP_NONE);
  }
}

void PlayerOverlay::piece_rail(const TorrentSnapshot& torrent, float width) {
  CLAY(CLAY_ID("PlayerPieceRail"),
       {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                              .height = CLAY_SIZING_FIXED(9.0F)},
                   .childGap = 1},
        .backgroundColor = color_rail,
        .cornerRadius = CLAY_CORNER_RADIUS(4.0F),
        .clip = {.horizontal = true, .vertical = true}}) {
    if (torrent.piece_states.empty()) {
      CLAY(CLAY_ID("PlayerPieceRailEmpty"),
           {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                  .height = CLAY_SIZING_GROW(0.0F)}},
            .backgroundColor = color_evicted}) {}
    } else {
      const std::size_t piece_count = torrent.piece_states.size();
      const std::size_t max_segments =
          static_cast<std::size_t>(std::clamp(width / 8.0F, 32.0F, 120.0F));
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
}

void PlayerOverlay::status_overlay(const PlaybackSnapshot& snapshot,
                                   const TorrentSnapshot& torrent) {
  std::string heading;
  std::string detail;
  Clay_Color accent = color_progress;

  if (torrent.state == TorrentLoadState::error || torrent.state == TorrentLoadState::unavailable) {
    heading = "Torrent error";
    detail = !torrent.error.empty() ? torrent.error : torrent.status;
    accent = color_failed;
  } else if (torrent.stalled) {
    heading = "Stalled";
    detail = torrent.buffer_status.empty() ? torrent.status : torrent.buffer_status;
    accent = color_failed;
  } else if (torrent.buffering) {
    heading = "Buffering";
    detail = torrent.buffer_status.empty() ? torrent.status : torrent.buffer_status;
    accent = color_requested;
  } else if (snapshot.status.find(':') != std::string::npos) {
    heading = "Playback error";
    detail = snapshot.status;
    accent = color_failed;
  } else if (snapshot.status == "Loading" || !snapshot.loaded) {
    heading = "Loading";
    detail = snapshot.status.empty() ? "Opening media" : snapshot.status;
  } else {
    return;
  }

  if (detail.empty()) {
    detail = "Preparing playback";
  }

  CLAY(CLAY_ID("PlayerCenterStatusOverlay"),
       {.layout = {.sizing = {.width = CLAY_SIZING_FIT(0.0F, 520.0F),
                              .height = CLAY_SIZING_FIT(0.0F)},
                   .padding = {18, 18, 14, 14},
                   .childGap = 8,
                   .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER},
                   .layoutDirection = CLAY_TOP_TO_BOTTOM},
        .backgroundColor = color_overlay,
        .cornerRadius = CLAY_CORNER_RADIUS(8.0F),
        .floating = {.offset = {0.0F, -36.0F},
                     .parentId = root_id_.id,
                     .zIndex = 15,
                     .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER,
                                      .parent = CLAY_ATTACH_POINT_CENTER_CENTER},
                     .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
                     .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID,
                     .clipTo = CLAY_CLIP_TO_NONE}}) {
    text(retain_frame_text(heading), 16, accent, CLAY_TEXT_WRAP_NONE);
    text(retain_frame_text(detail), 12, color_muted, CLAY_TEXT_WRAP_WORDS);
  }
}

void PlayerOverlay::build(const WindowMetrics& metrics, const PlaybackSnapshot& snapshot,
                          const TorrentSnapshot& torrent, bool fullscreen,
                          bool controls_visible) {
  const float width = static_cast<float>(metrics.logical_width);
  if (!controls_visible) {
    close_menus();
  }
  last_duration_ = snapshot.duration;
  last_rendered_cache_size_mib_ = std::max(1, torrent.cache_limit_mib);
  if (!buffer_input_focused_ &&
      (buffer_input_text_.empty() || !buffer_menu_open_ ||
       selected_cache_size_mib_ != last_rendered_cache_size_mib_)) {
    selected_cache_size_mib_ = last_rendered_cache_size_mib_;
    buffer_input_text_ = std::to_string(selected_cache_size_mib_);
  }
  const float panel_width = std::clamp(width - 48.0F, 360.0F, 1240.0F);
  const float progress = snapshot.duration > 0.0
                             ? static_cast<float>(
                                   std::clamp(snapshot.time_pos / snapshot.duration, 0.0, 1.0))
                             : 0.0F;
  const bool torrent_playback = has_torrent_playback_state(torrent);
  const std::string title = playback_title(snapshot, torrent);
  const std::string time_label =
      format_time(snapshot.time_pos) + " / " + format_time(snapshot.duration);
  const std::string tracks = "V " + std::to_string(snapshot.video_tracks) + "  A " +
                             std::to_string(snapshot.audio_tracks) + "  S " +
                             std::to_string(snapshot.subtitle_tracks);
  const std::string cache_usage =
      torrent.cache_bytes_limit > 0
          ? format_bytes(torrent.cache_bytes_used) + " / " + format_bytes(torrent.cache_bytes_limit)
          : std::to_string(torrent.cache_limit_mib) + " MiB";
  const std::string buffer_label = "Buffer " + std::to_string(torrent.cache_limit_mib) + " MiB";
  const std::string buffer_status =
      torrent_playback && !torrent.buffer_status.empty() ? torrent.buffer_status : cache_usage;
  const std::string audio_label =
      snapshot.audio_tracks > 0
          ? "Audio " + std::to_string(std::max(0, snapshot.active_audio_index) + 1) + "/" +
                std::to_string(snapshot.audio_tracks)
          : "Audio -";
  const std::string status = snapshot.status.empty() ? "Ready" : snapshot.status;
  const std::string volume =
      std::to_string(static_cast<int>(std::lround(std::clamp(snapshot.volume, 0.0, 100.0)))) + "%";
  const float volume_progress =
      static_cast<float>(std::clamp(snapshot.volume / 100.0, 0.0, 1.0));
  if (snapshot.audio_track_list.empty()) {
    audio_menu_open_ = false;
  }
  const bool show_audio_menu = audio_menu_open_ && !snapshot.audio_track_list.empty();
  const bool show_buffer_menu = buffer_menu_open_ && controls_visible;

  CLAY(root_id_,
       {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F), .height = CLAY_SIZING_GROW(0.0F)},
                   .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_BOTTOM}},
        .backgroundColor = {0.0F, 0.0F, 0.0F, 0.0F}}) {
    status_overlay(snapshot, torrent);
    if (controls_visible) {
      CLAY(control_panel_id_,
           {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(panel_width),
                                  .height = CLAY_SIZING_FIT(0.0F)},
                       .padding = {18, 18, 14, 14},
                       .childGap = 10,
                       .layoutDirection = CLAY_TOP_TO_BOTTOM},
            .backgroundColor = color_panel,
            .cornerRadius = CLAY_CORNER_RADIUS(8.0F)}) {
      CLAY(CLAY_ID("PlayerInfoRow"),
           {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                  .height = CLAY_SIZING_FIT(0.0F)},
                       .childGap = 16,
                       .childAlignment = {.x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER}}}) {
        text(retain_frame_text(title), 14, color_text, CLAY_TEXT_WRAP_NONE);
        text(retain_frame_text(status), 12, color_muted, CLAY_TEXT_WRAP_NONE);
        text(retain_frame_text(tracks), 12, color_muted, CLAY_TEXT_WRAP_NONE);
      }

      CLAY(timeline_id_,
           {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                  .height = CLAY_SIZING_FIXED(10.0F)}},
            .backgroundColor = color_rail,
            .cornerRadius = CLAY_CORNER_RADIUS(5.0F)}) {
        CLAY(CLAY_ID("PlayerProgress"),
             {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(
                                        std::max(4.0F, (panel_width - 36.0F) *
                                                           static_cast<float>(progress))),
                                    .height = CLAY_SIZING_GROW(0.0F)}},
              .backgroundColor = color_progress,
              .cornerRadius = CLAY_CORNER_RADIUS(5.0F)}) {}
      }

      piece_rail(torrent, panel_width - 36.0F);

      CLAY(CLAY_ID("PlayerBufferStatusRow"),
           {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                  .height = CLAY_SIZING_FIT(0.0F)},
                       .childGap = 14,
                       .childAlignment = {.x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER}}}) {
        text(retain_frame_text(buffer_status), 11,
             torrent.stalled ? color_failed : (torrent.buffering ? color_requested : color_muted),
             CLAY_TEXT_WRAP_NONE);
        text(retain_frame_text(cache_usage), 11, color_muted, CLAY_TEXT_WRAP_NONE);
      }

      CLAY(CLAY_ID("PlayerControlsRow"),
           {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                  .height = CLAY_SIZING_FIT(0.0F)},
                       .childGap = 8,
                       .childAlignment = {.x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER}}}) {
        icon_button(play_id_, snapshot.paused || snapshot.eof_reached ? Icon::play : Icon::pause,
                    true);
        icon_button(seek_back_id_, Icon::rewind);
        icon_button(seek_forward_id_, Icon::forward);
        text(retain_frame_text(time_label), 12, color_muted, CLAY_TEXT_WRAP_NONE);
        CLAY(CLAY_ID("PlayerSpacer"),
             {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                    .height = CLAY_SIZING_FIXED(1.0F)}}}) {}
        icon(Icon::volume, 18.0F);
        CLAY(volume_bar_id_,
             {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(volume_bar_width),
                                    .height = CLAY_SIZING_FIXED(10.0F)}},
              .backgroundColor = color_rail,
              .cornerRadius = CLAY_CORNER_RADIUS(5.0F)}) {
          CLAY(CLAY_ID("PlayerVolumeProgress"),
               {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(
                                          std::max(4.0F, volume_bar_width * volume_progress)),
                                      .height = CLAY_SIZING_GROW(0.0F)}},
                .backgroundColor = color_progress,
                .cornerRadius = CLAY_CORNER_RADIUS(5.0F)}) {}
        }
        text(retain_frame_text(volume), 12, color_muted, CLAY_TEXT_WRAP_NONE);
        CLAY(audio_button_id_,
             {.layout = {.sizing = {.width = CLAY_SIZING_FIT(0.0F),
                                    .height = CLAY_SIZING_FIT(0.0F)},
                         .padding = {10, 10, 0, 0},
                         .childGap = 8,
                         .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = show_audio_menu ? color_button_emphasis : color_button,
              .cornerRadius = CLAY_CORNER_RADIUS(6.0F)}) {
          icon(Icon::audio, 18.0F);
          text(retain_frame_text(audio_label), 12, color_text, CLAY_TEXT_WRAP_NONE);
        }
        CLAY(buffer_button_id_,
             {.layout = {.sizing = {.width = CLAY_SIZING_FIT(0.0F),
                                    .height = CLAY_SIZING_FIT(0.0F)},
                         .padding = {10, 10, 0, 0},
                         .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = show_buffer_menu ? color_button_emphasis : color_button,
              .cornerRadius = CLAY_CORNER_RADIUS(6.0F)}) {
          text(retain_frame_text(buffer_label), 12, color_text, CLAY_TEXT_WRAP_NONE);
        }
        icon_button(fullscreen_id_, fullscreen ? Icon::window : Icon::fullscreen);
        icon_button(stop_id_, Icon::close);
      }

      if (show_audio_menu) {
        CLAY(audio_menu_id_,
             {.layout = {.sizing = {.width = CLAY_SIZING_FIT(0.0F, 420.0F),
                                    .height = CLAY_SIZING_FIT(0.0F)},
                         .padding = {6, 6, 6, 6},
                         .childGap = 4,
                         .layoutDirection = CLAY_TOP_TO_BOTTOM},
              .backgroundColor = color_menu,
              .cornerRadius = CLAY_CORNER_RADIUS(7.0F),
              .floating = {.offset = {0.0F, -8.0F},
                           .parentId = audio_button_id_.id,
                           .zIndex = 20,
                           .attachPoints = {.element = CLAY_ATTACH_POINT_RIGHT_BOTTOM,
                                            .parent = CLAY_ATTACH_POINT_RIGHT_TOP},
                           .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE,
                           .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID,
                           .clipTo = CLAY_CLIP_TO_NONE}}) {
          for (std::size_t index = 0; index < snapshot.audio_track_list.size(); ++index) {
            const Clay_ElementId row_id =
                Clay_GetElementIdWithIndex(CLAY_STRING("PlayerAudioTrackRow"),
                                           static_cast<uint32_t>(index));
            audio_track_row_ids_.push_back(row_id);
            const bool active = static_cast<int>(index) == snapshot.active_audio_index;
            audio_track_row(row_id, retain_frame_text(snapshot.audio_track_list[index].label),
                            active);
          }
        }
      }

      if (show_buffer_menu) {
        CLAY(buffer_menu_id_,
             {.layout = {.sizing = {.width = CLAY_SIZING_FIT(0.0F, 520.0F),
                                    .height = CLAY_SIZING_FIT(0.0F)},
                         .padding = {12, 12, 12, 12},
                         .childGap = 10,
                         .layoutDirection = CLAY_TOP_TO_BOTTOM},
              .backgroundColor = color_menu,
              .cornerRadius = CLAY_CORNER_RADIUS(7.0F),
              .floating = {.offset = {0.0F, -8.0F},
                           .parentId = buffer_button_id_.id,
                           .zIndex = 20,
                           .attachPoints = {.element = CLAY_ATTACH_POINT_RIGHT_BOTTOM,
                                            .parent = CLAY_ATTACH_POINT_RIGHT_TOP},
                           .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE,
                           .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID,
                           .clipTo = CLAY_CLIP_TO_NONE}}) {
          text(retain_frame_text(cache_usage), 12, color_text, CLAY_TEXT_WRAP_NONE);
          text(retain_frame_text(buffer_status), 11, color_muted, CLAY_TEXT_WRAP_NONE);
          CLAY(CLAY_ID("PlayerCacheInputRow"),
               {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                      .height = CLAY_SIZING_FIT(0.0F)},
                           .childGap = 14,
                           .childAlignment = {.x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER}}}) {
            cache_size_input(retain_frame_text(buffer_input_text_), buffer_input_focused_);
            text("mib", 16, color_muted, CLAY_TEXT_WRAP_NONE);
            CLAY(buffer_apply_id_,
                 {.layout = {.sizing = {.width = CLAY_SIZING_FIT(0.0F),
                                        .height = CLAY_SIZING_FIT(0.0F)},
                             .padding = {12, 12, 5, 5},
                             .childAlignment = {.x = CLAY_ALIGN_X_CENTER,
                                                .y = CLAY_ALIGN_Y_CENTER}},
                  .backgroundColor = color_button_emphasis,
                  .cornerRadius = CLAY_CORNER_RADIUS(6.0F)}) {
              text("Set", 12, color_text, CLAY_TEXT_WRAP_NONE);
            }
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

double PlayerOverlay::volume_from_pointer(float pointer_x) const {
  const Clay_ElementData data = Clay_GetElementData(volume_bar_id_);
  if (!data.found || data.boundingBox.width <= 0.0F) {
    return 100.0;
  }

  const float local_x =
      std::clamp(pointer_x - data.boundingBox.x, 0.0F, data.boundingBox.width);
  return static_cast<double>(local_x / data.boundingBox.width) * 100.0;
}

double PlayerOverlay::seek_seconds_from_pointer(float pointer_x) const {
  const Clay_ElementData data = Clay_GetElementData(timeline_id_);
  if (!data.found || data.boundingBox.width <= 0.0F || last_duration_ <= 0.0) {
    return 0.0;
  }

  const float local_x =
      std::clamp(pointer_x - data.boundingBox.x, 0.0F, data.boundingBox.width);
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

void PlayerOverlay::set_font_size(int font_size) { font_size_ = std::clamp(font_size, 9, 24); }

void PlayerOverlay::close_audio_menu() { audio_menu_open_ = false; }

void PlayerOverlay::close_menus() {
  audio_menu_open_ = false;
  buffer_menu_open_ = false;
  buffer_input_focused_ = false;
}

} // namespace torrview::ui
