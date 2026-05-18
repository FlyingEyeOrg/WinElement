#pragma once

#include <functional>
#include <memory>

namespace winelement::platform {

namespace detail {
class DispatcherState;
} // namespace detail

class Dispatcher final {
  public:
    Dispatcher();
    ~Dispatcher();

    Dispatcher(const Dispatcher&) noexcept;
    Dispatcher& operator=(const Dispatcher&) noexcept;
    Dispatcher(Dispatcher&&) noexcept;
    Dispatcher& operator=(Dispatcher&&) noexcept;

    void post(std::function<void()> callback) const;
    void request_quit(int exit_code = 0) const noexcept;

    [[nodiscard]] bool is_current_thread() const noexcept;
    [[nodiscard]] int run() const;

  private:
    explicit Dispatcher(std::shared_ptr<detail::DispatcherState> state) noexcept;

    std::shared_ptr<detail::DispatcherState> state_;

    friend class Application;
};

} // namespace winelement::platform
