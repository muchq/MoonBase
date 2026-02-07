#include "domains/graphics/libs/png_plusplus/png_plusplus.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace pngpp {
namespace {

class PngPlusPlusTest : public ::testing::Test {
 protected:
  void SetUp() override { test_filename_ = "test_output.png"; }

  void TearDown() override {
    // Clean up test files
    std::remove(test_filename_.c_str());
  }

  bool fileExists(const std::string& filename) {
    std::ifstream f(filename);
    return f.good();
  }

  std::string test_filename_;
};

TEST_F(PngPlusPlusTest, BasicWriterCreation) {
  EXPECT_NO_THROW({ PngWriter writer(test_filename_, 100, 100); });

  // File should be created even without writing image data
  EXPECT_TRUE(fileExists(test_filename_));
}

TEST_F(PngPlusPlusTest, InvalidFilename) {
  EXPECT_THROW(
      { PngWriter writer("/invalid/path/that/does/not/exist/test.png", 100, 100); }, PngException);
}

TEST_F(PngPlusPlusTest, WriteSimpleImage) {
  const int width = 10;
  const int height = 10;

  std::vector<std::vector<RGB>> image(height, std::vector<RGB>(width));

  // Create a simple red image
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      image[y][x] = {255, 0, 0};
    }
  }

  EXPECT_NO_THROW({
    PngWriter writer(test_filename_, width, height);
    writer.writeImage(image);
  });

  EXPECT_TRUE(fileExists(test_filename_));
}

TEST_F(PngPlusPlusTest, WriteMismatchedDimensions) {
  const int width = 10;
  const int height = 10;

  PngWriter writer(test_filename_, width, height);

  // Create image with wrong dimensions
  std::vector<std::vector<RGB>> wrong_image(5, std::vector<RGB>(5));

  EXPECT_THROW({ writer.writeImage(wrong_image); }, PngException);
}

TEST_F(PngPlusPlusTest, WriteAndReadBack) {
  const int width = 20;
  const int height = 20;

  // Create a gradient image
  std::vector<std::vector<RGB>> original_image(height, std::vector<RGB>(width));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      unsigned char r = static_cast<unsigned char>((x * 255) / width);
      unsigned char g = static_cast<unsigned char>((y * 255) / height);
      unsigned char b = 128;
      original_image[y][x] = RGB{r, g, b};
    }
  }

  EXPECT_NO_THROW({
    PngWriter writer(test_filename_, width, height);
    writer.writeImage(original_image);
  });

  EXPECT_TRUE(fileExists(test_filename_));

  // Read back and verify
  std::vector<std::vector<RGB>> read_image;
  EXPECT_NO_THROW({ read_image = readPng(test_filename_); });

  EXPECT_EQ(read_image.size(), height);
  EXPECT_EQ(read_image[0].size(), width);

  // Verify pixel values
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      EXPECT_EQ(read_image[y][x].r, original_image[y][x].r);
      EXPECT_EQ(read_image[y][x].g, original_image[y][x].g);
      EXPECT_EQ(read_image[y][x].b, original_image[y][x].b);
    }
  }
}

TEST_F(PngPlusPlusTest, MoveSemantics) {
  const int width = 10;
  const int height = 10;

  PngWriter writer1(test_filename_, width, height);

  // Test move constructor
  PngWriter writer2(std::move(writer1));
  EXPECT_EQ(writer2.getWidth(), width);
  EXPECT_EQ(writer2.getHeight(), height);
  EXPECT_EQ(writer2.getFilename(), test_filename_);

  // Test move assignment
  std::string other_file = "test_other.png";
  PngWriter writer3(other_file, 5, 5);
  writer3 = std::move(writer2);
  EXPECT_EQ(writer3.getWidth(), width);
  EXPECT_EQ(writer3.getHeight(), height);
  EXPECT_EQ(writer3.getFilename(), test_filename_);

  // Clean up
  std::remove(other_file.c_str());
}

TEST_F(PngPlusPlusTest, ConvenienceFunction) {
  const int width = 15;
  const int height = 15;

  std::vector<std::vector<RGB>> image(height, std::vector<RGB>(width));

  // Create a checkerboard pattern
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      bool isWhite = ((x / 5) + (y / 5)) % 2 == 0;
      unsigned char val = isWhite ? 255 : 0;
      image[y][x] = {val, val, val};
    }
  }

  EXPECT_NO_THROW({ writePng(test_filename_, image); });

  EXPECT_TRUE(fileExists(test_filename_));
}

TEST_F(PngPlusPlusTest, EmptyImageThrows) {
  std::vector<std::vector<RGB>> empty_image;

  EXPECT_THROW({ writePng(test_filename_, empty_image); }, PngException);
}

TEST_F(PngPlusPlusTest, ReadNonExistentFile) {
  EXPECT_THROW({ readPng("non_existent_file.png"); }, PngException);
}

TEST_F(PngPlusPlusTest, LargerImageWithReadback) {
  const int width = 200;
  const int height = 150;

  // Create a more complex pattern
  std::vector<std::vector<RGB>> original_image(height, std::vector<RGB>(width));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      // Create concentric circles pattern
      int cx = width / 2;
      int cy = height / 2;
      int dist_sq = (x - cx) * (x - cx) + (y - cy) * (y - cy);
      int max_dist_sq = cx * cx + cy * cy;

      unsigned char intensity =
          static_cast<unsigned char>(255.0 * (1.0 - static_cast<double>(dist_sq) / max_dist_sq));

      original_image[y][x] = RGB{intensity, intensity, static_cast<unsigned char>(255 - intensity)};
    }
  }

  EXPECT_NO_THROW({
    PngWriter writer(test_filename_, width, height);
    writer.writeImage(original_image);
  });

  EXPECT_TRUE(fileExists(test_filename_));

  // Read back and verify dimensions
  std::vector<std::vector<RGB>> read_image;
  EXPECT_NO_THROW({ read_image = readPng(test_filename_); });

  EXPECT_EQ(read_image.size(), height);
  EXPECT_EQ(read_image[0].size(), width);

  // Verify a few sample pixels
  int cx = width / 2;
  int cy = height / 2;
  EXPECT_EQ(read_image[cy][cx].r, original_image[cy][cx].r);  // Center pixel
  EXPECT_EQ(read_image[0][0].r, original_image[0][0].r);      // Corner pixel
}

TEST_F(PngPlusPlusTest, MultipleWritersWithReadback) {
  std::string file1 = "test_file1.png";
  std::string file2 = "test_file2.png";

  // Create red image
  std::vector<std::vector<RGB>> red_image(50, std::vector<RGB>(50, RGB{255, 0, 0}));

  // Create green image
  std::vector<std::vector<RGB>> green_image(60, std::vector<RGB>(60, RGB{0, 255, 0}));

  EXPECT_NO_THROW({
    PngWriter writer1(file1, 50, 50);
    PngWriter writer2(file2, 60, 60);

    writer1.writeImage(red_image);
    writer2.writeImage(green_image);
  });

  EXPECT_TRUE(fileExists(file1));
  EXPECT_TRUE(fileExists(file2));

  // Read back and verify
  std::vector<std::vector<RGB>> read_red, read_green;
  EXPECT_NO_THROW({
    read_red = readPng(file1);
    read_green = readPng(file2);
  });

  EXPECT_EQ(read_red.size(), 50);
  EXPECT_EQ(read_red[0].size(), 50);
  EXPECT_EQ(read_red[25][25].r, 255);
  EXPECT_EQ(read_red[25][25].g, 0);
  EXPECT_EQ(read_red[25][25].b, 0);

  EXPECT_EQ(read_green.size(), 60);
  EXPECT_EQ(read_green[0].size(), 60);
  EXPECT_EQ(read_green[30][30].r, 0);
  EXPECT_EQ(read_green[30][30].g, 255);
  EXPECT_EQ(read_green[30][30].b, 0);

  // Clean up
  std::remove(file1.c_str());
  std::remove(file2.c_str());
}

TEST_F(PngPlusPlusTest, ReadModifyWriteWorkflow) {
  const int width = 100;
  const int height = 100;
  std::string modified_filename = "modified_test.png";

  // Step 1: Create and write a red square image
  std::vector<std::vector<RGB>> red_image(height, std::vector<RGB>(width, RGB{255, 0, 0}));

  EXPECT_NO_THROW({
    PngWriter writer(test_filename_, width, height);
    writer.writeImage(red_image);
  });

  // Step 2: Read the image back
  std::vector<std::vector<RGB>> read_image;
  EXPECT_NO_THROW({ read_image = readPng(test_filename_); });

  // Verify we read back the red image correctly
  EXPECT_EQ(read_image.size(), height);
  EXPECT_EQ(read_image[0].size(), width);
  EXPECT_EQ(read_image[50][50].r, 255);
  EXPECT_EQ(read_image[50][50].g, 0);
  EXPECT_EQ(read_image[50][50].b, 0);

  // Step 3: Modify the pixels - add a blue square in the center (25x25 square centered at 50,50)
  int square_size = 25;
  int start_x = width / 2 - square_size / 2;
  int start_y = height / 2 - square_size / 2;
  int end_x = start_x + square_size;
  int end_y = start_y + square_size;

  for (int y = start_y; y < end_y; ++y) {
    for (int x = start_x; x < end_x; ++x) {
      read_image[y][x] = RGB{0, 0, 255};  // Blue square
    }
  }

  // Step 4: Write the modified image
  EXPECT_NO_THROW({
    PngWriter writer(modified_filename, width, height);
    writer.writeImage(read_image);
  });

  // Step 5: Read the modified image back and verify
  std::vector<std::vector<RGB>> final_image;
  EXPECT_NO_THROW({ final_image = readPng(modified_filename); });

  EXPECT_EQ(final_image.size(), height);
  EXPECT_EQ(final_image[0].size(), width);

  // Verify the blue square is there
  EXPECT_EQ(final_image[50][50].r, 0);  // Center should be blue
  EXPECT_EQ(final_image[50][50].g, 0);
  EXPECT_EQ(final_image[50][50].b, 255);

  // Verify corners of blue square
  EXPECT_EQ(final_image[start_y][start_x].b, 255);      // Top-left of blue square
  EXPECT_EQ(final_image[end_y - 1][end_x - 1].b, 255);  // Bottom-right of blue square

  // Verify areas outside the blue square are still red
  EXPECT_EQ(final_image[10][10].r, 255);  // Top-left corner should still be red
  EXPECT_EQ(final_image[10][10].g, 0);
  EXPECT_EQ(final_image[10][10].b, 0);

  EXPECT_EQ(final_image[90][90].r, 255);  // Bottom-right corner should still be red
  EXPECT_EQ(final_image[90][90].g, 0);
  EXPECT_EQ(final_image[90][90].b, 0);

  // Clean up
  std::remove(modified_filename.c_str());
}

// Tests for Image<RGB_Double> roundtrip functionality
TEST_F(PngPlusPlusTest, ImageRgbDoubleToMemoryPngRoundtrip) {
  const int width = 50;
  const int height = 50;

  // Create an Image<RGB_Double> with various values including fractional components
  image_core::Image<image_core::RGB_Double> original_image(width, height);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      // Create a gradient pattern with fractional values
      double r = (static_cast<double>(x) / width) * 255.0;
      double g = (static_cast<double>(y) / height) * 255.0;
      double b = ((static_cast<double>(x + y) / (width + height)) * 127.5) + 63.75;
      original_image.data[y][x] = image_core::RGB_Double{r, g, b};
    }
  }

  // Convert to PNG buffer
  std::vector<unsigned char> png_buffer;
  EXPECT_NO_THROW({ png_buffer = imageToPng(original_image); });
  EXPECT_GT(png_buffer.size(), 0);

  // Convert back to Image<RGB_Double>
  image_core::Image<image_core::RGB_Double> roundtrip_image(1, 1);  // Dummy initialization
  EXPECT_NO_THROW({ roundtrip_image = pngToImage(png_buffer); });

  // Verify dimensions
  EXPECT_EQ(roundtrip_image.width, width);
  EXPECT_EQ(roundtrip_image.height, height);

  // Verify pixel values (note: due to quantization, values should be within 1.0 of original)
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      // Original values
      double orig_r = original_image.data[y][x].r;
      double orig_g = original_image.data[y][x].g;
      double orig_b = original_image.data[y][x].b;

      // Roundtrip values
      double round_r = roundtrip_image.data[y][x].r;
      double round_g = roundtrip_image.data[y][x].g;
      double round_b = roundtrip_image.data[y][x].b;

      // Check that clamped values match (RGB quantizes to [0,255] range)
      int expected_r = std::min(255, std::max(0, static_cast<int>(orig_r)));
      int expected_g = std::min(255, std::max(0, static_cast<int>(orig_g)));
      int expected_b = std::min(255, std::max(0, static_cast<int>(orig_b)));

      EXPECT_EQ(static_cast<int>(round_r), expected_r)
          << "Red mismatch at (" << x << "," << y << ")";
      EXPECT_EQ(static_cast<int>(round_g), expected_g)
          << "Green mismatch at (" << x << "," << y << ")";
      EXPECT_EQ(static_cast<int>(round_b), expected_b)
          << "Blue mismatch at (" << x << "," << y << ")";
    }
  }
}

TEST_F(PngPlusPlusTest, ImageRgbDoubleEdgeCases) {
  const int width = 20;
  const int height = 20;

  // Test with extreme values (negative, zero, very large)
  image_core::Image<image_core::RGB_Double> edge_image(width, height);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (x < width / 4) {
        // Negative values (should clamp to 0)
        edge_image.data[y][x] = image_core::RGB_Double{-50.0, -100.0, -25.0};
      } else if (x < width / 2) {
        // Zero values
        edge_image.data[y][x] = image_core::RGB_Double{0.0, 0.0, 0.0};
      } else if (x < 3 * width / 4) {
        // Values above 255 (should clamp to 255)
        edge_image.data[y][x] = image_core::RGB_Double{300.0, 500.0, 1000.0};
      } else {
        // Valid values
        edge_image.data[y][x] = image_core::RGB_Double{128.5, 64.7, 192.3};
      }
    }
  }

  // Convert to PNG and back
  std::vector<unsigned char> png_buffer = imageToPng(edge_image);
  image_core::Image<image_core::RGB_Double> result_image = pngToImage(png_buffer);

  EXPECT_EQ(result_image.width, width);
  EXPECT_EQ(result_image.height, height);

  // Verify clamping behavior
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      double r = result_image.data[y][x].r;
      double g = result_image.data[y][x].g;
      double b = result_image.data[y][x].b;

      if (x < width / 4) {
        // Negative values should become 0
        EXPECT_EQ(static_cast<int>(r), 0);
        EXPECT_EQ(static_cast<int>(g), 0);
        EXPECT_EQ(static_cast<int>(b), 0);
      } else if (x < width / 2) {
        // Zero values remain zero
        EXPECT_EQ(static_cast<int>(r), 0);
        EXPECT_EQ(static_cast<int>(g), 0);
        EXPECT_EQ(static_cast<int>(b), 0);
      } else if (x < 3 * width / 4) {
        // Large values should clamp to 255
        EXPECT_EQ(static_cast<int>(r), 255);
        EXPECT_EQ(static_cast<int>(g), 255);
        EXPECT_EQ(static_cast<int>(b), 255);
      } else {
        // Valid values should be truncated properly
        EXPECT_EQ(static_cast<int>(r), 128);  // 128.5 -> 128
        EXPECT_EQ(static_cast<int>(g), 64);   // 64.7 -> 64
        EXPECT_EQ(static_cast<int>(b), 192);  // 192.3 -> 192
      }
    }
  }
}

TEST_F(PngPlusPlusTest, MemoryPngWriterBasicFunctionality) {
  const int width = 30;
  const int height = 25;

  std::vector<std::vector<RGB>> test_image(height, std::vector<RGB>(width));

  // Create a diagonal pattern
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if ((x + y) % 2 == 0) {
        test_image[y][x] = RGB{255, 0, 0};  // Red
      } else {
        test_image[y][x] = RGB{0, 0, 255};  // Blue
      }
    }
  }

  MemoryPngWriter writer(width, height);
  EXPECT_NO_THROW({ writer.writeImage(test_image); });

  const std::vector<unsigned char>& buffer = writer.getBuffer();
  EXPECT_GT(buffer.size(), 0);
  EXPECT_EQ(writer.getWidth(), width);
  EXPECT_EQ(writer.getHeight(), height);

  // Verify PNG signature (first 8 bytes should be PNG signature)
  EXPECT_GE(buffer.size(), 8);
  const unsigned char png_signature[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(buffer[i], png_signature[i]) << "PNG signature mismatch at byte " << i;
  }
}

TEST_F(PngPlusPlusTest, MemoryPngReaderBasicFunctionality) {
  const int width = 15;
  const int height = 10;

  // Create test image
  std::vector<std::vector<RGB>> original_image(height, std::vector<RGB>(width));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      unsigned char r = static_cast<unsigned char>((x * 255) / width);
      unsigned char g = static_cast<unsigned char>((y * 255) / height);
      unsigned char b = 128;
      original_image[y][x] = RGB{r, g, b};
    }
  }

  // Write to memory buffer
  MemoryPngWriter writer(width, height);
  writer.writeImage(original_image);
  const std::vector<unsigned char>& png_buffer = writer.getBuffer();

  // Read back from memory buffer
  MemoryPngReader reader(png_buffer);
  std::vector<std::vector<RGB>> read_image;
  EXPECT_NO_THROW({ read_image = reader.readImage(); });

  // Verify dimensions and content
  EXPECT_EQ(read_image.size(), height);
  EXPECT_EQ(read_image[0].size(), width);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      EXPECT_EQ(read_image[y][x].r, original_image[y][x].r);
      EXPECT_EQ(read_image[y][x].g, original_image[y][x].g);
      EXPECT_EQ(read_image[y][x].b, original_image[y][x].b);
    }
  }
}

TEST_F(PngPlusPlusTest, MultipleRoundtripsPreserveData) {
  const int width = 40;
  const int height = 30;

  // Create initial image with specific pattern
  image_core::Image<image_core::RGB_Double> image1(width, height);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      double intensity = static_cast<double>((x + y) % 256);
      image1.data[y][x] = image_core::RGB_Double{intensity, intensity, intensity};
    }
  }

  // Perform multiple roundtrips
  image_core::Image<image_core::RGB_Double> current_image = image1;

  for (int i = 0; i < 3; ++i) {
    std::vector<unsigned char> png_buffer = imageToPng(current_image);
    current_image = pngToImage(png_buffer);

    // Verify dimensions haven't changed
    EXPECT_EQ(current_image.width, width);
    EXPECT_EQ(current_image.height, height);
  }

  // After multiple roundtrips, values should be stable (since they're already quantized)
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      int expected = (x + y) % 256;
      int actual_r = static_cast<int>(current_image.data[y][x].r);
      int actual_g = static_cast<int>(current_image.data[y][x].g);
      int actual_b = static_cast<int>(current_image.data[y][x].b);

      EXPECT_EQ(actual_r, expected);
      EXPECT_EQ(actual_g, expected);
      EXPECT_EQ(actual_b, expected);
    }
  }
}

TEST_F(PngPlusPlusTest, EmptyImageHandling) {
  // Test zero-sized image
  EXPECT_THROW(
      {
        image_core::Image<image_core::RGB_Double> empty_image(0, 0);
        imageToPng(empty_image);
      },
      PngException);

  EXPECT_THROW(
      {
        image_core::Image<image_core::RGB_Double> empty_width(0, 10);
        imageToPng(empty_width);
      },
      PngException);

  EXPECT_THROW(
      {
        image_core::Image<image_core::RGB_Double> empty_height(10, 0);
        imageToPng(empty_height);
      },
      PngException);
}

TEST_F(PngPlusPlusTest, InvalidPngBufferHandling) {
  // Test with invalid PNG data
  std::vector<unsigned char> invalid_buffer = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};

  EXPECT_THROW({ pngToImage(invalid_buffer); }, PngException);

  // Test with empty buffer
  std::vector<unsigned char> empty_buffer;
  EXPECT_THROW({ pngToImage(empty_buffer); }, PngException);
}

TEST_F(PngPlusPlusTest, CompressionLevelSupport) {
  const int width = 50;
  const int height = 50;

  // Create a test image with repeating pattern (compresses well)
  image_core::Image<image_core::RGB_Double> test_image(width, height);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      // Create a pattern that should compress well
      if ((x / 10 + y / 10) % 2 == 0) {
        test_image.data[y][x] = image_core::RGB_Double{255.0, 0.0, 0.0};  // Red
      } else {
        test_image.data[y][x] = image_core::RGB_Double{0.0, 0.0, 255.0};  // Blue
      }
    }
  }

  // Test different compression levels
  std::vector<unsigned char> no_compression = imageToPng(test_image, 0);
  std::vector<unsigned char> default_compression = imageToPng(test_image);  // -1 default
  std::vector<unsigned char> best_compression = imageToPng(test_image, 9);

  // All should produce valid PNG data
  EXPECT_GT(no_compression.size(), 0);
  EXPECT_GT(default_compression.size(), 0);
  EXPECT_GT(best_compression.size(), 0);

  // Verify PNG signatures
  const unsigned char png_signature[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(no_compression[i], png_signature[i]);
    EXPECT_EQ(default_compression[i], png_signature[i]);
    EXPECT_EQ(best_compression[i], png_signature[i]);
  }

  // Best compression should generally produce smaller files than no compression
  // Note: For small images, overhead might make this not always true, so we just verify they're
  // different
  EXPECT_NE(no_compression.size(), best_compression.size());

  // All should decode to the same image
  image_core::Image<image_core::RGB_Double> decoded_no_comp = pngToImage(no_compression);
  image_core::Image<image_core::RGB_Double> decoded_default = pngToImage(default_compression);
  image_core::Image<image_core::RGB_Double> decoded_best = pngToImage(best_compression);

  EXPECT_EQ(decoded_no_comp.width, width);
  EXPECT_EQ(decoded_default.width, width);
  EXPECT_EQ(decoded_best.width, width);

  // Verify pixel values are identical across all compression levels
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      EXPECT_EQ(static_cast<int>(decoded_no_comp.data[y][x].r),
                static_cast<int>(decoded_default.data[y][x].r));
      EXPECT_EQ(static_cast<int>(decoded_default.data[y][x].r),
                static_cast<int>(decoded_best.data[y][x].r));

      EXPECT_EQ(static_cast<int>(decoded_no_comp.data[y][x].g),
                static_cast<int>(decoded_default.data[y][x].g));
      EXPECT_EQ(static_cast<int>(decoded_default.data[y][x].g),
                static_cast<int>(decoded_best.data[y][x].g));

      EXPECT_EQ(static_cast<int>(decoded_no_comp.data[y][x].b),
                static_cast<int>(decoded_default.data[y][x].b));
      EXPECT_EQ(static_cast<int>(decoded_default.data[y][x].b),
                static_cast<int>(decoded_best.data[y][x].b));
    }
  }
}

TEST_F(PngPlusPlusTest, MemoryPngWriterCompressionLevelGetter) {
  const int width = 20;
  const int height = 20;

  // Test default compression level
  MemoryPngWriter writer_default(width, height);
  EXPECT_EQ(writer_default.getCompressionLevel(), -1);

  // Test specific compression levels
  MemoryPngWriter writer_no_compress(width, height, 0);
  EXPECT_EQ(writer_no_compress.getCompressionLevel(), 0);

  MemoryPngWriter writer_best_compress(width, height, 9);
  EXPECT_EQ(writer_best_compress.getCompressionLevel(), 9);

  // Test invalid compression level (should still store the value but not apply it)
  MemoryPngWriter writer_invalid(width, height, 15);
  EXPECT_EQ(writer_invalid.getCompressionLevel(), 15);
}

TEST_F(PngPlusPlusTest, CompressionLevelValidation) {
  const int width = 30;
  const int height = 30;

  // Create simple test image
  std::vector<std::vector<RGB>> simple_image(height, std::vector<RGB>(width, RGB{128, 128, 128}));

  // Test that invalid compression levels don't crash
  EXPECT_NO_THROW({
    MemoryPngWriter writer_negative(width, height, -5);
    writer_negative.writeImage(simple_image);
  });

  EXPECT_NO_THROW({
    MemoryPngWriter writer_too_high(width, height, 20);
    writer_too_high.writeImage(simple_image);
  });

  // Valid compression levels should work
  for (int level = 0; level <= 9; ++level) {
    EXPECT_NO_THROW({
      MemoryPngWriter writer(width, height, level);
      writer.writeImage(simple_image);
      const auto& buffer = writer.getBuffer();
      EXPECT_GT(buffer.size(), 0);
    }) << "Compression level "
       << level << " failed";
  }
}

}  // namespace
}  // namespace pngpp
