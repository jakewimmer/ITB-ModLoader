#include <cstdint>
#include <cstddef>

#include "screen_gl.h"
#include "glew.h"
#include "itbsdl_dispatch.h"

/*
 * Game OpenGL draw tracking.
 *
 * The mod loader gates onInitialLoadingFinished -> onModsLoaded on detecting
 * the game's main-menu background as drawn, via Surface::wasDrawn() checking
 * lastFrameMap. libitbsdl populated lastFrameMap only from its own blits, so
 * the game drawing its menu background never registered and the gate never
 * opened.
 *
 * This mirrors the Windows opengl32 proxy (DLL-Extensions/sdl-hooks.cpp):
 *   glBindTexture -> remember the currently bound GL_TEXTURE_2D
 *   glTexImage2D  -> hash uploaded RGBA pixels into texturesMap[boundTexture]
 *   draw call     -> if the bound texture is known, mark lastFrameMap[hash]
 *
 * The proxy records the draw marker at vertex-submission time (glVertex2f /
 * glVertexPointer / glVertexAttribPointer) because that is how the Windows game
 * path submits geometry. The native Linux Breach binary is a fixed-function
 * client-arrays renderer -- it submits geometry via glDrawArrays / glDrawElements
 * (no immediate mode, no shaders) -- so those calls are the correct per-draw
 * trigger here.
 *
 * These are exposed as exported dispatch entry points (itbsdl_dispatch_*), not
 * as GL interposers. libitbsdl no longer interposes glBindTexture / glTexImage2D
 * / glDrawArrays / glDrawElements; the thin libitbboot preload does, and forwards
 * here. screen_gl.cpp's own glBindTexture / glTexImage2D calls (Surface texture
 * uploads) are left undefined in this library and bind at load time to the same
 * libitbboot interposers, which forward back here -- so a Surface's texturesMap
 * entry and a game texture's are produced by the identical hashing path.
 */

namespace {

/* The GL_TEXTURE_2D name most recently passed to glBindTexture. Read by the
 * glTexImage2D hash step and the draw-marker step below. */
GLuint g_bound_texture = 0;

/* The hash value corresponding to the currently bound texture, cached to avoid
 * redundant map lookups on every draw call. Reset when a new texture is bound. */
uint64_t g_current_bound_hash = 0;

/* Record that the currently bound texture was drawn this frame. Coord is left
 * at its default (0,0): wasDrawn() only tests presence in lastFrameMap for the
 * onModsLoaded gate, so deriving exact draw positions (which the proxy computes
 * from vertex submission and the modelview stack) is unnecessary here. Only
 * records once per texture-binding change per frame (dedup via cached hash). */
void mark_bound_texture_drawn() {
  if (g_current_bound_hash != 0) {
    SDL::lastFrameMap[g_current_bound_hash] = SDL::Coord();
  }
}

}  // namespace

extern "C" void itbsdl_dispatch_bindtexture(unsigned int target,
                                            unsigned int texture) {
  if (target == GL_TEXTURE_2D) {
    g_bound_texture = texture;
    /* Cache the hash for this texture to avoid redundant lookups in draw calls.
     * On texture-binding change, recompute; within a binding, mark_bound_texture_drawn
     * uses the cached value. */
    auto iter = SDL::texturesMap.find(texture);
    g_current_bound_hash = (iter != SDL::texturesMap.end()) ? iter->second : 0;
  }
}

extern "C" void itbsdl_dispatch_teximage2d(unsigned int /*target*/,
                                           int /*internalformat*/, int width,
                                           int height, unsigned int format,
                                           unsigned int type,
                                           const void *pixels) {
  /* Match the proxy: only 8-bit RGBA uploads (4 bytes/pixel) are hashed. The
   * type == GL_UNSIGNED_BYTE check makes the 4-bytes/pixel read explicit and
   * crash-safe -- a packed type (e.g. GL_UNSIGNED_SHORT_4_4_4_4) with GL_RGBA
   * carries fewer bytes and must not be read as width*height*4. Guard NULL
   * pixels (storage-only uploads) -- which the proxy does not -- to stay
   * crash-safe; such uploads carry no bytes to match against anyway. */
  if (format == GL_RGBA && type == GL_UNSIGNED_BYTE && pixels != nullptr &&
      width > 0 && height > 0) {
    uint64_t hash = SDL::simple_hash(
        pixels, static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    SDL::texturesMap[g_bound_texture] = hash;
  }
}

extern "C" void itbsdl_dispatch_draw(void) { mark_bound_texture_drawn(); }
