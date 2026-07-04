#pragma once

/* Exported dispatch entry points into libitbsdl's frame-hook + texture-tracking
 * logic. libitbsdl no longer interposes SDL/GL symbols directly; instead the
 * thin libitbboot preload interposes them and forwards here (see itbboot.c).
 *
 * These are the only rendering-side symbols libitbsdl exports besides
 * luaopen_itbsdl. Declared extern "C" with default visibility so libitbboot can
 * resolve them via dlsym(RTLD_DEFAULT, ...) once libitbsdl is loaded. */

extern "C" {

/* Per-frame draw-hook pass. Runs every registered DrawHook against a fresh
 * Screen, then clears lastFrameMap. Does NOT swap the window (the preload calls
 * the real SDL_GL_SwapWindow afterwards). Screen resolves the current GL context
 * via SDL_GL_GetCurrentWindow(), so no window handle is needed. */
void itbsdl_dispatch_swapwindow(void);

/* Classify one already-pulled SDL event. Returns 1 if an event hook consumed it
 * (caller should keep pulling), 0 if it should be handed to the game. The
 * pull loop lives in the libitbboot SDL_PollEvent shim. */
int itbsdl_dispatch_event(void *evt_voidp);

/* Track the currently bound GL_TEXTURE_2D name and cache its known hash. */
void itbsdl_dispatch_bindtexture(unsigned int target, unsigned int texture);

/* Hash an RGBA texture upload into texturesMap[boundTexture]. */
void itbsdl_dispatch_teximage2d(unsigned int target, int internalformat,
                                int width, int height, unsigned int format,
                                const void *pixels);

/* Mark the currently bound texture as drawn this frame. */
void itbsdl_dispatch_draw(void);

}  // extern "C"
