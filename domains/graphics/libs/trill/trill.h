#ifndef DOMAINS_GRAPHICS_LIBS_TRILL_TRILL_H
#define DOMAINS_GRAPHICS_LIBS_TRILL_TRILL_H

#include <SDL3/SDL.h>

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

struct InitializeResult {
  SDL_AppResult result;
  SdlContext context;
};

InitializeResult Initialize(const InitConfig& initConfig);
}  // namespace trill

#endif
