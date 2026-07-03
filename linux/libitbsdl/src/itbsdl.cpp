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
#include "text_freetype.h"
#include "blob.h"

using namespace luabridge;

lua_State *g_lua = nullptr;
std::vector<void *> g_draw_hooks;
std::vector<void *> g_event_hooks;

/* Screen implementation is in screen_gl.cpp (Task 3) */

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

      /* Color class with static members and data fields */
      .beginClass<SDL::Color>("color")
      .addStaticData("white", &SDL::Color::White, false)
      .addStaticData("black", &SDL::Color::Black, false)
      .addStaticData("transparent", &SDL::Color::Transparent, false)
      .addData("r", &SDL::Color::r)
      .addData("g", &SDL::Color::g)
      .addData("b", &SDL::Color::b)
      .addData("a", &SDL::Color::a)
      .endClass()

      .deriveClass<SDL::Color, SDL::Color>("rgba")
      .addConstructor<void(*)(int, int, int, int)>()
      .endClass()

      .deriveClass<SDL::Color, SDL::Color>("rgb")
      .addConstructor<void(*)(int, int, int)>()
      .endClass()

      /* Rect class */
      .beginClass<SDL::Rect>("rect")
      .addConstructor<void(*)(int, int, int, int)>()
      .addData("x", &SDL::Rect::x)
      .addData("y", &SDL::Rect::y)
      .addData("w", &SDL::Rect::w)
      .addData("h", &SDL::Rect::h)
      .addFunction("contains", &SDL::Rect::contains)
      .addFunction("intersects", &SDL::Rect::intersects)
      .addFunction("getIntersect", &SDL::Rect::getIntersect)
      .addFunction("getUnion", &SDL::Rect::getUnion)
      .endClass()

      /* Timer class */
      .beginClass<SDL::Timer>("timer")
      .addConstructor<void(*)(void)>()
      .addFunction("elapsed", &SDL::Timer::elapsed)
      .addFunction("reset", &SDL::Timer::reset)
      .endClass()

      /* Surface class and derived transforms */
      .beginClass<SDL::Surface>("surface")
      .addConstructor<void(*)(const std::string &)>()
      .addFunction("w", &SDL::Surface::w)
      .addFunction("h", &SDL::Surface::h)
      .addFunction("padl", &SDL::Surface::leftPadding)
      .addFunction("padr", &SDL::Surface::rightPadding)
      .addData("x", &SDL::Surface::x, false)
      .addData("y", &SDL::Surface::y, false)
      .addFunction("wasDrawn", &SDL::Surface::wasDrawn)
      .endClass()

      .deriveClass<SDL::Surface, SDL::Surface>("surfaceFromBlob")
      .addConstructor<void(*)(const uint8_t *, size_t)>()
      .endClass()

      .deriveClass<SDL::Surface, SDL::Surface>("outlined")
      .addConstructor<void(*)(SDL::Surface *, int, SDL::Color *)>()
      .endClass()

      .deriveClass<SDL::Surface, SDL::Surface>("scaled")
      .addConstructor<void(*)(int, SDL::Surface *)>()
      .endClass()

      .deriveClass<SDL::Surface, SDL::Surface>("multiply")
      .addConstructor<void(*)(SDL::Surface *, SDL::Color *)>()
      .endClass()

      .deriveClass<SDL::Surface, SDL::Surface>("grayscale")
      .addConstructor<void(*)(SDL::Surface *, int)>()
      .endClass()

      .deriveClass<SDL::SurfaceScreenshot, SDL::Surface>("screenshot")
      .addConstructor<void(*)(void)>()
      .endClass()

      /* TextSettings: text rendering parameters */
      .beginClass<SDL::TextSettings>("textsettings")
      .addConstructor<void(*)(void)>()
      .addData("antialias", &SDL::TextSettings::antialias)
      .addData("color", &SDL::TextSettings::color)
      .addData("outlineWidth", &SDL::TextSettings::outlineWidth)
      .addData("outlineColor", &SDL::TextSettings::outlineColor)
      .endClass()

      /* Font: system font resolved via Fontconfig */
      .beginClass<SDL::Font>("font")
      .addConstructor<void(*)(const std::string &, double)>()
      .addFunction("ascent", &SDL::Font::getAscent)
      .addFunction("descent", &SDL::Font::getDescent)
      .endClass()

      /* FileFont: FreeType font from file path */
      .deriveClass<SDL::FileFont, SDL::Font>("filefont")
      .addConstructor<void(*)(const std::string &, double)>()
      .endClass()

      /* FileFont: FreeType font from memory blob */
      .deriveClass<SDL::FileFont, SDL::Font>("filefontFromBlob")
      .addConstructor<void(*)(const SDL::Blob *, double)>()
      .endClass()

      /* Text: render UTF-8 string to RGBA surface */
      .deriveClass<SDL::Surface, SDL::Surface>("text")
      .addConstructor<void(*)(const SDL::Font *, const SDL::TextSettings *,
                              const std::string &)>()
      .endClass()

      /* Screen class */
      .beginClass<SDL::Screen>("screen")
      .addConstructor<void(*)(void)>()
      .addFunction("w", &SDL::Screen::w)
      .addFunction("h", &SDL::Screen::h)
      .addFunction("begin", &SDL::Screen::begin)
      .addFunction("finish", &SDL::Screen::finish)
      .addFunction("finishWithoutSwapping", &SDL::Screen::finishWithoutSwapping)
      .addFunction("blit", &SDL::Screen::blit)
      .addFunction("blitRect", &SDL::Screen::blitRect)
      .addFunction("drawrect", &SDL::Screen::drawrect)
      .addFunction("clip", &SDL::Screen::clip)
      .addFunction("unclip", &SDL::Screen::unclip)
      .addFunction("mask", &SDL::Screen::mask)
      .addFunction("unmask", &SDL::Screen::unmask)
      .addFunction("clearmask", &SDL::Screen::clearmask)
      .addFunction("getClipRect", &SDL::Screen::getClipRect)
      .endClass()

      /* Hook classes */
      .beginClass<DrawHookImpl>("drawHook")
      .addConstructor<void(*)(LuaRef)>()
      .endClass()

      .beginClass<EventHookImpl>("eventHook")
      .addConstructor<void(*)(LuaRef)>()
      .endClass()

      /* Event class */
      .beginClass<Event>("event")
      .addConstructor<void(*)(void)>()
      .addFunction("type", &Event::type)
      .addFunction("keycode", &Event::keycode)
      .addFunction("x", &Event::x)
      .addFunction("y", &Event::y)
      .addFunction("wheel", &Event::wheely)
      .addFunction("mousebutton", &Event::mousebutton)
      .endClass()

      /* Event type constants */
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

      /* Mouse position */
      .beginNamespace("mouse")
      .addFunction("x", SDL::mousex)
      .addFunction("y", SDL::mousey)
      .endNamespace()

      /* Clipboard */
      .beginNamespace("clipboard")
      .addFunction("set", SDL::setClipboardData)
      .addFunction("get", SDL::getClipboardData)
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
