#pragma once

#include <SDL2/SDL.h>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>

namespace SDL {

// Forward declarations
struct Color;
struct Rect;
struct Surface;
struct Screen;
class Font;
struct TextSettings;

/* Texture management: track which textures were drawn this frame for wasDrawn() */
struct Coord {
  double x;
  double y;
  Coord() : x(0), y(0) {}
};

extern std::map<uint32_t, uint64_t> texturesMap;      // GLuint -> hash
extern std::map<uint64_t, Coord> lastFrameMap;        // hash -> x,y from last frame

/**
 * Color: RGBA color with static convenience values
 */
struct Color {
  uint8_t r, g, b, a;

  static Color White;
  static Color Black;
  static Color Transparent;

  Color();
  Color(int r, int g, int b, int a);
  Color(int r, int g, int b);
};

/**
 * Rect: Rectangle with collision detection helpers
 */
struct Rect {
  int x, y, w, h;

  Rect(int x, int y, int w, int h);
  bool contains(int x, int y);
  bool intersects(Rect *other);
  Rect getIntersect(Rect *other);
  Rect getUnion(Rect *other);
};

/**
 * Timer: SDL_GetTicks-based elapsed time tracker
 */
struct Timer {
  uint32_t startTime;

  Timer();
  int elapsed();
  void reset();
};

/**
 * Surface: GL texture with pixel buffer and transformation support
 */
struct Surface {
  unsigned char *pixelData;
  uint32_t textureId;
  uint64_t hash;
  int width;
  int height;
  double x;
  double y;
  int padl;   // left padding (for text)
  int padr;   // right padding (for text)

  Surface();
  ~Surface();

  void init();
  void createSurfaceFromPixelData(int w, int h);

  /* Image loading constructors */
  explicit Surface(const std::string &filename);        // from file
  explicit Surface(const uint8_t *blob, size_t len);   // from blob

  /* Text rendering (Task 4: FreeType) */
  Surface(const Font *font, const TextSettings *settings,
          const std::string &text);

  /* Transformations */
  Surface(Surface *parent, int levels, Color *color); // outlined
  Surface(int scaling, Surface *parent);               // scaled
  Surface(Surface *parent, std::vector<Color *> colormap);  // colormapped
  Surface(Surface *parent, Color *color);              // multiply
  Surface(Surface *parent, int type);                  // grayscale (type=0)

  /* Query methods */
  int w() const { return width; }
  int h() const { return height; }
  int leftPadding() const { return padl; }
  int rightPadding() const { return padr; }
  bool isValid() const;
  bool wasDrawn();

  /* GL texture management */
  uint32_t texture();

protected:
  void setBitmap(const uint8_t *data, int sx, int sy, int w, int h, int stride);
  void addOutline(int levels, const Color *color);
};

/**
 * SurfaceScreenshot: captures the current GL framebuffer
 */
struct SurfaceScreenshot : public Surface {
  SurfaceScreenshot();
};

/**
 * Screen: GL rendering context wrapper
 */
struct Screen {
  SDL_Window *window;
  std::vector<Rect> clippingRects;
  std::vector<Rect> maskRects;

  Screen();
  ~Screen() = default;

  int w();
  int h();

  void begin();
  void finishWithoutSwapping();
  void finish();

  void blit(Surface *src, Rect *srcRect, int destx, int desty);
  void blitRect(Surface *src, Rect *srcRect, Rect *destRect, Color *color);
  void drawrect(Color *color, Rect *rect);
  void clip(Rect *rect);
  void unclip();
  void mask(Rect *rect);
  void unmask(size_t count);
  void clearmask();
  Rect *getClipRect();

private:
  void applyClipping();
};

/**
 * Mouse position query
 */
int mousex();
int mousey();

/**
 * Clipboard access
 */
void setClipboardData(const std::string &text);
std::string getClipboardData();

}  // namespace SDL
