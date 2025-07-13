#include "trill.h"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>

namespace trill {

SDL_AppResult SDL_Fail() {
  SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "Error %s", SDL_GetError());
  return SDL_APP_FAILURE;
}

SDL_AppResult Initialize(const InitConfig& initConfig, SdlContext* sdlContext) {
  // init the library, here we make a window so we only need the Video capabilities.
  if (!SDL_Init(initConfig.init_flags)) {
    return SDL_Fail();
  }

  // create a window
  SDL_Window* window = SDL_CreateWindow(initConfig.name.c_str(), initConfig.width,
                                        initConfig.height, initConfig.window_flags);
  if (!window) {
    return SDL_Fail();
  }

  // create a renderer
  SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
  if (!renderer) {
    return SDL_Fail();
  }

  // a texture to hold renderer mandelbrot images while we draw the mouse selection area for zooming
  SDL_Texture* texture =
      SDL_CreateTexture(renderer, initConfig.pixel_format, initConfig.texture_access,
                        initConfig.width, initConfig.height);
  if (!texture) {
    return SDL_Fail();
  }

  // set up the application data
  sdlContext->window = window;
  sdlContext->renderer = renderer;
  sdlContext->background = texture;

  SDL_SetRenderVSync(renderer, 2);  // enable vysnc on every other vertical refresh

  SDL_Log("Application started successfully!");
  SDL_ShowWindow(window);
  return SDL_APP_CONTINUE;
}
}  // namespace trill
