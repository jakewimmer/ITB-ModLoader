#pragma once

#include <lua.h>
#include <vector>
#include <SDL2/SDL.h>
#include "screen_gl.h"

/* Module globals shared across LD_PRELOAD interposers and loadlib registration. */
extern lua_State *g_lua;
extern std::vector<void *> g_draw_hooks;
extern std::vector<void *> g_event_hooks;

/* Forward declarations for Event and hook classes. */

struct DrawHook {
  virtual ~DrawHook() = default;
  virtual void draw(SDL::Screen &screen) = 0;
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

/* Expose SDL namespace classes as top-level for LuaBridge binding */
using SDL::Screen;
using SDL::Surface;
using SDL::Color;
using SDL::Rect;
using SDL::Timer;
using SDL::SurfaceScreenshot;
using SDL::mousex;
using SDL::mousey;
using SDL::setClipboardData;
using SDL::getClipboardData;
