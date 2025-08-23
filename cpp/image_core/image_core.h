#ifndef CPP_IMAGE_CORE_IMAGE_CORE_H
#define CPP_IMAGE_CORE_IMAGE_CORE_H

#include <concepts>
#include <vector>

namespace image_core {

struct RGB_Double {
  double r, g, b;

  RGB_Double operator+(RGB_Double&& o) {
    return RGB_Double{.r = r + o.r, .g = g + o.g, .b = b + o.b};
  }

  RGB_Double operator*(double x) { return RGB_Double{.r = r * x, .g = g * x, .b = b * x}; }

  bool operator==(const RGB_Double& o) { return r == o.r && g == o.g && b == o.b; }
};

struct RGB {
  unsigned char r, g, b;

  RGB_Double toRGB_Double() const {
    return RGB_Double{
        .r = static_cast<double>(r), .g = static_cast<double>(g), .b = static_cast<double>(b)};
  }
};

inline int clampValue(double v) { return std::min(255, std::max(0, static_cast<int>(v))); }

inline RGB clampColor(const RGB_Double& rgb_double) {
  return RGB{.r = static_cast<unsigned char>(clampValue(rgb_double.r)),
             .g = static_cast<unsigned char>(clampValue(rgb_double.g)),
             .b = static_cast<unsigned char>(clampValue(rgb_double.b))};
}

template <typename T>
concept IsRGB = std::same_as<T, RGB> || std::same_as<T, RGB_Double>;

template <typename T>
  requires IsRGB<T>
struct Image {
  int width;
  int height;
  std::vector<std::vector<T>> data;  // a vector of rows

  explicit Image(int w, int h) : width(w), height(h) { data.resize(height, std::vector<T>(width)); }
  explicit Image(std::vector<std::vector<T>> pixels)
      : width(pixels[0].size()), height(pixels.size()), data(pixels) {}

  void putPixel(int x, int y, const T& color) {
    int row = static_cast<int>(height / 2 - y);
    int col = static_cast<int>(width / 2 + x);

    if (row >= 0 && row < height && col >= 0 && col < width) {
      data[row][col] = color;
    }
  }

  std::vector<std::vector<RGB>> toRGB() const {
    std::vector<std::vector<RGB>> copy;
    copy.resize(height, std::vector<RGB>(width));
    for (int row = 0; row < height; row++) {
      for (int col = 0; col < width; col++) {
        if constexpr (std::same_as<T, RGB>) {
          copy[row][col] = data[row][col];
        } else {
          copy[row][col] = clampColor(data[row][col]);
        }
      }
    }
    return copy;
  }
};
}  // namespace image_core

#endif
