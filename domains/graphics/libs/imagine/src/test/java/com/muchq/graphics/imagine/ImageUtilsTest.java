package com.muchq.graphics.imagine;

import java.awt.image.BufferedImage;
import java.io.IOException;
import javax.imageio.ImageIO;
import org.junit.Test;

public class ImageUtilsTest {
  @Test
  public void itCanBlurImages() throws IOException {
    BufferedImage marbles =
        ImageIO.read(ImageUtils.class.getResourceAsStream("/domains/graphics/data/MARBLES.BMP"));
    BufferedImage graussian = ImageUtils.grayGaussianBlur(marbles, Radius.FIVE, 1);

    // assert stuff about pixels
  }

  @Test
  public void itCanSobelImages() throws IOException {
    BufferedImage marbles =
        ImageIO.read(ImageUtils.class.getResourceAsStream("/domains/graphics/data/MARBLES.BMP"));
    BufferedImage graussian = ImageUtils.grayGaussianBlur(marbles, Radius.FIVE, 1);
    BufferedImage sobel = ImageUtils.sobel(graussian);

    // assert stuff about pixels
  }
}
