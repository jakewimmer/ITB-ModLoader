#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <cstdint>
#include <cstddef>

#include "screen_gl.h"
#include "glew.h"

/*
 * Game OpenGL draw interposition.
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
 * These symbols are exported with default visibility so that, under LD_PRELOAD,
 * they interpose the game's libGL (GLVND) calls -- the same mechanism already
 * used for SDL_GL_SwapWindow / SDL_PollEvent. Intra-library calls from
 * screen_gl.cpp (Surface texture uploads) also route through here, so a
 * Surface's hash and a game texture's hash are produced by the identical path.
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

extern "C" void glBindTexture(GLenum target, GLuint texture) {
  static void (*real)(GLenum, GLuint) =
      (void (*)(GLenum, GLuint))dlsym(RTLD_NEXT, "glBindTexture");

  if (target == GL_TEXTURE_2D) {
    g_bound_texture = texture;
    /* Cache the hash for this texture to avoid redundant lookups in draw calls.
     * On texture-binding change, recompute; within a binding, mark_bound_texture_drawn
     * uses the cached value. */
    auto iter = SDL::texturesMap.find(texture);
    g_current_bound_hash = (iter != SDL::texturesMap.end()) ? iter->second : 0;
  }
  real(target, texture);
}

extern "C" void glTexImage2D(GLenum target, GLint level, GLint internalformat,
                             GLsizei width, GLsizei height, GLint border,
                             GLenum format, GLenum type, const GLvoid *pixels) {
  static void (*real)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum,
                      GLenum, const GLvoid *) =
      (void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                const GLvoid *))dlsym(RTLD_NEXT, "glTexImage2D");

  /* Match the proxy: only RGBA uploads (4 bytes/pixel) are hashed. Guard NULL
   * pixels (storage-only uploads) -- which the proxy does not -- to stay
   * crash-safe; such uploads carry no bytes to match against anyway. */
  if (format == GL_RGBA && pixels != nullptr && width > 0 && height > 0) {
    uint64_t hash = SDL::simple_hash(
        pixels, static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    SDL::texturesMap[g_bound_texture] = hash;
  }
  real(target, level, internalformat, width, height, border, format, type,
       pixels);
}

extern "C" void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
  static void (*real)(GLenum, GLint, GLsizei) =
      (void (*)(GLenum, GLint, GLsizei))dlsym(RTLD_NEXT, "glDrawArrays");

  mark_bound_texture_drawn();
  real(mode, first, count);
}

extern "C" void glDrawElements(GLenum mode, GLsizei count, GLenum type,
                               const GLvoid *indices) {
  static void (*real)(GLenum, GLsizei, GLenum, const GLvoid *) =
      (void (*)(GLenum, GLsizei, GLenum,
                const GLvoid *))dlsym(RTLD_NEXT, "glDrawElements");

  mark_bound_texture_drawn();
  real(mode, count, type, indices);
}
