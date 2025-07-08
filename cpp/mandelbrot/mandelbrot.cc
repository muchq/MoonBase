#define SDL_MAIN_USE_CALLBACKS 1

#include <SDL3/SDL.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_video.h>

#include <complex>
#include <numeric>
#include <sys/stat.h>

#include "color.h"

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
  for (int i = 0; i < depth; i++) {
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
  SDL_Texture* texture = nullptr;
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

bool no_change(const AppContext* app_context) {
  return app_context->current_height == app_context->previous_height &&
         app_context->current_width == app_context->previous_width &&
         app_context->current_top_left == app_context->previous_top_left &&
         app_context->current_bottom_right == app_context->previous_bottom_right;
}

SDL_AppResult SDL_Fail() {
  SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "Error %s", SDL_GetError());
  return SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
  // init the library, here we make a window so we only need the Video capabilities.
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    return SDL_Fail();
  }

  // create a window
  SDL_Window* window =
      SDL_CreateWindow("Mandelbrot", windowStartWidth, windowStartHeight,
                       SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
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
      SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                        windowStartWidth, windowStartHeight);
  if (!texture) {
    return SDL_Fail();
  }

  // set up the application data
  *appstate = new AppContext{
      .window = window,
      .renderer = renderer,
      .texture = texture,
  };

  SDL_SetRenderVSync(renderer, 2);  // enable vysnc on every other vertical refresh

  SDL_Log("Application started successfully!");
  SDL_ShowWindow(window);
  return SDL_APP_CONTINUE;
}

complex<double> event_to_position(const SDL_Event* event, AppContext* app) {
  float rel_x = event->button.x;
  float rel_y = event->button.y;
  double m_down_x = lerp(app->current_top_left.real(), app->current_bottom_right.real(),
                         rel_x / static_cast<float>(app->current_width));
  double m_down_y = lerp(app->current_top_left.imag(), app->current_bottom_right.imag(),
                         rel_y / static_cast<float>(app->current_height));
  return {m_down_x, m_down_y};
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
  auto* app = static_cast<AppContext*>(appstate);

  if (event->type == SDL_EVENT_QUIT) {
    return SDL_APP_SUCCESS;
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
      SDL_RenderClear(app->renderer);
      SDL_RenderTexture(app->renderer, app->texture, nullptr, nullptr);
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

  return SDL_APP_CONTINUE;
}

int render_pixel(complex<double> top_left, complex<double> bottom_right, double x, double y, int iterations) {
  double real = lerp(top_left.real(), bottom_right.real(), x);
  double imag = lerp(top_left.imag(), bottom_right.imag(), y);
  int escape_time = in_mandelbrot(complex<double>{real, imag}, iterations);
  Color c = escape_time_to_color(escape_time);
  return c.r | c.g << 8 | c.b << 16 | SDL_ALPHA_OPAQUE << 24;
}

void render_to_texture(SDL_Texture* texture, complex<double> top_left, complex<double> bottom_right, int iterations) {
  int* pixels = nullptr;
  int pitch_bytes;

  if (!SDL_LockTexture(texture, nullptr, reinterpret_cast<void**>(&pixels), &pitch_bytes)) {
    SDL_Log("lock texture failed");
    return;
  }

  int pitch = pitch_bytes / 4;
  int row = 0;
  int col = 0;
  float w;
  float h;
  SDL_GetTextureSize(texture, &w, &h);
  int total_pixels = static_cast<int>(w) * static_cast<int>(h);
  for (int px = 0; px < total_pixels; px++) {
    double x = static_cast<double>(col) / w;
    double y = static_cast<double>(row) / h;
    pixels[px] = render_pixel(top_left, bottom_right, x, y, iterations);

    col++;
    if (col == pitch) {
      col = 0;
      row++;
    }
  }
  SDL_UnlockTexture(texture);
}

void draw_texture(SDL_Renderer *renderer, SDL_Texture *texture) {
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
  SDL_RenderClear(renderer);

  SDL_RenderTexture(renderer, texture, nullptr, nullptr);

  SDL_RenderPresent(renderer);
  SDL_Log("Rendered texture");
}

void render(AppContext* app) {
  render_to_texture(app->texture, app->current_top_left, app->current_bottom_right, app->iterations);
  draw_texture(app->renderer, app->texture);
}

SDL_AppResult SDL_AppIterate(void* appstate) {
  auto* app = static_cast<AppContext*>(appstate);

  if (!app->first && no_change(app)) {
    return SDL_APP_CONTINUE;
  }

  app->first = false;
  app->iterations = ceil(app->iterations * 1.15);
  SDL_Log("new iterations: %d", app->iterations);

  app->previous_top_left = app->current_top_left;
  app->previous_bottom_right = app->current_bottom_right;

  render(app);
  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
  auto* app = (AppContext*)appstate;
  if (app) {
    SDL_DestroyTexture(app->texture);
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
//     SDL_Window *window = SDL_CreateWindow("Mandelbrot", 800, 600, SDL_WINDOW_HIDDEN |
//     SDL_WINDOW_RESIZABLE); if (!window) {
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
