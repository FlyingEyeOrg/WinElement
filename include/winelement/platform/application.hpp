#pragma once

#include <winelement/platform/dispatcher.hpp>

namespace winelement::platform {

class Application final {
  public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) noexcept;
    Application& operator=(Application&&) noexcept;

    [[nodiscard]] Dispatcher dispatcher() const noexcept;

    [[nodiscard]] int run();
    void request_quit(int exit_code = 0) noexcept;

    [[nodiscard]] static Dispatcher current_dispatcher();

  private:
    Dispatcher dispatcher_;
};

} // namespace winelement::platform
