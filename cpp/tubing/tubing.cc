#define SDL_MAIN_USE_CALLBACKS 1

#include <SDL3/SDL.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>
#include <math.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "cpp/trill/trill.h"

struct AppContext {
  trill::SdlContext sdl_context;
  int width;
  int height;
};

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
  trill::InitConfig init_config{
      .name = "TuberProV6",
      .width = 1000,
      .height = 800,
  };
  auto [result, sdlContext] = trill::Initialize(init_config);

  *appstate = new AppContext{
      .sdl_context = sdlContext,
      .width = init_config.width,
      .height = init_config.height,
  };
  return result;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
  if (event->type == SDL_EVENT_QUIT) {
    return SDL_APP_SUCCESS;
  }

  return SDL_APP_CONTINUE;
}

std::vector<float> compute_scales(uint64_t now_ms, int ms_per_cycle, uint8_t subdivisions) {
  std::vector<float> scales{};
  for (int i = 0; i < subdivisions; i++) {
    scales.emplace_back(((now_ms + i * (ms_per_cycle / subdivisions)) % ms_per_cycle) /
                        static_cast<float>(ms_per_cycle));
  }

  return scales;
}

void render(AppContext* app) {
  const uint64_t now_ms = SDL_GetTicks();
  SDL_Renderer* renderer = app->sdl_context.renderer;

  SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
  SDL_RenderClear(renderer);

  std::vector<float> scales = compute_scales(now_ms, 12000, 10);

  std::ranges::for_each(scales, [app, renderer](float scale) {
    float w = app->width * scale;
    float h = app->height * scale;
    SDL_FRect r{
        .x = (app->width - w) / 2,
        .y = (app->height - h) / 2,
        .w = w,
        .h = h,
    };

    float sin_scale = std::sin(scale * M_PI);
    uint8_t gray = 255 * sin_scale;

    SDL_SetRenderDrawColor(renderer, std::floor(std::sin(gray / 2)), gray, 255,
                           std::ceil(SDL_ALPHA_OPAQUE * (1 - scale)));
    SDL_RenderRect(renderer, &r);
  });

  SDL_RenderPresent(renderer);
}

SDL_AppResult SDL_AppIterate(void* appstate) {
  auto* app = static_cast<AppContext*>(appstate);
  render(app);

  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
  auto* app = static_cast<AppContext*>(appstate);
  if (app) {
    SDL_DestroyTexture(app->sdl_context.background);
    SDL_DestroyRenderer(app->sdl_context.renderer);
    SDL_DestroyWindow(app->sdl_context.window);
    delete app;
  }
  SDL_Log("Bye!");
  SDL_Quit();
}
