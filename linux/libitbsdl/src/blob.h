#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <map>
#include <memory>

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

/* BlobFromFile: load entire file into memory */
class BlobFromFile : public Blob {
private:
  uint8_t *owned_data;

public:
  BlobFromFile(const std::string &filename);
  ~BlobFromFile();
};

/* ResourceDat: archive reader for resource.dat files */
class ResourceDat {
public:
  struct FileInfo {
    size_t offset;
    size_t size;

    FileInfo() : offset(0), size(0) {}
    FileInfo(size_t o, size_t s) : offset(o), size(s) {}
  };

  std::string filename;
  std::map<std::string, FileInfo> index;

  ResourceDat(const std::string &filename);
  void reload();
};

/* BlobFromResourceDat: load entry from resource.dat archive */
class BlobFromResourceDat : public Blob {
private:
  uint8_t *owned_data;

public:
  BlobFromResourceDat(const std::shared_ptr<ResourceDat> &dat,
                      const std::string &entryname);
  ~BlobFromResourceDat();
};

} // namespace SDL
