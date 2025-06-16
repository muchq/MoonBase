#include <GLFW/glfw3.h>

#include <iostream>

void error_callback(int error, const char* description) {
  fprintf(stderr, "Error: %s\n", description);
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) glfwSetWindowShouldClose(window, GLFW_TRUE);
}

int main() {
  // glfw: initialize and configure
  // OpenGL 4.1 is the newest version supported on my mac.
  // Forward compatibility mode is required for some reason.
  // ------------------------------
  if (!glfwInit()) {
    exit(EXIT_FAILURE);
  }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, true);
  glfwSetErrorCallback(error_callback);

  // glfw window creation
  // --------------------
  GLFWwindow* window = glfwCreateWindow(600, 400, "HelloGL", NULL, NULL);
  if (window == NULL) {
    std::cerr << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    return -1;
  }

  glfwSetKeyCallback(window, key_callback);
  glfwMakeContextCurrent(window);

  // avoid screen tearing https://en.wikipedia.org/wiki/Screen_tearing
  glfwSwapInterval(1);
  // glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
  // glfwSetCursorPosCallback(window, mouse_callback);
  // glfwSetScrollCallback(window, scroll_callback);

  // tell GLFW to capture our mouse
  // glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

  // render loop
  // -----------
  while (!glfwWindowShouldClose(window)) {
    // per-frame time logic
    // --------------------
    // float currentFrame = static_cast<float>(glfwGetTime());
    // deltaTime = currentFrame - lastFrame;
    // lastFrame = currentFrame;

    // input
    // -----
    // processInput(window);

    // render
    // ------
    //      glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    //      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    //
    //      profiler p;
    //      RenderText(shader, "This is sample text", 25.0f, 25.0f, 1.0f, glm::vec3(0.5, 0.8f,
    //      0.2f)); fprintf(stderr, "%lld\n", p.get_micros()); RenderText(shader, "(C)
    //      LearnOpenGL.com", 1300.0f, 1140.0f, 0.5f, glm::vec3(0.3, 0.7f, 0.9f)); fprintf(stderr,
    //      "%lld\n", p.get_micros());

    // glfw: swap buffers and poll IO events (keys pressed/released, mouse moved etc.)
    // -------------------------------------------------------------------------------
    glfwSwapBuffers(window);
    glfwPollEvents();
  }

  glfwDestroyWindow(window);
  glfwTerminate();
  exit(EXIT_SUCCESS);
}
