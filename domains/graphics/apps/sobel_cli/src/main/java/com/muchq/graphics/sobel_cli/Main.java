package com.muchq.graphics.sobel_cli;

import com.muchq.graphics.imagine.ImageUtils;
import com.muchq.graphics.imagine.Radius;
import java.awt.image.BufferedImage;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Paths;
import javax.imageio.ImageIO;

public class Main {
  public static void main(String[] args) throws IOException {
    if (args.length == 0) {
      System.err.println("Usage: sobel_cli <image_path>");
      System.exit(1);
    }
    String path = args[0];
    BufferedImage marbles = ImageIO.read(Files.newInputStream(Paths.get(path)));
    BufferedImage graussian = ImageUtils.grayGaussianBlur(marbles, Radius.FIVE, 1);
    BufferedImage sobel = ImageUtils.sobel(graussian);

    ImageIO.write(graussian, "png", new File("blurred.png"));
    ImageIO.write(sobel, "png", new File("edges.png"));
  }
}
