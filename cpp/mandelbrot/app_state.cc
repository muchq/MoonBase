#include "app_state.h"

SDL_Renderer *AppContext::GetRenderer() { return this->renderer; }

SDL_Window *AppContext::GetWindow() { return this->window; }

SDL_Texture *AppContext::GetTexture() { return this->texture; }
