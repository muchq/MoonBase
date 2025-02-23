#include "include/raylib.h"

int main() {
  int screenWidth = 400;
  int screenHeight = 200;

  InitWindow(screenWidth, screenHeight, "Hello, World!");

  SetTargetFPS(60);
  SetExitKey(-1); // don't exit on ESC

  while (!WindowShouldClose()) {
    BeginDrawing();
    ClearBackground(RAYWHITE);
    DrawText("hello raylib", screenWidth / 2 - 50, screenHeight / 2, 20, BLACK);
    EndDrawing();
  }

  CloseWindow();
  return 0;
}
