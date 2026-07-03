#include "app.hpp"

#include "logger.hpp"

#include <exception>

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  try {
    torrview::Application app;
    app.initialize();
    app.run();
  } catch (const std::exception& error) {
    TORRVIEW_LOG_ERROR(error.what());
    return 1;
  }

  return 0;
}
