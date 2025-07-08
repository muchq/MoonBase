#ifndef CPP_TRILL_TRILL_H
#define CPP_TRILL_TRILL_H

#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>

namespace trill {
class AppContextInterface {
 public:
  virtual ~AppContextInterface() = default;
  virtual SDL_Window* GetWindow() = 0;
  virtual SDL_Renderer* GetRenderer() = 0;
  virtual SDL_Texture* GetTexture() = 0;
  virtual bool IsChanged() = 0;
};
}  // namespace trill

#endif
