#include "ui/player_overlay.hpp"

#include "config.hpp"

#include <algorithm>
#include <cmath>
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

std::string filename_title(const PlaybackSnapshot& snapshot) {
  if (snapshot.path.empty()) {
    return "Local playback";
  }

  std::filesystem::path path(snapshot.path);
  std::string filename = path.filename().string();
  if (filename.empty()) {
    filename = snapshot.path;
  }

  constexpr std::size_t max_title_length = 46;
  if (filename.size() <= max_title_length) {
    return filename;
  }

  return filename.substr(0, max_title_length - 3) + "...";
}

} // namespace

void PlayerOverlay::initialize_ids() {
  control_panel_id_ = Clay_GetElementId(CLAY_STRING("PlayerControlPanel"));
  play_id_ = Clay_GetElementId(CLAY_STRING("PlayerPlay"));
  seek_back_id_ = Clay_GetElementId(CLAY_STRING("PlayerSeekBack"));
  seek_forward_id_ = Clay_GetElementId(CLAY_STRING("PlayerSeekForward"));
  volume_bar_id_ = Clay_GetElementId(CLAY_STRING("PlayerVolumeBar"));
  audio_button_id_ = Clay_GetElementId(CLAY_STRING("PlayerAudioButton"));
  audio_menu_id_ = Clay_GetElementId(CLAY_STRING("PlayerAudioTrackMenu"));
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
  CLAY_TEXT(clay_string(value), CLAY_TEXT_CONFIG({.textColor = color,
                                                  .fontSize = font_size,
                                                  .lineHeight = static_cast<uint16_t>(std::lround(
                                                      static_cast<float>(font_size) * 1.25F)),
                                                  .wrapMode = wrap}));
}

const char* PlayerOverlay::icon_path(Icon icon) const {
  switch (icon) {
  case Icon::play: {
    static const std::string path = std::string(TORRVIEW_ICON_DIR) + "/play.svg";
    return path.c_str();
  }
  case Icon::pause: {
    static const std::string path = std::string(TORRVIEW_ICON_DIR) + "/pause.svg";
    return path.c_str();
  }
  case Icon::rewind: {
    static const std::string path = std::string(TORRVIEW_ICON_DIR) + "/rewind.svg";
    return path.c_str();
  }
  case Icon::forward: {
    static const std::string path = std::string(TORRVIEW_ICON_DIR) + "/forward.svg";
    return path.c_str();
  }
  case Icon::volume: {
    static const std::string path = std::string(TORRVIEW_ICON_DIR) + "/volume.svg";
    return path.c_str();
  }
  case Icon::fullscreen: {
    static const std::string path = std::string(TORRVIEW_ICON_DIR) + "/fullscreen.svg";
    return path.c_str();
  }
  case Icon::window: {
    static const std::string path = std::string(TORRVIEW_ICON_DIR) + "/window.svg";
    return path.c_str();
  }
  case Icon::close: {
    static const std::string path = std::string(TORRVIEW_ICON_DIR) + "/close.svg";
    return path.c_str();
  }
  case Icon::audio: {
    static const std::string path = std::string(TORRVIEW_ICON_DIR) + "/audio.svg";
    return path.c_str();
  }
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
  CLAY(id,
       {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(icon_button_size),
                              .height = CLAY_SIZING_FIXED(icon_button_size)},
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
                              .height = CLAY_SIZING_FIXED(32.0F)},
                   .padding = {10, 10, 0, 0},
                   .childAlignment = {.x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER}},
        .backgroundColor = background,
        .cornerRadius = CLAY_CORNER_RADIUS(5.0F)}) {
    text(label, 12, active ? color_text : color_muted, CLAY_TEXT_WRAP_NONE);
  }
}

void PlayerOverlay::build(const WindowMetrics& metrics, const PlaybackSnapshot& snapshot,
                          bool fullscreen, bool controls_visible) {
  const float width = static_cast<float>(metrics.logical_width);
  const float height = static_cast<float>(metrics.logical_height);
  if (!controls_visible) {
    audio_menu_open_ = false;
  }
  const float panel_width = std::max(360.0F, width - 48.0F);
  const float progress = snapshot.duration > 0.0
                             ? static_cast<float>(
                                   std::clamp(snapshot.time_pos / snapshot.duration, 0.0, 1.0))
                             : 0.0F;
  const std::string title = filename_title(snapshot);
  const std::string time_label =
      format_time(snapshot.time_pos) + " / " + format_time(snapshot.duration);
  const std::string tracks = "V " + std::to_string(snapshot.video_tracks) + "  A " +
                             std::to_string(snapshot.audio_tracks) + "  S " +
                             std::to_string(snapshot.subtitle_tracks);
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
  const int visible_audio_rows =
      show_audio_menu ? static_cast<int>(snapshot.audio_track_list.size()) : 0;
  constexpr float panel_height = 150.0F;

  CLAY(CLAY_ID("PlayerOverlayRoot"),
       {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F), .height = CLAY_SIZING_GROW(0.0F)},
                   .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_BOTTOM}},
        .backgroundColor = {0.0F, 0.0F, 0.0F, 0.0F}}) {
    if (controls_visible) {
      CLAY(control_panel_id_,
           {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(panel_width),
                                  .height = CLAY_SIZING_FIXED(panel_height)},
                       .padding = {18, 18, 14, 14},
                       .childGap = 10,
                       .layoutDirection = CLAY_TOP_TO_BOTTOM},
            .backgroundColor = color_panel,
            .cornerRadius = CLAY_CORNER_RADIUS(8.0F)}) {
      CLAY(CLAY_ID("PlayerInfoRow"),
           {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                  .height = CLAY_SIZING_FIXED(24.0F)},
                       .childGap = 16,
                       .childAlignment = {.x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER}}}) {
        text(retain_frame_text(title), 14, color_text, CLAY_TEXT_WRAP_NONE);
        text(retain_frame_text(status), 12, color_muted, CLAY_TEXT_WRAP_NONE);
        text(retain_frame_text(tracks), 12, color_muted, CLAY_TEXT_WRAP_NONE);
      }

      CLAY(CLAY_ID("PlayerTimeline"),
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

      CLAY(CLAY_ID("PlayerControlsRow"),
           {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0.0F),
                                  .height = CLAY_SIZING_FIXED(44.0F)},
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
             {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(118.0F),
                                    .height = CLAY_SIZING_FIXED(icon_button_size)},
                         .padding = {10, 10, 0, 0},
                         .childGap = 8,
                         .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = show_audio_menu ? color_button_emphasis : color_button,
              .cornerRadius = CLAY_CORNER_RADIUS(6.0F)}) {
          icon(Icon::audio, 18.0F);
          text(retain_frame_text(audio_label), 12, color_text, CLAY_TEXT_WRAP_NONE);
        }
        icon_button(fullscreen_id_, fullscreen ? Icon::window : Icon::fullscreen);
        icon_button(stop_id_, Icon::close);
      }

      if (show_audio_menu) {
        CLAY(audio_menu_id_,
             {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(320.0F),
                                    .height = CLAY_SIZING_FIXED(
                                        static_cast<float>(visible_audio_rows) * 36.0F + 8.0F)},
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
          for (std::size_t index = 0; index < static_cast<std::size_t>(visible_audio_rows);
               ++index) {
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
    }
    }
  }
  (void)height;
}

PlayerOverlayAction PlayerOverlay::hit_test_click() {
  if (Clay_PointerOver(play_id_)) {
    audio_menu_open_ = false;
    return PlayerOverlayAction::toggle_pause;
  }
  if (Clay_PointerOver(seek_back_id_)) {
    audio_menu_open_ = false;
    return PlayerOverlayAction::seek_back;
  }
  if (Clay_PointerOver(seek_forward_id_)) {
    audio_menu_open_ = false;
    return PlayerOverlayAction::seek_forward;
  }
  if (Clay_PointerOver(volume_bar_id_)) {
    audio_menu_open_ = false;
    return PlayerOverlayAction::set_volume;
  }
  if (Clay_PointerOver(audio_button_id_)) {
    audio_menu_open_ = !audio_menu_open_;
    return PlayerOverlayAction::toggle_audio_menu;
  }
  for (std::size_t index = 0; index < audio_track_row_ids_.size(); ++index) {
    if (Clay_PointerOver(audio_track_row_ids_[index])) {
      selected_audio_track_index_ = index;
      audio_menu_open_ = false;
      return PlayerOverlayAction::select_audio_track;
    }
  }
  if (Clay_PointerOver(fullscreen_id_)) {
    audio_menu_open_ = false;
    return PlayerOverlayAction::fullscreen;
  }
  if (Clay_PointerOver(stop_id_)) {
    audio_menu_open_ = false;
    return PlayerOverlayAction::stop;
  }

  audio_menu_open_ = false;
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

bool PlayerOverlay::pointer_over_volume_bar() const { return Clay_PointerOver(volume_bar_id_); }

bool PlayerOverlay::pointer_over_controls() const {
  return Clay_PointerOver(control_panel_id_) || Clay_PointerOver(audio_menu_id_);
}

std::size_t PlayerOverlay::selected_audio_track_index() const {
  return selected_audio_track_index_;
}

void PlayerOverlay::close_audio_menu() { audio_menu_open_ = false; }

} // namespace torrview::ui
