// Shared module state. Because the same .so is loaded via LD_PRELOAD and
// package.loadlib, these globals are shared between the SDL interposers and the
// Lua-registered callbacks.
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <vector>
#include <algorithm>
#include <SDL2/SDL.h>
#include <LuaBridge/LuaBridge.h>
#include "itbsdl.h"

using namespace luabridge;

lua_State *g_lua = nullptr;
std::vector<void *> g_draw_hooks;
std::vector<void *> g_event_hooks;

/* Minimal Screen stub for Task 2. Task 3 provides the real implementation
 * with OpenGL rendering. */
Screen::Screen() {
  window = SDL_GL_GetCurrentWindow();
}

int Screen::w() {
  int w, h;
  SDL_GL_GetDrawableSize(window, &w, &h);
  return w;
}

int Screen::h() {
  int w, h;
  SDL_GL_GetDrawableSize(window, &w, &h);
  return h;
}

void Screen::begin() {
  // Task 3: set up GL projection, clipping, etc.
}

void Screen::finishWithoutSwapping() {
  // Task 3: tear down GL state.
}

void Screen::finish() {
  finishWithoutSwapping();
  // Task 3: swap buffers if called directly (normally SDL_GL_SwapWindow does this).
}

/* Event stub methods (Task 2). Task 3+ may expand these. */
int Event::type() { return event.type; }
int Event::x() { return event.motion.x; }
int Event::y() { return event.motion.y; }
int Event::wheely() { return event.wheel.y; }
int Event::keycode() { return event.key.keysym.sym; }
int Event::mousebutton() { return event.button.button; }

/* LuaBridge registration for hook classes and event constants.
 *
 * Hooks use GC-based lifetime: constructor pushes onto g_draw_hooks/
 * g_event_hooks; destructor removes. When Lua code stores the result in
 * a global or upvalue, the hook persists until that reference is cleared
 * or Lua garbage collects it. If the result is discarded, the hook is
 * freed immediately. */

namespace event {
  int quit = SDL_QUIT;
  int keydown = SDL_KEYDOWN;
  int keyup = SDL_KEYUP;
  int mousemotion = SDL_MOUSEMOTION;
  int mousebuttondown = SDL_MOUSEBUTTONDOWN;
  int mousebuttonup = SDL_MOUSEBUTTONUP;
  int mousewheel = SDL_MOUSEWHEEL;
  int textinput = SDL_TEXTINPUT;
}

/* C++ wrapper that stores the Lua function ref and manages hook list lifetime. */
struct DrawHookImpl : public DrawHook {
  LuaRef fn;

  DrawHookImpl(LuaRef f) : fn(f) {
    g_draw_hooks.push_back(this);
  }

  ~DrawHookImpl() {
    auto it = std::find(g_draw_hooks.begin(), g_draw_hooks.end(), this);
    if (it != g_draw_hooks.end()) {
      g_draw_hooks.erase(it);
    }
  }

  void draw(Screen &screen) override {
    try {
      fn(screen);
    } catch (const luabridge::LuaException &e) {
      // TODO: log error
    }
  }
};

struct EventHookImpl : public EventHook {
  LuaRef fn;

  EventHookImpl(LuaRef f) : fn(f) {
    g_event_hooks.push_back(this);
  }

  ~EventHookImpl() {
    auto it = std::find(g_event_hooks.begin(), g_event_hooks.end(), this);
    if (it != g_event_hooks.end()) {
      g_event_hooks.erase(it);
    }
  }

  bool handle(Event &evt) override {
    try {
      return fn(evt);
    } catch (const luabridge::LuaException &e) {
      // TODO: log error
      return false;
    }
  }
};

static void install_sdl_namespace(lua_State *L) {
  getGlobalNamespace(L)
      .beginNamespace("sdl")

      .beginClass<DrawHookImpl>("drawHook")
      .addConstructor<void(*)(LuaRef)>()
      .endClass()

      .beginClass<EventHookImpl>("eventHook")
      .addConstructor<void(*)(LuaRef)>()
      .endClass()

      .beginClass<Event>("event")
      .addConstructor<void(*)(void)>()
      .addFunction("type", &Event::type)
      .addFunction("keycode", &Event::keycode)
      .addFunction("x", &Event::x)
      .addFunction("y", &Event::y)
      .addFunction("wheel", &Event::wheely)
      .addFunction("mousebutton", &Event::mousebutton)
      .endClass()

      .beginNamespace("events")
      .addVariable("quit", &event::quit, false)
      .addVariable("keydown", &event::keydown, false)
      .addVariable("keyup", &event::keyup, false)
      .addVariable("mousemotion", &event::mousemotion, false)
      .addVariable("mousebuttondown", &event::mousebuttondown, false)
      .addVariable("mousebuttonup", &event::mousebuttonup, false)
      .addVariable("mousewheel", &event::mousewheel, false)
      .addVariable("textinput", &event::textinput, false)
      .endNamespace()

      .endNamespace();
}

extern "C" int luaopen_itbsdl(lua_State *L) {
  g_lua = L;
  luaL_openlibs(L);
  install_sdl_namespace(L);
  // TODO: install_os_namespace(L); // wired up in Task 5
  // TODO: install screen, surface, font, text, mouse, clipboard, resourcedat, blob // Tasks 3-5
  return 0;
}
