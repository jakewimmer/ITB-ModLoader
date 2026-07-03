#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <SDL2/SDL.h>
#include "itbsdl.h"

/* SDL_GL_SwapWindow interposition.
 *
 * Each frame, if draw hooks exist: construct a Screen, begin(), invoke each
 * hook in reverse order (later hooks take priority), finishWithoutSwapping(),
 * then call the real swap. The Screen captures the current GL context via
 * SDL_GL_GetCurrentWindow(). */
extern "C" void SDL_GL_SwapWindow(SDL_Window *window) {
  static void (*real)(SDL_Window *) =
      (void (*)(SDL_Window *))dlsym(RTLD_NEXT, "SDL_GL_SwapWindow");

  if (g_lua && !g_draw_hooks.empty()) {
    Screen screen;
    screen.begin();
    /* Snapshot the hook list to guard against iterator invalidation if a
     * hook's Lua function triggers garbage collection or hook registration.
     * WARNING: Snapshot holds void* pointers that may become dangling if a
     * hook's Lua function destroys another hook mid-iteration. Iteration is
     * safe (pointers are copied before modification), but dereferencing an
     * invalidated pointer would be unsafe. Currently mitigated by the fact that
     * hook destruction in Lua happens through refcount, and hooks are not
     * directly exposed to the caller during execution, but this remains a
     * potential hazard if hook lifetime management changes.
     */
    std::vector<void *> hooks_snapshot = g_draw_hooks;
    for (auto it = hooks_snapshot.rbegin(); it != hooks_snapshot.rend(); ++it) {
      DrawHook *hook = static_cast<DrawHook *>(*it);
      hook->draw(screen);
    }
    screen.finishWithoutSwapping();
  }

  /* Clear lastFrameMap once per frame after finishWithoutSwapping, matching
   * the proxy pattern (sdl-hooks.cpp:31): this ensures wasDrawn() gates
   * onModsLoaded correctly, and prevents mid-frame screen:finish() calls
   * from wiping state.
   */
  SDL::lastFrameMap.clear();
  real(window);
}

/* SDL_PollEvent interposition.
 *
 * Loop: pull a real event; feed each event hook; if a hook returns true
 * (consumed), keep pulling; if none consume, return the event to the game.
 * Input injection note (Phase 2, finding #8): the game imports only
 * SDL_PollEvent (not SDL_PushEvent), so any events the loader needs to
 * inject must surface through this return path. */
extern "C" int SDL_PollEvent(SDL_Event *evt) {
  static int (*real)(SDL_Event *) =
      (int (*)(SDL_Event *))dlsym(RTLD_NEXT, "SDL_PollEvent");

  if (!g_lua || g_event_hooks.empty() || evt == nullptr) {
    return real(evt);
  }

  for (;;) {
    int ret = real(evt);
    if (ret == 0) {
      return 0;
    }

    Event e;
    e.event = *evt;

    /* Snapshot the hook list to guard against iterator invalidation if a
     * hook's Lua function triggers garbage collection or hook registration.
     * WARNING: Snapshot holds void* pointers that may become dangling if a
     * hook's Lua function destroys another hook mid-iteration. Iteration is
     * safe (pointers are copied before modification), but dereferencing an
     * invalidated pointer would be unsafe. Currently mitigated by the fact that
     * hook destruction in Lua happens through refcount, and hooks are not
     * directly exposed to the caller during execution, but this remains a
     * potential hazard if hook lifetime management changes.
     */
    std::vector<void *> hooks_snapshot = g_event_hooks;
    bool handled = false;
    for (auto *hook_ptr : hooks_snapshot) {
      EventHook *hook = static_cast<EventHook *>(hook_ptr);
      if (hook->handle(e)) {
        handled = true;
        break;
      }
    }

    if (!handled) {
      return 1;  // pass unconsumed event to the game
    }
  }
}
