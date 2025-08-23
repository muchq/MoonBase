#ifndef CPP_PNG_PLUSPLUS_PNG_PLUSPLUS_H
#define CPP_PNG_PLUSPLUS_PNG_PLUSPLUS_H

#include <png.h>

#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "cpp/image_core/image_core.h"

namespace pngpp {
using image_core::RGB;

class PngException : public std::runtime_error {
 public:
  explicit PngException(const std::string& msg) : std::runtime_error(msg) {}
};

class PngWriter {
 public:
  PngWriter(const std::string& filename, int width, int height)
      : width_(width), height_(height), filename_(filename) {
    file_ = std::fopen(filename.c_str(), "wb");
    if (!file_) {
      throw PngException("Failed to open file: " + filename);
    }

    png_ = png_create_write_struct(PNG_LIBPNG_VER_STRING, this, &PngWriter::errorHandler,
                                   &PngWriter::warningHandler);
    if (!png_) {
      std::fclose(file_);
      throw PngException("Failed to create PNG write struct");
    }

    info_ = png_create_info_struct(png_);
    if (!info_) {
      cleanup();
      throw PngException("Failed to create PNG info struct");
    }

    // Set up error handling with setjmp
    if (setjmp(png_jmpbuf(png_))) {
      cleanup();
      throw PngException("PNG error during initialization");
    }

    png_init_io(png_, file_);

    png_set_IHDR(png_, info_, width, height, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    png_write_info(png_, info_);
  }

  ~PngWriter() { cleanup(); }

  // Delete copy constructor and assignment operator
  PngWriter(const PngWriter&) = delete;
  PngWriter& operator=(const PngWriter&) = delete;

  // Move constructor
  PngWriter(PngWriter&& other) noexcept
      : png_(other.png_),
        info_(other.info_),
        file_(other.file_),
        width_(other.width_),
        height_(other.height_),
        filename_(std::move(other.filename_)) {
    other.png_ = nullptr;
    other.info_ = nullptr;
    other.file_ = nullptr;
  }

  // Move assignment operator
  PngWriter& operator=(PngWriter&& other) noexcept {
    if (this != &other) {
      cleanup();
      png_ = other.png_;
      info_ = other.info_;
      file_ = other.file_;
      width_ = other.width_;
      height_ = other.height_;
      filename_ = std::move(other.filename_);
      other.png_ = nullptr;
      other.info_ = nullptr;
      other.file_ = nullptr;
    }
    return *this;
  }

  void writeImage(const std::vector<std::vector<RGB>>& image) {
    if (image.size() != static_cast<size_t>(height_) ||
        (height_ > 0 && image[0].size() != static_cast<size_t>(width_))) {
      throw PngException("Image dimensions don't match writer dimensions");
    }

    if (setjmp(png_jmpbuf(png_))) {
      throw PngException("PNG error during write");
    }

    std::vector<png_bytep> row_pointers(height_);
    std::vector<png_byte> row_data(width_ * 3 * height_);

    for (int y = 0; y < height_; ++y) {
      row_pointers[y] = &row_data[y * width_ * 3];
      for (int x = 0; x < width_; ++x) {
        row_pointers[y][x * 3 + 0] = image[y][x].r;
        row_pointers[y][x * 3 + 1] = image[y][x].g;
        row_pointers[y][x * 3 + 2] = image[y][x].b;
      }
    }

    png_write_image(png_, row_pointers.data());
    png_write_end(png_, nullptr);
  }

  int getWidth() const { return width_; }
  int getHeight() const { return height_; }
  const std::string& getFilename() const { return filename_; }

 private:
  void cleanup() {
    if (png_ || info_) {
      png_destroy_write_struct(png_ ? &png_ : nullptr, info_ ? &info_ : nullptr);
      png_ = nullptr;
      info_ = nullptr;
    }
    if (file_) {
      std::fclose(file_);
      file_ = nullptr;
    }
  }

  static void errorHandler(png_structp png, png_const_charp msg) {
    (void)png;  // Unused
    throw PngException(std::string("PNG Error: ") + msg);
  }

  static void warningHandler(png_structp png, png_const_charp msg) {
    (void)png;  // Unused
    // Could log warnings here if desired
    (void)msg;
  }

  png_structp png_ = nullptr;
  png_infop info_ = nullptr;
  FILE* file_ = nullptr;
  int width_;
  int height_;
  std::string filename_;
};

// Convenience function for simple image writing
inline void writePng(const std::string& filename, const std::vector<std::vector<RGB>>& image) {
  if (image.empty() || image[0].empty()) {
    throw PngException("Cannot write empty image");
  }

  int height = static_cast<int>(image.size());
  int width = static_cast<int>(image[0].size());

  PngWriter writer(filename, width, height);
  writer.writeImage(image);
}

// Convenience function for reading PNG files
inline std::vector<std::vector<RGB>> readPng(const std::string& filename) {
  FILE* file = std::fopen(filename.c_str(), "rb");
  if (!file) {
    throw PngException("Failed to open file for reading: " + filename);
  }

  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png) {
    std::fclose(file);
    throw PngException("Failed to create PNG read struct");
  }

  png_infop info = png_create_info_struct(png);
  if (!info) {
    png_destroy_read_struct(&png, nullptr, nullptr);
    std::fclose(file);
    throw PngException("Failed to create PNG info struct");
  }

  if (setjmp(png_jmpbuf(png))) {
    png_destroy_read_struct(&png, &info, nullptr);
    std::fclose(file);
    throw PngException("PNG error during read");
  }

  png_init_io(png, file);
  png_read_info(png, info);

  int width = png_get_image_width(png, info);
  int height = png_get_image_height(png, info);
  png_byte color_type = png_get_color_type(png, info);
  png_byte bit_depth = png_get_bit_depth(png, info);

  // Convert to RGB format
  if (color_type == PNG_COLOR_TYPE_PALETTE) {
    png_set_palette_to_rgb(png);
  }
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
    png_set_expand_gray_1_2_4_to_8(png);
  }
  if (png_get_valid(png, info, PNG_INFO_tRNS)) {
    png_set_tRNS_to_alpha(png);
  }
  if (bit_depth == 16) {
    png_set_strip_16(png);
  }
  if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
    png_set_gray_to_rgb(png);
  }
  if (color_type == PNG_COLOR_TYPE_RGB_ALPHA) {
    png_set_strip_alpha(png);
  }

  png_read_update_info(png, info);

  std::vector<png_bytep> row_pointers(height);
  std::vector<png_byte> row_data(height * width * 3);

  for (int y = 0; y < height; ++y) {
    row_pointers[y] = &row_data[y * width * 3];
  }

  png_read_image(png, row_pointers.data());
  png_read_end(png, nullptr);

  png_destroy_read_struct(&png, &info, nullptr);
  std::fclose(file);

  std::vector<std::vector<RGB>> image(height, std::vector<RGB>(width));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      image[y][x] = {row_pointers[y][x * 3 + 0], row_pointers[y][x * 3 + 1],
                     row_pointers[y][x * 3 + 2]};
    }
  }

  return image;
}

}  // namespace pngpp

#endif  // CPP_PNG_PLUSPLUS_PNG_PLUSPLUS_H