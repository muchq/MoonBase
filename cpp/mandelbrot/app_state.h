#ifndef CPP_MANDELBROT_APP_STATE_H
#define CPP_MANDELBROT_APP_STATE_H

#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>

#include "cpp/trill/trill.h"

class AppContext final : public trill::AppContextInterface {
 public:
  explicit AppContext(SDL_Window* window, SDL_Renderer* renderer, SDL_Texture* texture,
                      int initialWidth, int initialHeight)
      : window(window),
        renderer(renderer),
        texture(texture),
        current_width(initialWidth),
        current_height(initialHeight),
        previous_width(initialWidth),
        previous_height(initialHeight) {}

  SDL_Window* GetWindow() override;
  SDL_Renderer* GetRenderer() override;
  SDL_Texture* GetTexture() override;

 private:
  SDL_Window* window;
  SDL_Renderer* renderer;
  SDL_Texture* texture;
  complex<double> current_top_left = {-2, 2};
  complex<double> current_bottom_right = {2, -2};
  int current_width;
  int current_height;
  complex<double> previous_top_left = {-2, 2};
  complex<double> previous_bottom_right = {2, -2};
  int previous_width;
  int previous_height;
  complex<double> mouse_down = {0, 0};
  complex<double> mouse_down_raw = {0, 0};
  int iterations = 100;
  bool first = true;
  SDL_FRect selected;
};

#endif
