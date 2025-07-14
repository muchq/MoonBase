#define SDL_MAIN_USE_CALLBACKS 1

#include "cpp/trill/trill.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <vector>

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

void render(AppContext* app) {
    const uint64_t now_ms = SDL_GetTicks();
    SDL_Renderer *renderer = app->sdl_context.renderer;

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
    std::vector<float> scales{
        static_cast<float>((now_ms % 5000) / 5000.0),
        static_cast<float>(((now_ms + 1000) % 5000) / 5000.0),
        static_cast<float>(((now_ms + 2000) % 5000) / 5000.0),
        static_cast<float>(((now_ms + 3000) % 5000) / 5000.0),
        static_cast<float>(((now_ms + 4000) % 5000) / 5000.0),
    };


    std::vector<SDL_FRect> rects{};
    auto op = [app](float scale) -> SDL_FRect {
        const float w = 900.0f * scale;
        const float h = 700.0f * scale;

        const SDL_FRect r{
            .x = app->width - w / 2,
            .y = app->height - h / 2,
            .w = w,
            .h = h,
        };
        return r;
    };
    std::ranges::transform(scales, std::back_inserter(rects), op);

    for (auto r : rects) {
        SDL_RenderRect(renderer, &r);
    }
    SDL_RenderPresent(renderer);
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* app = static_cast<AppContext*>(appstate);
    render(app);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    auto* app = static_cast<AppContext *>(appstate);
    if (app) {
        SDL_DestroyTexture(app->sdl_context.background);
        SDL_DestroyRenderer(app->sdl_context.renderer);
        SDL_DestroyWindow(app->sdl_context.window);
        delete app;
    }
    SDL_Log("Bye!");
    SDL_Quit();
}
