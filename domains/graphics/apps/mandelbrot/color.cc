#include "color.h"

Color escape_time_to_color(int escape_time) {
  if (escape_time == 0) {
    return Color{
        .r = 0,
        .g = 0,
        .b = 0,
    };
  } else {
    return Color{
        .r = static_cast<uint8_t>(2 * escape_time % 255),
        .g = static_cast<uint8_t>(13 * escape_time % 255),
        .b = static_cast<uint8_t>(25 * escape_time % 255),
    };
  }
}
