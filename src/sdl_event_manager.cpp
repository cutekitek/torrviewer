#include "sdl_event_manager.hpp"

namespace torrview {

void SdlEventManager::pump(SdlEventHandler& handler) {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      handler.request_quit();
      break;

    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
      handler.window_metrics_changed();
      break;

    case SDL_EVENT_KEY_DOWN:
      handler.key_down(event.key);
      break;

    case SDL_EVENT_TEXT_INPUT:
      break;

    case SDL_EVENT_MOUSE_MOTION:
      handler.mouse_motion(event.motion);
      break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
      handler.mouse_button(event.button);
      break;

    case SDL_EVENT_MOUSE_WHEEL:
      handler.mouse_wheel(event.wheel);
      break;

    case SDL_EVENT_DROP_FILE:
      if (event.drop.data != nullptr) {
        handler.drop_file(event.drop.data);
      }
      break;

    case SDL_EVENT_DROP_TEXT:
      if (event.drop.data != nullptr) {
        handler.drop_text(event.drop.data);
      }
      break;

    case SDL_EVENT_DROP_BEGIN:
      handler.drop_begin();
      break;

    case SDL_EVENT_DROP_COMPLETE:
      handler.drop_complete();
      break;

    case SDL_EVENT_DROP_POSITION:
      handler.drop_position(event.drop.x, event.drop.y);
      break;

    default:
      break;
    }
  }
}

} // namespace torrview
