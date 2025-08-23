#include "cpp/png_plusplus/png_plusplus.h"

#include <gtest/gtest.h>

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

}  // namespace
}  // namespace pngpp