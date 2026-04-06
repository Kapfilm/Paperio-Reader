#pragma once
#include <HalStorage.h>

#include <memory>
#include <string>

class GfxRenderer;

struct ImageDimensions {
  int16_t width;
  int16_t height;
};

enum class ImageDitherMode : uint8_t {
  Bayer = 0,
  Atkinson = 1,
  DiffusedBayer = 2,
  COUNT,
};

inline ImageDitherMode imageDitherModeFromSetting(uint8_t value) {
  switch (static_cast<ImageDitherMode>(value)) {
    case ImageDitherMode::Bayer:
    case ImageDitherMode::Atkinson:
    case ImageDitherMode::DiffusedBayer:
      return static_cast<ImageDitherMode>(value);
    case ImageDitherMode::COUNT:
    default:
      return ImageDitherMode::Bayer;
  }
}

inline const char* getImageDitherCacheSuffix(ImageDitherMode mode) {
  switch (mode) {
    case ImageDitherMode::Atkinson:
      return ".atkinson";
    case ImageDitherMode::DiffusedBayer:
      return ".diffused-bayer";
    case ImageDitherMode::Bayer:
    case ImageDitherMode::COUNT:
    default:
      return ".bayer";
  }
}

struct RenderConfig {
  int x, y;
  int maxWidth, maxHeight;
  bool useGrayscale = true;
  bool useDithering = true;
  ImageDitherMode ditherMode = ImageDitherMode::Bayer;
  bool performanceMode = false;
  bool useExactDimensions = false;  // If true, use maxWidth/maxHeight as exact output size (no recalculation)
  std::string cachePath;            // If non-empty, decoder will write pixel cache to this path
};

class ImageToFramebufferDecoder {
 public:
  virtual ~ImageToFramebufferDecoder() = default;

  virtual bool decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer, const RenderConfig& config) = 0;

  virtual bool getDimensions(const std::string& imagePath, ImageDimensions& dims) const = 0;

  virtual const char* getFormatName() const = 0;

 protected:
  // Size validation helpers
  static constexpr int MAX_SOURCE_PIXELS = 3145728;  // 2048 * 1536

  bool validateImageDimensions(int width, int height, const std::string& format);
  void warnUnsupportedFeature(const std::string& feature, const std::string& imagePath);
};
