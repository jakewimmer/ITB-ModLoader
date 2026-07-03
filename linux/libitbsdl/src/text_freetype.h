#pragma once

#include <string>
#include <memory>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_STROKER_H
#include <cstdint>

#include "blob.h"
#include "screen_gl.h"

namespace SDL {

/* TextSettings: text rendering parameters (color, antialiasing, outline)
 * Color fields are embedded Color structs that are exposed in Lua,
 * allowing Lua code to assign colors directly: settings.color = sdl.rgba(...)
 */
struct TextSettings {
  Color color;
  bool antialias = true;
  Color outlineColor;
  int outlineWidth = 0;

  TextSettings() : antialias(true), outlineWidth(0) {}
};

/* Font: system or FreeType font loaded by family name via Fontconfig.
 * Provides metrics (ascent/descent) for layout.
 */
class Font {
public:
  Font(const std::string &name, double size);
  virtual ~Font();

  FT_Face getFace() const { return face; }
  double getSize() const { return size; }
  float getAscent() const { return ascent; }
  float getDescent() const { return descent; }

protected:
  FT_Face face = nullptr;
  double size = 0;
  float ascent = 0;
  float descent = 0;

  /* Memory-resident font data (owned by subclasses) */
  std::vector<uint8_t> fontData;

  /* Called by subclasses after loading a face to compute metrics */
  void computeMetrics();
};

/* FileFont: FreeType font loaded from file or memory blob.
 * Inherits from Font and adds ownership of the font file data.
 */
class FileFont : public Font {
public:
  FileFont(const std::string &filename, double size);
  FileFont(const Blob *blob, double size);
  ~FileFont();
};

} // namespace SDL
