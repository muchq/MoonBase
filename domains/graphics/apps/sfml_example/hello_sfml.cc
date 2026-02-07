#include <SFML/Graphics.hpp>

std::vector<float> compute_scales(int32_t now_ms, int ms_per_cycle, uint8_t subdivisions) {
  std::vector<float> scales{};
  // how to scale this like a tube instead of linearly?
  // maybe a 1/x type shape? or log?
  for (int i = 0; i < subdivisions; i++) {
    scales.emplace_back(((now_ms + i * (ms_per_cycle / subdivisions)) % ms_per_cycle) /
                        static_cast<float>(ms_per_cycle));
  }

  return scales;
}

sf::CircleShape centered_circle(float r, float scale, int w, int h) {
  float radius = r / std::log(scale);
  uint8_t gray = 255 * scale;

  sf::CircleShape circle(radius, 100);
  circle.setFillColor(sf::Color::Transparent);
  circle.setOutlineThickness(1.f);
  circle.setOutlineColor(sf::Color(gray, gray, 255));
  circle.setPosition(sf::Vector2f(w / 2 - radius, h / 2 - radius));
  return circle;
}

int main() {
  // create the window
  sf::ContextSettings settings;
  settings.antiAliasingLevel = 8;
  sf::RenderWindow window(sf::VideoMode({800, 600}), "TuberProV7", sf::Style::Default,
                          sf::State::Windowed, settings);

  sf::Clock clock{};

  // run the program as long as the window is open
  while (window.isOpen()) {
    // check all the window's events that were triggered since the last iteration of the loop
    while (const std::optional event = window.pollEvent()) {
      // "close requested" event: we close the window
      if (event->is<sf::Event::Closed>()) window.close();
    }

    int32_t now_ms = clock.getElapsedTime().asMilliseconds();

    // clear the window
    window.clear(sf::Color::Black);

    // draw everything
    auto scales = compute_scales(now_ms, 5000, 15);
    for (auto scale : scales) {
      sf::CircleShape circle = centered_circle(300.0, scale, 800, 600);
      window.draw(circle);
    }

    // end the current frame
    window.display();
  }
}
