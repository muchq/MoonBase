#ifndef CPP_TRILL_TRILL_H
#define CPP_TRILL_TRILL_H

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>

#include <string>

namespace trill {
struct SdlContext {
  SDL_Window* window;
  SDL_Renderer* renderer;
  SDL_Texture* background;
};

struct InitConfig {
  std::string name;
  int width = 800;
  int height = 640;
  SDL_InitFlags init_flags = SDL_INIT_VIDEO | SDL_INIT_EVENTS;
  SDL_WindowFlags window_flags =
      SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
  SDL_PixelFormat pixel_format = SDL_PIXELFORMAT_RGBA32;
  SDL_TextureAccess texture_access = SDL_TEXTUREACCESS_STREAMING;
};

SDL_AppResult Initialize(const InitConfig& initConfig, SdlContext* sdlContext);
}  // namespace trill

#endif
