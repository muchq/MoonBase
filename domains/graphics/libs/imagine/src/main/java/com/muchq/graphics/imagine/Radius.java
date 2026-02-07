package com.muchq.graphics.imagine;

import java.util.Arrays;

public enum Radius {
  THREE(new int[] {1, 3, 1, 3, 9, 3, 1, 3, 1}),
  FIVE(
      new int[] {
        1, 4, 7, 4, 1, 4, 16, 26, 16, 4, 7, 26, 41, 26, 7, 4, 16, 26, 16, 4, 1, 4, 7, 4, 1
      });

  private final int[] gaussianKernel;

  Radius(int[] gaussianKernel) {
    this.gaussianKernel = gaussianKernel;
  }

  public int[] getGaussianKernel() {
    return Arrays.copyOf(gaussianKernel, gaussianKernel.length);
  }
}
