#include <SDL2/SDL.h>
#include <vector>
#include "itbsdl.h"
#include "itbsdl_dispatch.h"

/* Frame-hook dispatch entry points.
 *
 * These carry the bodies of what used to be the SDL_GL_SwapWindow and
 * SDL_PollEvent interposers, minus the real-function calls. libitbsdl no longer
 * exports the SDL symbols; the thin libitbboot preload interposes them and
 * forwards here (see itbboot.c). Dispatching from the preload is what makes the
 * hooks actually fire under LD_PRELOAD -- interposition from this large,
 * dual-loaded library did not. */

/* Per-frame draw pass.
 *
 * If draw hooks exist: construct a Screen, begin(), invoke each hook in reverse
 * order (later hooks take priority), finishWithoutSwapping(). The Screen
 * captures the current GL context via SDL_GL_GetCurrentWindow(), so no window
 * handle is needed. Then clear lastFrameMap. The caller performs the real swap. */
extern "C" void itbsdl_dispatch_swapwindow(void) {
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
}

/* Classify one already-pulled SDL event.
 *
 * Feeds the event to each event hook (snapshotted to survive mid-iteration hook
 * mutation). Returns 1 if a hook consumed it -- the caller keeps pulling -- or 0
 * to pass the event to the game. The pull loop lives in the libitbboot
 * SDL_PollEvent shim. */
extern "C" int itbsdl_dispatch_event(void *evt_voidp) {
  if (!g_lua || g_event_hooks.empty() || evt_voidp == nullptr) {
    return 0;
  }

  Event e;
  e.event = *static_cast<SDL_Event *>(evt_voidp);

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
  for (auto *hook_ptr : hooks_snapshot) {
    EventHook *hook = static_cast<EventHook *>(hook_ptr);
    if (hook->handle(e)) {
      return 1;  // consumed
    }
  }

  return 0;  // pass unconsumed event to the game
}
