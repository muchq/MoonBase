package com.muchq.imagine.blur;

import java.awt.Graphics;
import java.awt.image.BufferedImage;
import java.awt.image.ColorModel;
import java.awt.image.DataBufferByte;
import java.awt.image.WritableRaster;
import java.util.stream.IntStream;

public final class Blurtils {
    private Blurtils() {
        throw new AssertionError();
    }

    public static BufferedImage meanBlur(BufferedImage input) {
        int[] kernel = {1, 1, 1, 1, 1, 1, 1, 1, 1};
        return convolution(grayScaleGood(input), kernel);
    }

    public static BufferedImage gaussianBlur(BufferedImage input) {
        int[] kernel = {1, 3, 1, 3, 9, 3, 1, 3, 1};
        return convolution(grayScaleGood(input), kernel);
    }

    public static BufferedImage sobelX(BufferedImage input) {
        int[] kernel = {1, 3, 1, 3, 9, 3, 1, 3, 1};
        return convolution(grayScaleGood(input), kernel);
    }

    public static BufferedImage grayScaleGood(BufferedImage input) {
        BufferedImage copied = new BufferedImage(input.getWidth(), input.getHeight(), BufferedImage.TYPE_BYTE_GRAY);
        Graphics graphics = copied.getGraphics();
        graphics.drawImage(input, 0, 0, null);
        graphics.dispose();
        return copied;
    }

    public static BufferedImage grayScaleCompact(BufferedImage input) {
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

    public static BufferedImage grayScaleBad(BufferedImage input) {
        // TODO: should be able to just store 1 byte per pixel in the grayscale copy instead of 3 or 4...
        BufferedImage copied = copy(input);
        byte[] pixels = getPixels(copied);
        final boolean hasAlpha = copied.getAlphaRaster() != null;
        final int bytesPerPixel = hasAlpha ? 4 : 3;

        for (int p=0; p<pixels.length; p+= bytesPerPixel) {
            int r, g, b;
            int i = hasAlpha ? p+1 : p;
            r = pixels[i] & 0xff;
            g = pixels[i+1] & 0xff;
            b = pixels[i+2] & 0xff;
            byte avg = (byte)((r + g + b) / 3);
            if (hasAlpha) {
                pixels[p] = (byte)0xff;
            }
            pixels[i] = pixels[i+1] = pixels[i+2] = avg;
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
        int kernelSize = (int)Math.sqrt(kernel.length);
        int edgeOffset = kernelSize/2;
        byte[] inputPixels = getPixels(input);
        BufferedImage output = copy(input);
        byte[] outputPixels = getPixels(output);

        int kernelSum = IntStream.of(kernel).sum();

        for (int row = edgeOffset; row < input.getHeight() - edgeOffset; row++) {
            for (int col = edgeOffset; col < input.getWidth() - edgeOffset; col++) {
                final int r = row;
                final int c = col;
                // window is ordered by row,col. i.e top row of pixels is followed by the next row in the array
                int[] window = IntStream.rangeClosed(-edgeOffset, edgeOffset)
                        .flatMap(i -> IntStream.rangeClosed(-edgeOffset, edgeOffset)
                                .map(j -> computeIndex(r + i, c + j, input.getWidth())))
                        .map(index -> inputPixels[index] & 0xff)
                        .toArray();

                int sumOfProducts = 0;
                for (int i=0; i<kernel.length; i++) {
                    sumOfProducts = Math.addExact(sumOfProducts, kernel[i] * window[i]);
                }

                byte convolvedValue = (byte)(sumOfProducts / kernelSum);
                outputPixels[computeIndex(row, col, input.getWidth())] = convolvedValue;
            }
        }

        return output;
    }

    private static int computeIndex(int row, int col, int width) {
        return row*width + col;
    }

    private static byte[] getPixels(BufferedImage image) {
        return ((DataBufferByte) image.getRaster().getDataBuffer()).getData();
    }
}
