package com.muchq.imagine;

import javax.imageio.ImageIO;
import java.awt.image.BufferedImage;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Paths;

public class Main {
    public static void main(String[] args) throws IOException {
        String path = args[0];
        BufferedImage marbles = ImageIO.read(Files.newInputStream(Paths.get(path)));
        BufferedImage graussian = ImageUtils.grayGaussianBlur(marbles, Radius.FIVE, 1);
        BufferedImage sobel = ImageUtils.sobel(graussian);

        ImageIO.write(sobel, "png", new File("edges.png"));
    }
}
