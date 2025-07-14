#include "trill.h"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>

namespace trill {

InitializeResult SDL_Fail(SdlContext context) {
  SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "Error %s", SDL_GetError());
  return InitializeResult{
      .result = SDL_APP_FAILURE,
      .context = context,
  };
}

InitializeResult Initialize(const InitConfig& initConfig) {
  SdlContext sdl_context = {};

  // init the library, here we make a window so we only need the Video capabilities.
  if (!SDL_Init(initConfig.init_flags)) {
    return SDL_Fail(sdl_context);
  }

  // create a window
  SDL_Window* window = SDL_CreateWindow(initConfig.name.c_str(), initConfig.width,
                                        initConfig.height, initConfig.window_flags);
  if (!window) {
    return SDL_Fail(sdl_context);
  }

  // create a renderer
  SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
  if (!renderer) {
    return SDL_Fail(sdl_context);
  }

  int rw = 0, rh = 0;
  SDL_GetRenderOutputSize(renderer, &rw, &rh);
  if(rw != initConfig.width) {
    float widthScale = (float)rw / (float) initConfig.width;
    float heightScale = (float)rh / (float) initConfig.height;

    if(widthScale != heightScale) {
      SDL_Log("WARNING: width scale != height scale");
    }

    SDL_SetRenderScale(renderer, widthScale, heightScale);
  }

  // a texture to hold renderer mandelbrot images while we draw the mouse selection area for zooming
  SDL_Texture* texture =
      SDL_CreateTexture(renderer, initConfig.pixel_format, initConfig.texture_access,
                        initConfig.width, initConfig.height);
  if (!texture) {
    return SDL_Fail(sdl_context);
  }

  // set up the application data
  sdl_context.window = window;
  sdl_context.renderer = renderer;
  sdl_context.background = texture;

  SDL_SetRenderVSync(renderer, 2);  // enable vysnc on every other vertical refresh

  SDL_Log("Application started successfully!");
  SDL_ShowWindow(window);
  return InitializeResult{
      .result = SDL_APP_CONTINUE,
      .context = sdl_context,
  };
}
}  // namespace trill
