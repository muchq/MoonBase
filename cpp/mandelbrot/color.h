#ifndef CPP_MANDELBROT_COLOR_H
#define CPP_MANDELBROT_COLOR_H
#include <cstdint>

using std::uint8_t;

struct Color {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
};

Color escape_time_to_color(int escape_time);

#endif
