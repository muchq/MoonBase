#include "raylib.h"

int main() {
  int screenWidth = 800;
  int screenHeight = 600;

  InitWindow(screenWidth, screenHeight, "Hello Raylib.");
  SetTargetFPS(60);

  while (!WindowShouldClose()) {
    BeginDrawing();
    ClearBackground(RAYWHITE);
    DrawText("Hello", screenWidth / 2, screenHeight / 2, 20, BLACK);
    EndDrawing();
  }

  CloseWindow();
  return 0;
}
