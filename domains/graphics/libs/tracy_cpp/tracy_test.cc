#include "tracy.h"

#include <gtest/gtest.h>

using namespace tracy;

TEST(Vec3Test, DotProduct) {
  // Arrange
  Vec3 v1{1.0, 2.0, 3.0};
  Vec3 v2{4.0, 5.0, 6.0};

  // Act
  double result = v1.dot(v2);

  // Assert
  EXPECT_DOUBLE_EQ(result, 32.0);  // 1*4 + 2*5 + 3*6 = 4 + 10 + 18 = 32
}

TEST(Vec3Test, DotProductZeroVector) {
  // Arrange
  Vec3 v1{1.0, 2.0, 3.0};
  Vec3 v2{0.0, 0.0, 0.0};

  // Act
  double result = v1.dot(v2);

  // Assert
  EXPECT_DOUBLE_EQ(result, 0.0);
}

TEST(Vec3Test, DotProductSameVector) {
  // Arrange
  Vec3 v1{3.0, 4.0, 0.0};

  // Act
  double result = v1.dot(v1);

  // Assert
  EXPECT_DOUBLE_EQ(result, 25.0);  // 3*3 + 4*4 + 0*0 = 9 + 16 + 0 = 25
}