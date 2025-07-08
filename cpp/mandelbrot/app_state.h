#ifndef CPP_MANDELBROT_APP_STATE_H
#define CPP_MANDELBROT_APP_STATE_H

#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>

#include "cpp/trill/trill.h"

class AppContext final : public trill::AppContextInterface {
 public:
  explicit AppContext() = default;

  SDL_Window* GetWindow() override;
  SDL_Renderer* GetRenderer() override;
  SDL_Texture* GetTexture() override;

 private:
  SDL_Window* window;
  SDL_Renderer* renderer;
  SDL_Texture* texture;
};

#endif
