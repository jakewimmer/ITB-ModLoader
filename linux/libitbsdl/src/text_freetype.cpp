/* FreeType text rendering for libitbsdl.
 *
 * Provides Font, FileFont, TextSettings, and sdl.text() for rasterizing
 * UTF-8 text into RGBA Surfaces. Supports antialiasing, color, outline
 * stroking (FT_Stroker), and left-to-right glyph shaping via FreeType.
 *
 * Differs from the Windows proxy (GDI+): FreeType metrics (ascent/descent,
 * glyph advances) are used directly. Font parity is verified in Task 6.
 *
 * UTF-8 decoding: valid codepoints up to U+10FFFF; invalid sequences are
 * skipped with a warning.
 */

#include "text_freetype.h"
#include "screen_gl.h"
#include <fontconfig/fontconfig.h>
#include <cstring>
#include <cmath>
#include <cassert>
#include <cstdio>

namespace SDL {

/* Global FreeType library handle */
static FT_Library g_ft_library = nullptr;

static FT_Library getFreeTypeLibrary() {
  if (g_ft_library == nullptr) {
    FT_Error err = FT_Init_FreeType(&g_ft_library);
    if (err) {
      fprintf(stderr, "FreeType init failed: %d\n", err);
      return nullptr;
    }
  }
  return g_ft_library;
}

/* UTF-8 decoding: returns the Unicode codepoint and advances the pointer.
 * Invalid sequences are skipped (pointer advances 1 byte) with a warning.
 * Returns -1 on end of string or error.
 */
static int decodeUTF8Char(const char *&p, const char *end) {
  if (p >= end)
    return -1;

  unsigned char c = (unsigned char)*p;

  if (c < 0x80) {
    /* Single-byte ASCII: 0xxxxxxx */
    ++p;
    return c;
  } else if ((c & 0xE0) == 0xC0 && p + 1 < end) {
    /* Two-byte: 110xxxxx 10xxxxxx */
    unsigned char b1 = (unsigned char)p[1];
    if ((b1 & 0xC0) != 0x80) {
      fprintf(stderr, "Invalid UTF-8 sequence\n");
      ++p;
      return -2;
    }
    int cp = ((c & 0x1F) << 6) | (b1 & 0x3F);
    p += 2;
    if (cp < 0x80) {
      fprintf(stderr, "Overlong UTF-8 sequence\n");
      return -2;
    }
    return cp;
  } else if ((c & 0xF0) == 0xE0 && p + 2 < end) {
    /* Three-byte: 1110xxxx 10xxxxxx 10xxxxxx */
    unsigned char b1 = (unsigned char)p[1];
    unsigned char b2 = (unsigned char)p[2];
    if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) {
      fprintf(stderr, "Invalid UTF-8 sequence\n");
      ++p;
      return -2;
    }
    int cp = ((c & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
    p += 3;
    if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) {
      fprintf(stderr, "Invalid UTF-8 codepoint\n");
      return -2;
    }
    return cp;
  } else if ((c & 0xF8) == 0xF0 && p + 3 < end) {
    /* Four-byte: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
    unsigned char b1 = (unsigned char)p[1];
    unsigned char b2 = (unsigned char)p[2];
    unsigned char b3 = (unsigned char)p[3];
    if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) {
      fprintf(stderr, "Invalid UTF-8 sequence\n");
      ++p;
      return -2;
    }
    int cp = ((c & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) |
             (b3 & 0x3F);
    p += 4;
    if (cp < 0x10000 || cp > 0x10FFFF) {
      fprintf(stderr, "Invalid UTF-8 codepoint\n");
      return -2;
    }
    return cp;
  } else {
    fprintf(stderr, "Invalid UTF-8 byte\n");
    ++p;
    return -2;
  }
}

/* Resolve a font family name to a file path using Fontconfig.
 * Returns true and fills `path` on success; false on error.
 */
static bool resolveFontFile(const std::string &family, std::string &path) {
  if (!FcInit()) {
    fprintf(stderr, "Fontconfig init failed\n");
    return false;
  }

  FcPattern *pattern = FcNameParse((const FcChar8 *)family.c_str());
  if (!pattern) {
    fprintf(stderr, "Failed to parse Fontconfig pattern\n");
    return false;
  }

  FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
  FcDefaultSubstitute(pattern);

  FcResult result = FcResultNoMatch;
  FcPattern *match = FcFontMatch(nullptr, pattern, &result);
  FcPatternDestroy(pattern);

  if (!match || result != FcResultMatch) {
    fprintf(stderr, "Fontconfig match failed for '%s'\n", family.c_str());
    return false;
  }

  FcChar8 *file_path = nullptr;
  FcResult result_file = FcPatternGetString(match, FC_FILE, 0, &file_path);
  FcPatternDestroy(match);

  if (result_file != FcResultMatch || !file_path) {
    fprintf(stderr, "Fontconfig: no file path for '%s'\n", family.c_str());
    return false;
  }

  path = (const char *)file_path;
  return true;
}

/* Font constructor: resolve system font by name and load with FreeType */
Font::Font(const std::string &name, double size_pt) : size(size_pt) {
  FT_Library lib = getFreeTypeLibrary();
  if (!lib)
    return;

  std::string filepath;
  if (!resolveFontFile(name, filepath)) {
    fprintf(stderr, "Failed to resolve font '%s', using default\n", name.c_str());
    return;
  }

  FT_Error err = FT_New_Face(lib, filepath.c_str(), 0, &face);
  if (err) {
    fprintf(stderr, "FT_New_Face failed for '%s': %d\n", filepath.c_str(), err);
    return;
  }

  computeMetrics();
}

/* Load FreeType font from a file path */
FileFont::FileFont(const std::string &filename, double size_pt) : Font("", size_pt) {
  FT_Library lib = getFreeTypeLibrary();
  if (!lib)
    return;

  FT_Error err = FT_New_Face(lib, filename.c_str(), 0, &face);
  if (err) {
    fprintf(stderr, "FT_New_Face failed for '%s': %d\n", filename.c_str(), err);
    return;
  }

  computeMetrics();
}

/* Load FreeType font from a memory blob */
FileFont::FileFont(const Blob *blob, double size_pt) : Font("", size_pt) {
  if (!blob || !blob->data || blob->length == 0) {
    fprintf(stderr, "Invalid blob for FileFont\n");
    return;
  }

  FT_Library lib = getFreeTypeLibrary();
  if (!lib)
    return;

  /* Copy blob data into our owned vector */
  fontData.assign(blob->data, blob->data + blob->length);

  FT_Error err = FT_New_Memory_Face(lib, fontData.data(), fontData.size(), 0,
                                     &face);
  if (err) {
    fprintf(stderr, "FT_New_Memory_Face failed: %d\n", err);
    fontData.clear();
    return;
  }

  computeMetrics();
}

/* Compute ascent/descent from the loaded face */
void Font::computeMetrics() {
  if (!face)
    return;

  /* Set character size in points (26.6 fixed-point format: size * 64) */
  FT_Set_Char_Size(face, (FT_F26Dot6)(size * 64), 0, 72, 72);

  /* Use FreeType metrics. Note: FreeType reports ascender/descender in
   * font units; convert to pixels by dividing by units_per_EM.
   */
  int units_per_em = face->units_per_EM;
  if (units_per_em == 0)
    units_per_em = 1000;

  /* ascent: distance from baseline to top of tallest glyph */
  ascent = (float)face->ascender / units_per_em * size;

  /* descent: distance from baseline to bottom (negative value in FreeType) */
  descent = -(float)face->descender / units_per_em * size;
}

/* Destructor: clean up FreeType face if we own it */
Font::~Font() {
  if (face) {
    FT_Done_Face(face);
    face = nullptr;
  }
}

FileFont::~FileFont() {
  /* ~Font() handles FT_Done_Face */
  fontData.clear();
}

} // namespace SDL

/* Surface constructor for text rendering.
 * Rasterizes UTF-8 text into an RGBA Surface using FreeType.
 * Supports antialiasing, color, and outline stroking.
 * Declared in screen_gl.h; implemented here to avoid circular includes.
 */
SDL::Surface::Surface(const SDL::Font *font, const SDL::TextSettings *settings,
                      const std::string &text) {
  init();

  if (!font || !font->getFace()) {
    fprintf(stderr, "Invalid font for text surface\n");
    return;
  }

  FT_Face face = font->getFace();

  /* Use provided settings or create default (white text, antialiased, no outline) */
  SDL::TextSettings default_settings;
  default_settings.color = SDL::Color(255, 255, 255);
  default_settings.antialias = true;
  default_settings.outlineWidth = 0;

  const SDL::TextSettings *s = settings ? settings : &default_settings;

  int outline_width = s->outlineWidth;
  bool antialias = s->antialias && outline_width == 0;

  /* Determine glyph rendering mode */
  int load_flags = FT_LOAD_RENDER;
  FT_Render_Mode render_mode =
      antialias ? FT_RENDER_MODE_NORMAL : FT_RENDER_MODE_MONO;

  /* First pass: measure the text to determine buffer size */
  int pen_x = 0;
  int pen_y = 0;
  int max_width = 0;
  int max_height = 0;

  const char *str = text.c_str();
  const char *end = str + text.length();

  /* Outline stroker (used if outline_width > 0) */
  FT_Stroker stroker = nullptr;
  if (outline_width > 0) {
    FT_Library lib = getFreeTypeLibrary();
    if (!lib) {
      fprintf(stderr, "FreeType library not available for stroker\n");
      outline_width = 0;
    } else {
      FT_Error err = FT_Stroker_New(lib, &stroker);
      if (err) {
        fprintf(stderr, "FT_Stroker_New failed: %d\n", err);
        outline_width = 0;
      } else {
        /* Stroker radius in 26.6 fixed-point: outline_width * 64 */
        FT_Stroker_Set(stroker, outline_width * 64, FT_STROKER_LINECAP_ROUND,
                       FT_STROKER_LINEJOIN_ROUND, 0);
      }
    }
  }

  /* Shape and measure glyphs */
  int baseline = (int)std::ceil(font->getAscent());
  int height_total = baseline + (int)std::ceil(font->getDescent());

  for (const char *p = str; p < end;) {
    int codepoint = decodeUTF8Char(p, end);
    if (codepoint < 0)
      continue; /* Skip invalid sequences */

    uint32_t glyph_idx = FT_Get_Char_Index(face, codepoint);
    if (glyph_idx == 0) {
      /* Glyph not found; skip */
      continue;
    }

    FT_Error err = FT_Load_Glyph(face, glyph_idx, load_flags);
    if (err) {
      fprintf(stderr, "FT_Load_Glyph failed for U+%04X: %d\n", codepoint, err);
      continue;
    }

    FT_Glyph glyph = nullptr;
    if (outline_width > 0) {
      err = FT_Get_Glyph(face->glyph, &glyph);
      if (err) {
        fprintf(stderr, "FT_Get_Glyph failed: %d\n", err);
        continue;
      }

      FT_Glyph_StrokeBorder(&glyph, stroker, 0, 1);
      FT_Glyph_To_Bitmap(&glyph, render_mode, nullptr, 1);
    }

    /* Advance to next character position */
    FT_Vector advance = face->glyph->advance;
    pen_x += advance.x >> 6; /* Convert from 26.6 to pixels */
  }

  max_width = (pen_x > 0) ? pen_x : 1;
  max_height = (height_total > 0) ? height_total : 1;

  if (outline_width > 0) {
    max_width += outline_width * 2;
    max_height += outline_width * 2;
  }

  /* Allocate RGBA buffer */
  width = max_width;
  height = max_height;
  pixelData = new unsigned char[width * height * 4];
  memset(pixelData, 0, width * height * 4);

  /* Second pass: render glyphs into the buffer */
  pen_x = outline_width;
  pen_y = baseline + outline_width;

  for (const char *p = str; p < end;) {
    int codepoint = decodeUTF8Char(p, end);
    if (codepoint < 0)
      continue;

    uint32_t glyph_idx = FT_Get_Char_Index(face, codepoint);
    if (glyph_idx == 0)
      continue;

    FT_Error err = FT_Load_Glyph(face, glyph_idx, load_flags);
    if (err)
      continue;

    FT_Bitmap *bmp = &face->glyph->bitmap;

    /* Render outline if requested */
    if (outline_width > 0 && stroker) {
      FT_Glyph glyph = nullptr;
      if (FT_Get_Glyph(face->glyph, &glyph) == 0) {
        FT_Glyph outline_glyph = glyph;
        FT_Glyph_StrokeBorder(&outline_glyph, stroker, 0, 1);
        FT_Glyph_To_Bitmap(&outline_glyph, render_mode, nullptr, 1);

        FT_BitmapGlyph bmp_glyph = (FT_BitmapGlyph)outline_glyph;
        int x = pen_x + bmp_glyph->left;
        int y = pen_y - bmp_glyph->top;

        /* Blend outline color (from TextSettings.outlineColor) */
        uint32_t outline_color = ((uint32_t)s->outlineColor.a << 24) |
                                 ((uint32_t)s->outlineColor.b << 16) |
                                 ((uint32_t)s->outlineColor.g << 8) |
                                 (uint32_t)s->outlineColor.r;

        for (int gy = 0; gy < (int)bmp_glyph->bitmap.rows; ++gy) {
          for (int gx = 0; gx < (int)bmp_glyph->bitmap.width; ++gx) {
            int py = y + gy;
            int px = x + gx;

            if (px < 0 || px >= width || py < 0 || py >= height)
              continue;

            unsigned char alpha = bmp_glyph->bitmap.buffer[gy * bmp_glyph->bitmap.pitch + gx];
            if (alpha == 0)
              continue;

            uint32_t *dst = (uint32_t *)(pixelData + (py * width + px) * 4);
            uint32_t dst_color = *dst;

            /* Simple alpha blend */
            float a = alpha / 255.0f;
            float inv_a = 1.0f - a;

            uint8_t r = (uint8_t)(((outline_color & 0xFF) * a +
                                    (dst_color & 0xFF) * inv_a));
            uint8_t g =
                (uint8_t)((((outline_color >> 8) & 0xFF) * a +
                           ((dst_color >> 8) & 0xFF) * inv_a));
            uint8_t b =
                (uint8_t)((((outline_color >> 16) & 0xFF) * a +
                           ((dst_color >> 16) & 0xFF) * inv_a));
            uint8_t a_dst = (uint8_t)(((outline_color >> 24) & 0xFF) * a +
                                      ((dst_color >> 24) & 0xFF) * inv_a);

            *dst = (a_dst << 24) | (b << 16) | (g << 8) | r;
          }
        }

        FT_Done_Glyph(outline_glyph);
      }
    }

    /* Render glyph foreground (from TextSettings.color) */
    uint32_t color = ((uint32_t)s->color.a << 24) | ((uint32_t)s->color.b << 16) |
                     ((uint32_t)s->color.g << 8) | (uint32_t)s->color.r;

    int glyph_x = pen_x + face->glyph->bitmap_left;
    int glyph_y = pen_y - face->glyph->bitmap_top;

    for (int gy = 0; gy < (int)bmp->rows; ++gy) {
      for (int gx = 0; gx < (int)bmp->width; ++gx) {
        int py = glyph_y + gy;
        int px = glyph_x + gx;

        if (px < 0 || px >= width || py < 0 || py >= height)
          continue;

        unsigned char alpha = bmp->buffer[gy * bmp->pitch + gx];
        if (alpha == 0)
          continue;

        uint32_t *dst = (uint32_t *)(pixelData + (py * width + px) * 4);
        uint32_t dst_color = *dst;

        /* Alpha blend */
        float a = alpha / 255.0f;
        float inv_a = 1.0f - a;

        uint8_t r = (uint8_t)(((color & 0xFF) * a + (dst_color & 0xFF) * inv_a));
        uint8_t g = (uint8_t)((((color >> 8) & 0xFF) * a +
                               ((dst_color >> 8) & 0xFF) * inv_a));
        uint8_t b = (uint8_t)((((color >> 16) & 0xFF) * a +
                               ((dst_color >> 16) & 0xFF) * inv_a));
        uint8_t a_dst =
            (uint8_t)(((color >> 24) & 0xFF) * a + ((dst_color >> 24) & 0xFF) * inv_a);

        *dst = (a_dst << 24) | (b << 16) | (g << 8) | r;
      }
    }

    /* Advance pen */
    FT_Vector advance = face->glyph->advance;
    pen_x += advance.x >> 6;
  }

  if (stroker) {
    FT_Stroker_Done(stroker);
  }

  /* Upload to GL texture */
  createSurfaceFromPixelData(width, height);
}
