#pragma once

#include <cstdint>
#include <cstring>

namespace SDL {

/* Minimal shared Blob interface for in-memory font/image data.
 * Task 4 (text_freetype.cpp) reads fonts from Blob.
 * Task 5 (os_posix.cpp) populates Blob with data from resource.dat.
 *
 * This is a cross-platform, C++ interface without Windows IStream
 * dependencies. Platform-specific subclasses (BlobFromFile,
 * BlobFromResourceDat) add file I/O, defined in Task 5.
 */
class Blob {
public:
  const uint8_t *data;
  size_t length;

  Blob() : data(nullptr), length(0) {}
  Blob(const uint8_t *d, size_t len) : data(d), length(len) {}
  virtual ~Blob() = default;
};

} // namespace SDL
