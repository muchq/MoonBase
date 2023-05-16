package com.muchq.imagine.blur;

import org.assertj.core.util.Files;
import org.junit.Test;

import javax.imageio.ImageIO;
import javax.swing.ImageIcon;
import javax.swing.JFrame;
import javax.swing.JLabel;
import java.awt.BorderLayout;
import java.awt.image.BufferedImage;
import java.io.IOException;

public class BlurtilsTest {
    @Test
    public void itCanReadImages() throws IOException, InterruptedException {
        BufferedImage marbles = ImageIO.read(Blurtils.class.getResourceAsStream("/MARBLES.BMP"));
        BufferedImage marblesCopy2 = Blurtils.grayScaleSlow(marbles);
        BufferedImage marblesCopy3 = Blurtils.grayScale(marbles);
        display(marbles);
        display(marblesCopy2);
        display(marblesCopy3);
        Thread.sleep(35_000);
    }

    @Test
    public void itCanBlurImages() throws IOException, InterruptedException {
        BufferedImage marbles = ImageIO.read(Blurtils.class.getResourceAsStream("/MARBLES.BMP"));
        BufferedImage gaussianBlur = Blurtils.gaussianBlur(marbles);
        //int[] gaussianFive = {1, 4, 7, 4, 1, 4, 16, 26, 16, 4, 7, 26, 41, 26, 7, 4, 16, 26, 16, 4, 1, 4, 7, 4, 1};
        int[] heavyGaussian = {0, 2, 2, 2, 0, 2, 4, 6, 4, 2, 2, 6, 10, 6, 2, 2, 4, 6, 4, 2, 0, 2, 2, 2, 0};
        BufferedImage bigGaussian = Blurtils.convolution(Blurtils.grayScale(marbles), heavyGaussian);
        BufferedImage gaussian55 = Blurtils.convolution(bigGaussian, heavyGaussian);
        BufferedImage gaussian555 = Blurtils.convolution(gaussian55, heavyGaussian);

        display(marbles);
        display(gaussian555);
        display(gaussianBlur);
        ImageIO.write(gaussian555, "png", Files.newFile("blur.png"));
        Thread.sleep(35_000);

        System.out.println("yo");
    }

    private void display(BufferedImage img) {
        JFrame frame = new JFrame();
        JLabel label = new JLabel();
        frame.setSize(img.getWidth(), img.getHeight());
        label.setIcon(new ImageIcon(img));
        frame.getContentPane().add(label, BorderLayout.CENTER);
        frame.pack();
        frame.setVisible(true);
    }

}
