#include "screen_gl.h"
#include "blob.h"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <climits>
#include <string>
/* GLEW_STATIC / GLEW_NO_GLU (needed so glew.h skips the missing system GL/glu.h)
 * are supplied for the whole target via target_compile_definitions. */
#include "glew.h"

/* stb_image implementation */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "../third_party/stb_image.h"

namespace SDL {

/* Module globals */
std::map<uint32_t, uint64_t> texturesMap;
std::map<uint64_t, Coord> lastFrameMap;

/* 64-bit FNV-1a hash over raw pixel bytes. Declared in screen_gl.h so the GL
 * draw interposers hash game texture uploads through the identical code path. */
uint64_t simple_hash(const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  uint64_t h = 0xcbf29ce484222325ULL;
  for (size_t i = 0; i < len; i++) {
    h ^= p[i];
    h *= 0x100000001b3ULL;
  }
  return h;
}

/* GLEW initialization guard */
static bool g_glew_initialized = false;
static void ensure_glew_init() {
  if (!g_glew_initialized) {
    glewExperimental = GL_TRUE;
    glewInit();
    g_glew_initialized = true;
  }
}

/* Color static members */
Color Color::White = Color(255, 255, 255, 255);
Color Color::Black = Color(0, 0, 0, 255);
Color Color::Transparent = Color(0, 0, 0, 0);

Color::Color() : r(255), g(255), b(255), a(255) {}

Color::Color(int red, int green, int blue, int alpha)
    : r(static_cast<uint8_t>(red)), g(static_cast<uint8_t>(green)),
      b(static_cast<uint8_t>(blue)), a(static_cast<uint8_t>(alpha)) {}

Color::Color(int red, int green, int blue)
    : r(static_cast<uint8_t>(red)), g(static_cast<uint8_t>(green)),
      b(static_cast<uint8_t>(blue)), a(255) {}

/* Rect methods */
Rect::Rect(int xx, int yy, int ww, int hh) : x(xx), y(yy), w(ww), h(hh) {}

bool Rect::contains(int px, int py) {
  SDL_Point p = {px, py};
  SDL_Rect sdl_rect = {x, y, w, h};
  return SDL_PointInRect(&p, &sdl_rect) == SDL_TRUE;
}

bool Rect::intersects(Rect *other) {
  SDL_Rect a = {x, y, w, h};
  SDL_Rect b = {other->x, other->y, other->w, other->h};
  return SDL_HasIntersection(&a, &b) == SDL_TRUE;
}

Rect Rect::getIntersect(Rect *other) {
  SDL_Rect a = {x, y, w, h};
  SDL_Rect b = {other->x, other->y, other->w, other->h};
  SDL_Rect result;
  if (SDL_IntersectRect(&a, &b, &result)) {
    return Rect(result.x, result.y, result.w, result.h);
  }
  return Rect(0, 0, 0, 0);
}

Rect Rect::getUnion(Rect *other) {
  SDL_Rect a = {x, y, w, h};
  SDL_Rect b = {other->x, other->y, other->w, other->h};
  SDL_Rect result;
  SDL_UnionRect(&a, &b, &result);
  return Rect(result.x, result.y, result.w, result.h);
}

/* Timer methods */
Timer::Timer() { reset(); }

int Timer::elapsed() {
  return static_cast<int>(SDL_GetTicks() - startTime);
}

void Timer::reset() {
  startTime = SDL_GetTicks();
}

/* Mouse position helpers */
int mousex() {
  int x = 0;
  SDL_GetMouseState(&x, nullptr);
  return x;
}

int mousey() {
  int y = 0;
  SDL_GetMouseState(nullptr, &y);
  return y;
}

/* Clipboard helpers */
void setClipboardData(const std::string &text) {
  SDL_SetClipboardText(text.c_str());
}

std::string getClipboardData() {
  char *text = SDL_GetClipboardText();
  std::string result(text ? text : "");
  SDL_free(text);
  return result;
}

/* GL texture upload: create texture from RGBA pixel data */
static uint32_t glTexture(unsigned char *pixelData, int w, int h) {
  ensure_glew_init();

  uint32_t texture = 0;

  GLenum texture_format = GL_RGBA;
  GLenum tex_type = GL_UNSIGNED_INT_8_8_8_8_REV;
  GLenum internal_format = GL_RGBA8;

  int pitch = w * 4;
  int alignment = 8;
  while (pitch % alignment)
    alignment >>= 1;
  glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);

  int expected_pitch = (w * 4 + alignment - 1) / alignment * alignment;
  if (pitch - expected_pitch >= alignment)
    glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch / 4);
  else
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);

  glTexImage2D(GL_TEXTURE_2D, 0, internal_format, w, h, 0, texture_format,
               tex_type, pixelData);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

  return texture;
}

/* Surface implementation */
void Surface::init() {
  pixelData = nullptr;
  textureId = 0;
  hash = 0;
  width = 0;
  height = 0;
  padl = 0;
  padr = 0;
  x = 0;
  y = 0;
}

Surface::Surface() { init(); }

Surface::~Surface() {
  if (pixelData != nullptr)
    delete[] pixelData;
  if (textureId != 0)
    glDeleteTextures(1, &textureId);
}

void Surface::createSurfaceFromPixelData(int w, int h) {
  hash = simple_hash(pixelData, w * h * 4);
  width = w;
  height = h;
}

void Surface::setBitmap(const uint8_t *data, int sx, int sy, int w, int h,
                        int stride) {
  if (!data || w <= 0 || h <= 0)
    return;

  /* Check for integer overflow: ensure w * h * 4 won't overflow */
  if (w > static_cast<int>(SIZE_MAX / 4) ||
      h > static_cast<int>(SIZE_MAX / (4 * w))) {
    return;
  }

  pixelData = new unsigned char[w * h * 4];
  int initial = 0;
  if (stride < 0) {
    initial = (sy + h - 1) * (-stride);
  }

  for (int y = 0; y < h; y++) {
    unsigned char *dst = pixelData + (y * w * 4);
    const unsigned char *src =
        data + (initial + sx * 4 + (sy + y) * stride);

    for (int x = 0; x < w; x++) {
      /* stb_image gives RGBA; need BGRA for GL_UNSIGNED_INT_8_8_8_8_REV */
      dst[0] = src[2];  // B
      dst[1] = src[1];  // G
      dst[2] = src[0];  // R
      dst[3] = src[3];  // A

      dst += 4;
      src += 4;
    }
  }

  createSurfaceFromPixelData(w, h);
}

/* Load surface from file using stb_image */
Surface::Surface(const std::string &filename) {
  init();

  int w, h, channels;
  unsigned char *data =
      stbi_load(filename.c_str(), &w, &h, &channels, STBI_rgb_alpha);
  if (!data) {
    // Image load failed
    return;
  }

  setBitmap(data, 0, 0, w, h, w * 4);
  stbi_image_free(data);
}

/* Load surface from blob (in-memory image data) */
Surface::Surface(const Blob *blob) {
  init();

  if (!blob || !blob->data || blob->length == 0) {
    return;
  }

  int w, h, channels;
  unsigned char *data =
      stbi_load_from_memory(blob->data, static_cast<int>(blob->length), &w, &h,
                            &channels, STBI_rgb_alpha);
  if (!data) {
    return;
  }

  setBitmap(data, 0, 0, w, h, w * 4);
  stbi_image_free(data);
}

bool Surface::isValid() const {
  return pixelData != nullptr;
}

uint32_t Surface::texture() {
  if (textureId == 0 && isValid()) {
    /* glTexture() binds the new texture and calls glTexImage2D, which routes
     * through the interposed glTexImage2D (gl_interpose.cpp) and records
     * texturesMap[textureId] = simple_hash(pixelData) -- the same value as
     * this->hash. The game's own textures are hashed by the identical path, so
     * a Surface and an identically-pixelled game texture share one hash. */
    textureId = glTexture(pixelData, width, height);
  }
  return textureId;
}

bool Surface::wasDrawn() {
  auto iter = lastFrameMap.find(hash);
  if (iter == lastFrameMap.end())
    return false;

  x = iter->second.x;
  y = iter->second.y;
  return true;
}

/* Outlined surface: adds colored outline around existing surface */
void Surface::addOutline(int levels, const Color *color) {
  if (levels == 0 || width <= 0 || height <= 0)
    return;

  uint32_t colorValue =
      (0xff << 24) | (color->b << 16) | (color->g << 8) | (color->r);

  int w = width;
  int h = height;

  /* Check for integer overflow before allocation */
  if (w > static_cast<int>(SIZE_MAX / 4) ||
      h > static_cast<int>(SIZE_MAX / (4 * w))) {
    return;
  }

  unsigned char *data = new unsigned char[w * h * 4];
  unsigned char *data2 = new unsigned char[w * h * 4];

  std::memcpy(data, pixelData, 4 * w * h);

  for (int i = 0; i < levels; i++) {
    std::memcpy(data2, data, 4 * w * h);

    /* Horizontal pass */
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        unsigned char *p = &data[(x + y * w) * 4];
        unsigned char *p2 = &data2[(x + y * w) * 4];
        if (p[3] > 128) {
          if (x > 0 && p[-4 + 3] < 128)
            *((uint32_t *)p2 - 1) = colorValue;
          if (x + 1 < w && p[4 + 3] < 128)
            *((uint32_t *)p2 + 1) = colorValue;
        }
      }
    }

    std::memcpy(data, data2, 4 * w * h);

    /* Vertical pass */
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        unsigned char *p = &data[(x + y * w) * 4];
        unsigned char *p2 = &data2[(x + y * w) * 4];
        if (p[3] > 128) {
          if (y > 0 && p[-w * 4 + 3] < 128)
            *((uint32_t *)p2 - w) = colorValue;
          if (y + 1 < h && p[w * 4 + 3] < 128)
            *((uint32_t *)p2 + w) = colorValue;
        }
      }
    }

    std::swap(data, data2);
  }

  std::memcpy(pixelData, data, 4 * w * h);

  delete[] data;
  delete[] data2;
}

/* Outlined: create outlined version of parent surface */
Surface::Surface(Surface *parent, int levels, Color *color) {
  init();
  if (!parent || !parent->isValid())
    return;

  int w = parent->w();
  int h = parent->h();

  /* Check for integer overflow before allocation */
  if (w > static_cast<int>(SIZE_MAX / 4) ||
      h > static_cast<int>(SIZE_MAX / (4 * w))) {
    return;
  }

  pixelData = new unsigned char[w * h * 4];
  std::memcpy(pixelData, parent->pixelData, 4 * w * h);

  width = w;
  height = h;

  addOutline(levels, color);

  createSurfaceFromPixelData(w, h);
}

/* Scaled: create scaled version of parent surface (nearest-neighbor) */
Surface::Surface(int scaling, Surface *parent) {
  init();
  if (!parent || !parent->isValid())
    return;

  int w = parent->w();
  int h = parent->h();

  int neww = w * scaling;
  int newh = h * scaling;

  uint32_t *data = new uint32_t[neww * newh];
  uint32_t *pixels = (uint32_t *)parent->pixelData;

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uint32_t pixel = pixels[y * w + x];
      for (int y1 = 0; y1 < scaling; y1++) {
        for (int x1 = 0; x1 < scaling; x1++) {
          data[(y * scaling + y1) * neww + (x * scaling + x1)] = pixel;
        }
      }
    }
  }

  pixelData = (unsigned char *)data;
  createSurfaceFromPixelData(neww, newh);
}

/* Colormapped: remap colors from a list */
Surface::Surface(Surface *parent, std::vector<Color *> colormap) {
  init();
  if (!parent || !parent->isValid())
    return;

  std::map<uint32_t, uint32_t> map;
  for (size_t i = 0; i + 1 < colormap.size(); i += 2) {
    Color *from = colormap[i];
    Color *to = colormap[i + 1];

    uint32_t fromPx = from->r | (from->g << 8) | (from->b << 16);
    uint32_t toPx = to->r | (to->g << 8) | (to->b << 16);

    map[fromPx] = toPx;
  }

  int w = parent->w();
  int h = parent->h();

  /* Check for integer overflow before allocation */
  if (w <= 0 || h <= 0 || w > static_cast<int>(SIZE_MAX / sizeof(uint32_t)) ||
      h > static_cast<int>(SIZE_MAX / (sizeof(uint32_t) * w))) {
    return;
  }

  uint32_t *data = new uint32_t[w * h];
  uint32_t *pixels = (uint32_t *)parent->pixelData;

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uint32_t pixel = pixels[y * w + x];

      uint32_t color = pixel & 0x00ffffff;
      auto iter = map.find(color);
      if (iter != map.end()) {
        uint32_t alpha = pixel & 0xff000000;
        data[y * w + x] = iter->second | alpha;
      } else {
        data[y * w + x] = pixel;
      }
    }
  }

  pixelData = (unsigned char *)data;
  createSurfaceFromPixelData(w, h);
}

/* Multiply: tint surface with a color (multiply blend) */
Surface::Surface(Surface *parent, Color *color) {
  init();
  if (!parent || !parent->isValid())
    return;

  int w = parent->w();
  int h = parent->h();

  /* Check for integer overflow before allocation */
  if (w <= 0 || h <= 0 || w > static_cast<int>(SIZE_MAX / sizeof(uint32_t)) ||
      h > static_cast<int>(SIZE_MAX / (sizeof(uint32_t) * w))) {
    return;
  }

  uint32_t *data = new uint32_t[w * h];
  uint32_t *pixels = (uint32_t *)parent->pixelData;

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uint32_t pixel = pixels[y * w + x];
      uint32_t r = ((pixel & 0x000000ff) * color->r / 0xff) & 0x000000ff;
      uint32_t g =
          ((pixel & 0x0000ff00) * color->g / 0xff) & 0x0000ff00;
      uint32_t b =
          ((pixel & 0x00ff0000) * color->b / 0xff) & 0x00ff0000;
      uint32_t a =
          (((pixel >> 24) & 0x000000ff) * color->a / 0xff) << 24;
      data[y * w + x] = a | r | g | b;
    }
  }

  pixelData = (unsigned char *)data;
  createSurfaceFromPixelData(w, h);
}

/* Grayscale: convert surface to grayscale */
Surface::Surface(Surface *parent, [[maybe_unused]] int type) {
  init();
  if (!parent || !parent->isValid())
    return;

  int w = parent->w();
  int h = parent->h();

  /* Check for integer overflow before allocation */
  if (w <= 0 || h <= 0 || w > static_cast<int>(SIZE_MAX / sizeof(uint32_t)) ||
      h > static_cast<int>(SIZE_MAX / (sizeof(uint32_t) * w))) {
    return;
  }

  uint32_t *data = new uint32_t[w * h];
  uint32_t *pixels = (uint32_t *)parent->pixelData;

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uint32_t pixel = pixels[y * w + x];
      uint32_t r = pixel & 0x000000ff;
      uint32_t g = (pixel >> 8) & 0x000000ff;
      uint32_t b = (pixel >> 16) & 0x000000ff;
      uint32_t gray = (21 * r + 72 * g + 7 * b) / 100;
      data[y * w + x] = (pixel & 0xff000000) | gray | (gray << 8) |
                        (gray << 16);
    }
  }

  pixelData = (unsigned char *)data;
  createSurfaceFromPixelData(w, h);
}

/* SurfaceScreenshot: capture current GL framebuffer */
SurfaceScreenshot::SurfaceScreenshot() {
  init();

  SDL_Window *window = SDL_GL_GetCurrentWindow();
  if (!window)
    return;

  int w, h;
  SDL_GL_GetDrawableSize(window, &w, &h);

  /* Check for integer overflow before allocation */
  if (w <= 0 || h <= 0 || w > static_cast<int>(SIZE_MAX / 4) ||
      h > static_cast<int>(SIZE_MAX / (4 * w))) {
    return;
  }

  unsigned char *pixels = new unsigned char[4 * w * h];
  glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, pixels);

  setBitmap(pixels, 0, 0, w, h, -w * 4);

  delete[] pixels;
}

/* Screen implementation */
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
  ensure_glew_init();

  int w, h;
  SDL_GL_GetDrawableSize(window, &w, &h);

  glViewport(0, 0, w, h);
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glOrtho(0.0, w, h, 0.0, -1.0, 1.0);
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();

  glLoadIdentity();
  glDisable(GL_LIGHTING);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Screen::finishWithoutSwapping() {
  glDisable(GL_TEXTURE_2D);
  glPopMatrix();

  glMatrixMode(GL_PROJECTION);
  glPopMatrix();

  glMatrixMode(GL_MODELVIEW);
}

void Screen::finish() {
  finishWithoutSwapping();
  SDL_GL_SwapWindow(window);
}

void Screen::blitRect(Surface *src, [[maybe_unused]] Rect *srcRect,
                      Rect *destRect, Color *color) {
  if (!src || !src->isValid())
    return;

  int x1 = destRect->x;
  int y1 = destRect->y;
  int x2 = destRect->x + destRect->w;
  int y2 = destRect->y + destRect->h;

  glColor4f(color->r / 255.0f, color->g / 255.0f, color->b / 255.0f,
            color->a / 255.0f);

  uint32_t texId = src->texture();
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, texId);

  /* Track that this surface was drawn at this position */
  Coord coord;
  coord.x = x1;
  coord.y = y1;
  lastFrameMap[src->hash] = coord;

  /* Note: srcRect is accepted but currently ignored; always renders full
   * texture. This matches the Windows proxy behavior (sdl-utils.cpp:719).
   * Sub-rect blitting would require computing texCoord bounds from srcRect.
   */
  glBegin(GL_QUADS);
  glTexCoord2f(0, 0);
  glVertex3i(x1, y1, 0);
  glTexCoord2f(0, 1);
  glVertex3i(x1, y2, 0);
  glTexCoord2f(1, 1);
  glVertex3i(x2, y2, 0);
  glTexCoord2f(1, 0);
  glVertex3i(x2, y1, 0);
  glEnd();
}

void Screen::blit(Surface *src, Rect *srcRect, int destx, int desty) {
  if (!src || !src->isValid())
    return;

  Rect destRect(destx, desty, src->w(), src->h());
  blitRect(src, srcRect, &destRect, &Color::White);
}

void Screen::drawrect(Color *color, Rect *rect) {
  glColor4f(color->r / 255.0f, color->g / 255.0f, color->b / 255.0f,
            color->a / 255.0f);
  glDisable(GL_TEXTURE_2D);

  int x1, y1, x2, y2;
  if (rect == nullptr) {
    int w, h;
    SDL_GL_GetDrawableSize(window, &w, &h);

    x1 = 0;
    y1 = 0;
    x2 = w;
    y2 = h;
  } else {
    x1 = rect->x;
    y1 = rect->y;
    x2 = rect->x + rect->w;
    y2 = rect->y + rect->h;
  }

  glBegin(GL_QUADS);
  glVertex3i(x1, y1, 0);
  glVertex3i(x1, y2, 0);
  glVertex3i(x2, y2, 0);
  glVertex3i(x2, y1, 0);
  glEnd();
}

void Screen::clip(Rect *rect) {
  clippingRects.push_back(*rect);
  applyClipping();
}

void Screen::unclip() {
  if (!clippingRects.empty())
    clippingRects.pop_back();

  applyClipping();
}

void Screen::mask(Rect *rect) {
  glEnable(GL_STENCIL_TEST);

  glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
  glStencilFunc(GL_ALWAYS, 1, 0xFF);
  glStencilMask(0xFF);

  maskRects.push_back(*rect);
  drawrect(&Color::Transparent, rect);

  glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
  glStencilFunc(GL_EQUAL, 0, 0xFF);
  glStencilMask(0x00);
}

void Screen::unmask(size_t count) {
  glEnable(GL_STENCIL_TEST);

  glStencilOp(GL_KEEP, GL_KEEP, GL_DECR);
  glStencilFunc(GL_ALWAYS, 1, 0xFF);
  glStencilMask(0xFF);

  while (count-- > 0 && !maskRects.empty()) {
    auto rect = maskRects.back();

    drawrect(&Color::Transparent, &rect);

    maskRects.pop_back();
  }

  glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
  glStencilFunc(GL_EQUAL, 0, 0xFF);
  glStencilMask(0x00);

  if (maskRects.empty())
    glDisable(GL_STENCIL_TEST);
}

void Screen::clearmask() {
  glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
  glStencilFunc(GL_ALWAYS, 0, 0xFF);
  glStencilMask(0xFF);

  maskRects.clear();

  glClear(GL_STENCIL_BUFFER_BIT);
  glDisable(GL_STENCIL_TEST);
}

void Screen::applyClipping() {
  if (clippingRects.empty()) {
    glDisable(GL_SCISSOR_TEST);
  } else {
    int w, h;
    SDL_GL_GetDrawableSize(window, &w, &h);

    Rect *rect = &clippingRects.back();
    glScissor(rect->x, h - rect->y - rect->h, rect->w, rect->h);
    glEnable(GL_SCISSOR_TEST);
  }
}

Rect *Screen::getClipRect() {
  if (clippingRects.empty())
    return nullptr;

  return &clippingRects.back();
}

}  // namespace SDL
