#pragma once

#include <lua.h>
#include <vector>
#include <SDL2/SDL.h>

/* Module globals shared across LD_PRELOAD interposers and loadlib registration. */
extern lua_State *g_lua;
extern std::vector<void *> g_draw_hooks;
extern std::vector<void *> g_event_hooks;

/* Forward declarations for minimal Task 2 stubs.
 * Real implementations in Task 3 (Screen), Task 2 (DrawHook/EventHook). */

struct Screen {
  SDL_Window *window;

  Screen();
  ~Screen() = default;

  int w();
  int h();
  void begin();
  void finishWithoutSwapping();
  void finish();
};

struct DrawHook {
  virtual ~DrawHook() = default;
  virtual void draw(Screen &screen) = 0;
};

struct Event {
  SDL_Event event;

  int type();
  int x();
  int y();
  int wheely();
  int keycode();
  int mousebutton();
};

struct EventHook {
  virtual ~EventHook() = default;
  virtual bool handle(Event &evt) = 0;
};
