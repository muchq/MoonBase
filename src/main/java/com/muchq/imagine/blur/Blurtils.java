package com.muchq.imagine.blur;

import java.awt.Graphics;
import java.awt.image.BufferedImage;
import java.awt.image.ColorModel;
import java.awt.image.DataBufferByte;
import java.awt.image.WritableRaster;

public final class Blurtils {
    private Blurtils() {
        throw new AssertionError();
    }

    public static BufferedImage meanBlur(BufferedImage input) {
        int[] kernel = {1, 1, 1, 1, 1, 1, 1, 1, 1};
        return convolution(grayScale(input), kernel);
    }

    public static BufferedImage gaussianBlur(BufferedImage input) {
        int[] kernel = {1, 3, 1, 3, 9, 3, 1, 3, 1};
        return convolution(grayScale(input), kernel);
    }

    public static BufferedImage grayScale(BufferedImage input) {
        BufferedImage copied = new BufferedImage(input.getWidth(), input.getHeight(), BufferedImage.TYPE_BYTE_GRAY);
        Graphics graphics = copied.getGraphics();
        graphics.drawImage(input, 0, 0, null);
        graphics.dispose();
        return copied;
    }

    public static BufferedImage grayScaleSlow(BufferedImage input) {
        BufferedImage copied = new BufferedImage(input.getWidth(), input.getHeight(), BufferedImage.TYPE_BYTE_GRAY);

        byte[] pixels = getPixels(input);
        byte[] outputPixels = getPixels(copied);
        final boolean hasAlpha = copied.getAlphaRaster() != null;
        final int bytesPerPixel = hasAlpha ? 4 : 3;

        for (int p=0, outputIndex=0; p<pixels.length; p+= bytesPerPixel, outputIndex++) {
            int r, g, b;
            int i = hasAlpha ? p+1 : p;
            r = pixels[i] & 0xff;
            g = pixels[i+1] & 0xff;
            b = pixels[i+2] & 0xff;
            byte avg = (byte)((r + g + b) / 3);
            outputPixels[outputIndex] = avg;
        }
        return copied;
    }

    public static BufferedImage copy(BufferedImage input) {
        ColorModel colorModel = input.getColorModel();
        boolean isAlphaPremultiplied = colorModel.isAlphaPremultiplied();
        WritableRaster raster = input.copyData(null);
        return new BufferedImage(colorModel, raster, isAlphaPremultiplied, null);
    }

    public static BufferedImage convolution(BufferedImage input, int[] kernel) {
        // TODO: assert that kernel.length is an odd square (3x3, 5x5, 7x7)
        // TODO: what is a GPU?
        final int edgeOffset = (int)Math.sqrt(kernel.length)/2;
        final byte[] inputPixels = getPixels(input);
        final BufferedImage output = copy(input);
        final byte[] outputPixels = getPixels(output);

        final int kernelSum = sum(kernel);
        final int width = input.getWidth();
        final int height = input.getHeight();

        for (int row = edgeOffset; row < height - edgeOffset; row++) {
            for (int col = edgeOffset; col < width - edgeOffset; col++) {
                int i = 0;
                int dotProduct = 0;
                for (int r = -edgeOffset; r <= edgeOffset; r++) {
                    for (int c = -edgeOffset; c <= edgeOffset; c++) {
                        int neighborPixel = inputPixels[computeIndex(row+r, col+c, width)] & 0xff;
                        dotProduct = Math.addExact(dotProduct, kernel[i] * neighborPixel);
                        i++;
                    }
                }

                byte convolvedValue = (byte)(dotProduct / kernelSum);
                outputPixels[computeIndex(row, col, width)] = convolvedValue;
            }
        }

        return output;
    }

    private static int computeIndex(int row, int col, int width) {
        return row*width + col;
    }

    // no need to check for overflow here because these ints are all small kernel values
    // and kernel sizes are small (3x3, 5x5, 7x7)
    private static int sum(int[] ints) {
        int sum = 0;
        for (int i : ints) {
            sum += i;
        }
        return sum;
    }

    private static byte[] getPixels(BufferedImage image) {
        return ((DataBufferByte) image.getRaster().getDataBuffer()).getData();
    }
}
