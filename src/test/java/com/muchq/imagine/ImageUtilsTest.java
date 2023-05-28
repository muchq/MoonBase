package com.muchq.imagine;

import com.muchq.imagine.ImageUtils;
import com.muchq.imagine.Radius;
import org.junit.Test;

import javax.imageio.ImageIO;
import javax.swing.ImageIcon;
import javax.swing.JFrame;
import javax.swing.JLabel;
import java.awt.BorderLayout;
import java.awt.image.BufferedImage;
import java.io.File;
import java.io.IOException;

public class ImageUtilsTest {
    @Test
    public void itCanReadImages() throws IOException, InterruptedException {
        BufferedImage marbles = ImageIO.read(ImageUtils.class.getResourceAsStream("/MARBLES.BMP"));
        BufferedImage marblesCopy2 = ImageUtils.grayScaleSlow(marbles);
        BufferedImage marblesCopy3 = ImageUtils.grayScale(marbles);
        display(marbles);
        display(marblesCopy2);
        display(marblesCopy3);
        Thread.sleep(35_000);
    }

    @Test
    public void itCanBlurImages() throws IOException, InterruptedException {
        BufferedImage marbles = ImageIO.read(ImageUtils.class.getResourceAsStream("/MARBLES.BMP"));
        BufferedImage graussian = ImageUtils.grayGaussianBlur(marbles, Radius.FIVE, 1);

        display(marbles);
        display(graussian);
        ImageIO.write(graussian, "png", new File("blur.png"));
        Thread.sleep(35_000);

        System.out.println("yo");
    }

    @Test
    public void itCanSobelImages() throws IOException, InterruptedException {
        BufferedImage marbles = ImageIO.read(ImageUtils.class.getResourceAsStream("/MARBLES.BMP"));
        BufferedImage graussian = ImageUtils.grayGaussianBlur(marbles, Radius.FIVE, 1);
        BufferedImage sobel = ImageUtils.sobel(graussian);

        display(marbles);
        display(graussian);
        display(sobel);
        File file = new File("sobel.png");
        boolean success = ImageIO.write(sobel, "png", file);
        System.out.println(success);
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
