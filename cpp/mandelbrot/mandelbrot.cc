#define SDL_MAIN_USE_CALLBACKS 1

#include <stdexcept>
#include <SDL3/SDL.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_video.h>
#include <complex>
#include <numeric>

constexpr uint32_t windowStartWidth = 1000;
constexpr uint32_t windowStartHeight = 1000;

using std::ceil;
using std::complex;
using std::lerp;
using std::norm;
using std::pow;
using std::sqrt;

int in_mandelbrot(complex<double> c, int depth) {
    complex<double> z{0, 0};
    for (int i=0; i<depth; i++) {
        z = pow(z, 2) + c;
        if (norm(z) > 128) {
            return i;
        }
    }
    return 0;
}

struct AppContext {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_AppResult state = SDL_APP_CONTINUE;
    complex<double> current_top_left = {-2, 2};
    complex<double> current_bottom_right = {2, -2};
    int current_width = windowStartWidth;
    int current_height = windowStartHeight;
    complex<double> previous_top_left = {-2, 2};
    complex<double> previous_bottom_right = {2, -2};
    int previous_width = windowStartWidth;
    int previous_height = windowStartHeight;
    complex<double> mouse_down = {0, 0};
    complex<double> mouse_down_raw = {0, 0};
    int iterations = 100;
    bool first = true;
    SDL_FRect selected;
};

bool no_change(const AppContext *app_context) {
    return app_context->current_height == app_context->previous_height &&
        app_context->current_width == app_context->previous_width &&
            app_context->current_top_left == app_context->previous_top_left &&
                app_context->current_bottom_right == app_context->previous_bottom_right;
}

SDL_AppResult SDL_Fail(){
    SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "Error %s", SDL_GetError());
    return SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    // init the library, here we make a window so we only need the Video capabilities.
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        return SDL_Fail();
    }

    // create a window
    SDL_Window* window = SDL_CreateWindow("Mandelbrot", windowStartWidth, windowStartHeight, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window){
        return SDL_Fail();
    }

    // create a renderer
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer){
        return SDL_Fail();
    }

    // set up the application data
    *appstate = new AppContext{
       .window = window,
       .renderer = renderer,
    };

    SDL_SetRenderVSync(renderer, 2);   // enable vysnc on every other vertical refresh

    SDL_Log("Application started successfully!");
    SDL_ShowWindow(window);
    return SDL_APP_CONTINUE;
}

complex<double> event_to_position(const SDL_Event *event, AppContext *app) {
    float rel_x = event->button.x;
    float rel_y = event->button.y;
    double m_down_x = lerp(app->current_top_left.real(), app->current_bottom_right.real(), rel_x / static_cast<float>(app->current_width));
    double m_down_y = lerp(app->current_top_left.imag(), app->current_bottom_right.imag(), rel_y / static_cast<float>(app->current_height));
    return {m_down_x, m_down_y};
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event* event) {
    auto* app = static_cast<AppContext *>(appstate);

    if (event->type == SDL_EVENT_QUIT) {
        app->state = SDL_APP_SUCCESS;
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        app->mouse_down = event_to_position(event, app);
        app->mouse_down_raw = complex<double>{event->button.x, event->button.y};
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        app->current_top_left = {app->mouse_down.real(), app->mouse_down.imag()};
        app->current_bottom_right = event_to_position(event, app);
        app->mouse_down = {0, 0};
        app->mouse_down_raw = {0, 0};
    } else if (event->type == SDL_EVENT_MOUSE_MOTION) {
        // draw box
        if (app->mouse_down_raw != complex<double>{0, 0}) {
            app->selected = {
                .x = static_cast<float>(app->mouse_down_raw.real()),
                .y = static_cast<float>(app->mouse_down_raw.imag()),
                .w = event->motion.x - static_cast<float>(app->mouse_down_raw.real()),
                .h = event->motion.y - static_cast<float>(app->mouse_down_raw.imag()),
            };
            SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 80);
            SDL_RenderRect(app->renderer, &app->selected);
            SDL_RenderPresent(app->renderer);
        }
    }

    return app->state;
}

struct Color {
    int r = 0;
    int g = 0;
    int b = 0;
};

Color escape_time_to_color(int escape_time) {
    if (escape_time == 0) {
        return Color{
        .r = 0,
        .g = 0,
        .b = 0,
        };
    } else {
        return Color{
            .r = 2*escape_time % 255,
            .g = 13*escape_time % 255,
            .b = 25*escape_time % 255,
        };
    }
}

void render(AppContext *app) {
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(app->renderer);

    for (double x=0.0; x<1.0; x += 0.001) {
        for (double y=0.0; y<1.0; y += 0.001) {
            double real = lerp(app->current_top_left.real(), app->current_bottom_right.real(), x);
            double imag = lerp(app->current_top_left.imag(), app->current_bottom_right.imag(), y);
            int escape_time = in_mandelbrot(complex<double>{real, imag}, app->iterations);
            Color c = escape_time_to_color(escape_time);
            SDL_SetRenderDrawColor(app->renderer, c.r, c.g, c.b, SDL_ALPHA_OPAQUE);
            SDL_RenderPoint(app->renderer, x * 1000, y * 1000);
        }
    }

    SDL_RenderPresent(app->renderer);
    SDL_Log("Rendered something");
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    auto* app = static_cast<AppContext *>(appstate);

    if (!app->first && no_change(app)) {
        return app->state;
    }

    app->first = false;
    app->iterations = ceil(app->iterations * 1.15);
    SDL_Log("new iterations: %d", app->iterations);

    app->previous_top_left = app->current_top_left;
    app->previous_bottom_right = app->current_bottom_right;

    render(app);
    return app->state;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    auto* app = (AppContext*)appstate;
    if (app) {
        SDL_DestroyRenderer(app->renderer);
        SDL_DestroyWindow(app->window);
        delete app;
    }
    SDL_Log("Application quit successfully!");
    SDL_Quit();
}


// int main() {
//     if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
//         throw std::runtime_error("SDL_Init failed");
//     }
//
//     SDL_Window *window = SDL_CreateWindow("Mandelbrot", 800, 600, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
//     if (!window) {
//         throw std::runtime_error("SDL_CreateWindow failed");
//     }
//     SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);
//     if (!renderer) {
//         throw std::runtime_error("SDL_CreateRenderer failed");
//     }
//
//     SDL_ShowWindow(window);
//
//     bool isRunning = true;
//     SDL_Event event;
//
//     while (isRunning) {
//         while (SDL_PollEvent(&event)) {
//             switch (event.type) {
//                 case SDL_EVENT_QUIT:
//                     isRunning = false;
//                 case SDL_EVENT_WINDOW_RESIZED:
//                     render();
//             }
//         }
//     }
//
//     SDL_DestroyRenderer(renderer);
//     SDL_DestroyWindow(window);
//     SDL_Quit();
//
//     return 0;
// }
