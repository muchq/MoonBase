package com.muchq.imagine;

import java.awt.Graphics;
import java.awt.image.BufferedImage;
import java.awt.image.ColorModel;
import java.awt.image.DataBufferByte;
import java.awt.image.WritableRaster;
import java.util.Arrays;
import java.util.function.BiFunction;
import java.util.function.Function;

public final class ImageUtils {
    private static final int MAX_DEPTH = 50;

    private static final int[] SOBEL_X_KERNEL = {1, 0, -1, 2, 0, -2, 1, 0, -1};
    private static final int[] SOBEL_Y_KERNEL = {1, 2, 1, 0, 0, 0, -1, -2, -1};

    private ImageUtils() {
        throw new AssertionError();
    }

    public static BufferedImage meanBlur(BufferedImage input) {
        int[] kernel = {1, 1, 1, 1, 1, 1, 1, 1, 1};
        return convolutionScaleByKernelSum(input, kernel);
    }

    public static BufferedImage gaussianBlur(BufferedImage input, Radius radius) {
        return convolutionScaleByKernelSum(input, radius.getGaussianKernel());
    }

    public static BufferedImage grayGaussianBlur(BufferedImage input, Radius radius, int depth) {
        validateDepth(depth);
        int[] kernel = radius.getGaussianKernel();
        BufferedImage grayCopy = grayScale(input);
        BufferedImage blurred = grayCopy;
        for (int i = 0; i < depth; i++) {
            blurred = convolutionScaleByKernelSum(blurred, kernel);
        }
        return blurred;
    }

    public static BufferedImage grayScale(BufferedImage input) {
        BufferedImage copied = new BufferedImage(input.getWidth(), input.getHeight(), BufferedImage.TYPE_BYTE_GRAY);
        Graphics graphics = copied.getGraphics();
        graphics.drawImage(input, 0, 0, null);
        graphics.dispose();
        return copied;
    }

    // assumes a gray image
    public static BufferedImage sobel(BufferedImage input) {
        // wasteful atm, but maybe worth caching these partial results?
        int[] inputPixels = bytesToInts(getPixels(input));
        int[] sobelX = convolve(inputPixels, input.getWidth(), input.getHeight(), SOBEL_X_KERNEL);
        int[] sobelY = convolve(inputPixels, input.getWidth(), input.getHeight(), SOBEL_Y_KERNEL);

        Function<Double, Byte> scaleToByte = (d) -> (byte)((255 * d) / 360);
        BufferedImage sobel = new BufferedImage(input.getWidth(), input.getHeight(), BufferedImage.TYPE_BYTE_GRAY);
        byte[] sobelPixels = getPixels(sobel);
        for (int i=0; i<sobelPixels.length; i++) {
            int xi = sobelX[i];
            int yi = sobelY[i];
            sobelPixels[i] = scaleToByte.apply(Math.sqrt(xi*xi + yi*yi));
        }

        return sobel;
    }

    public static BufferedImage gate(BufferedImage input, int threshold) {
        BufferedImage copied = copy(input);
        byte[] pixels = getPixels(copied);

        for (int i=0; i<pixels.length; i++) {
            if ((pixels[i] & 0xff) > threshold) {
                pixels[i] = 0;
            } else {
                pixels[i] = (byte)255;
            }
        }

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

    public static BufferedImage convolutionScaleByKernelSum(BufferedImage input, int[] kernel) {
        final int kernelSum = sum(kernel);
        return convolve(input, kernel, (d, k) -> (byte)(d / kernelSum));
    }

    public static BufferedImage convolve(BufferedImage input, int[] kernel, BiFunction<Integer, int[], Byte> toByte) {
        int[] inputPixels = bytesToInts(getPixels(input));
        BufferedImage output = new BufferedImage(input.getWidth(), input.getHeight(), BufferedImage.TYPE_BYTE_GRAY);

        int[] convOutput = convolve(inputPixels, input.getWidth(), input.getHeight(), kernel);
        byte[] outputPixels = getPixels(output);
        for (int i=0; i<outputPixels.length; i++) {
            outputPixels[i] = toByte.apply(convOutput[i], null);
        }

        return output;
    }

    /**
     *
     * @param input a 1d array representing a 2d matrix laid out with pixels horizontally adjacent
     *              pixels in the 2d matrix adjacent in the 1d array. To find the pixel immediately
     *              below a pixel at index i, look at index i + width
     * @param width matrix width
     * @param height matrix height
     * @param kernel
     */
    public static int[] convolve(int[] input, int width, int height, int[] kernel) {
        // TODO: assert that kernel.length is an odd square (3x3, 5x5, 7x7)
        // TODO: what is a GPU?
        final int edgeOffset = (int)Math.sqrt(kernel.length)/2;
        final int[] outputPixels = Arrays.copyOf(input, input.length);

        for (int row = edgeOffset; row < height - edgeOffset; row++) {
            for (int col = edgeOffset; col < width - edgeOffset; col++) {
                int i = 0;
                int dotProduct = 0;
                for (int r = -edgeOffset; r <= edgeOffset; r++) {
                    for (int c = -edgeOffset; c <= edgeOffset; c++) {
                        int neighborPixel = input[computeIndex(row+r, col+c, width)];
                        dotProduct = Math.addExact(dotProduct, kernel[i] * neighborPixel);
                        i++;
                    }
                }

                outputPixels[computeIndex(row, col, width)] = dotProduct;
            }
        }

        return outputPixels;
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

    private static void validateDepth(int depth) {
        if (depth < 1 || depth > MAX_DEPTH) {
            throw new RuntimeException("blur depth must be > 0 and < " + MAX_DEPTH);
        }
    }

    private static int[] bytesToInts(byte[] bytes) {
        int[] ints = new int[bytes.length];

        for (int i=0; i<bytes.length; i++) {
            ints[i] = bytes[i] & 0xff;
        }

        return ints;
    }
}
