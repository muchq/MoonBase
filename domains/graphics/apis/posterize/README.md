# Posterize

Image post-processing api

## Supported formats

Input images are auto-detected from their bytes. PNG, JPEG, GIF, BMP, TIFF,
WebP, and ICO are accepted. The result is returned in the same format as the
input where an encoder is available; WebP and ICO inputs are returned as PNG.

## Routes

### POST /imagine/v1/blur

blur an image

### POST /imagine/v1/edges

detect edges on an image
