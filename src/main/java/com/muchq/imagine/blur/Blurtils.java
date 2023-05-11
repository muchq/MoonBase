package com.muchq.imagine.blur;

import java.awt.image.BufferedImage;
import java.awt.image.ColorModel;
import java.awt.image.DataBufferByte;
import java.awt.image.WritableRaster;

public final class Blurtils {
    private Blurtils() {
        throw new AssertionError();
    }

    public static BufferedImage meanBlur(BufferedImage input) {
        BufferedImage blurred = copy(input);
        // blur stuff here using 3x3 kernel of 1s
        return blurred;
    }

    public static BufferedImage grayScale(BufferedImage input) {
        // TODO: should be able to just store 1 byte per pixel in the grayscale copy instead of 3 or 4...
        BufferedImage copied = copy(input);
        byte[] pixels = ((DataBufferByte) copied.getRaster().getDataBuffer()).getData();
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
}
