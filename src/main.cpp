#include "app.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  try {
    torrview::Application app;
    app.initialize();
    app.run();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }

  return 0;
}
