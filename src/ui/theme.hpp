#pragma once

#include "clay.h"

#include <cstdint>

// Torrview's visual language. The interface is achromatic, warm, and lit like a projection
// booth: color belongs to the picture and to torrent state, never to decoration.
namespace torrview::ui::theme {

// Surfaces
constexpr Clay_Color graphite = {27.0F, 26.0F, 24.0F, 255.0F}; // #1B1A18 page
constexpr Clay_Color raised = {37.0F, 35.0F, 32.0F, 255.0F};   // #252320 hover, fields
constexpr Clay_Color lifted = {48.0F, 46.0F, 42.0F, 255.0F};   // #302E2A pressed, menus
constexpr Clay_Color rule = {54.0F, 51.0F, 46.0F, 255.0F};     // #36332E hairlines
constexpr Clay_Color scrim = {0.0F, 0.0F, 0.0F, 196.0F};       // over video
constexpr Clay_Color veil = {12.0F, 11.0F, 10.0F, 214.0F};     // behind dialogs

// Ink
constexpr Clay_Color lamp = {240.0F, 235.0F, 225.0F, 255.0F}; // #F0EBE1 primary text
constexpr Clay_Color ash = {156.0F, 149.0F, 138.0F, 255.0F};  // #9C958A secondary text
constexpr Clay_Color smoke = {106.0F, 100.0F, 91.0F, 255.0F}; // #6A645B tertiary text

// State (torrent and playback only)
constexpr Clay_Color signal = {227.0F, 165.0F, 66.0F, 255.0F}; // #E3A542 buffering, requested
constexpr Clay_Color fault = {214.0F, 90.0F, 69.0F, 255.0F};   // #D65A45 errors
constexpr Clay_Color clear = {0.0F, 0.0F, 0.0F, 0.0F};

// Piece map
constexpr Clay_Color piece_missing = {62.0F, 59.0F, 54.0F, 255.0F};
constexpr Clay_Color piece_have = {176.0F, 169.0F, 157.0F, 255.0F};
constexpr Clay_Color piece_evictable = {108.0F, 103.0F, 95.0F, 255.0F};
constexpr Clay_Color piece_evicted = {44.0F, 42.0F, 39.0F, 255.0F};

// Shape: radius encodes hierarchy. Lists are flat, controls are softened, switches are pills.
constexpr float radius_control = 4.0F;
constexpr float radius_menu = 6.0F;

// Type
constexpr uint16_t font_regular = 0;
constexpr uint16_t font_semibold = 1;

// Base sizes at the default 13 px setting; pages scale them with the user's text size.
constexpr uint16_t size_caption = 12;
constexpr uint16_t size_body = 14;
constexpr uint16_t size_lead = 16;
constexpr uint16_t size_title = 26;
constexpr uint16_t size_numeral = 22;

} // namespace torrview::ui::theme
