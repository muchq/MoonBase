package com.muchq.imagine.blur;

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
        BufferedImage marblesCopy = Blurtils.grayScale(marbles);
        display(marbles);
        display(marblesCopy);
        System.out.println("sup");
        Thread.sleep(35_000);
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
