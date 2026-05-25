#pragma once

#include <functional>
#include <memory>
#include <vector>

struct HWND__;
using HWND = HWND__*;

namespace winelement::platform::detail {

class DispatcherState;

[[nodiscard]] std::shared_ptr<DispatcherState> ensure_current_dispatcher_state();
void request_all_dispatchers_quit(int exit_code = 0) noexcept;

class DispatcherState final {
  public:
    DispatcherState();
    ~DispatcherState();

    DispatcherState(const DispatcherState&) = delete;
    DispatcherState& operator=(const DispatcherState&) = delete;

    void post(std::function<void()> callback);
    void request_quit(int exit_code = 0) noexcept;

    [[nodiscard]] bool is_current_thread() const noexcept;
    [[nodiscard]] bool has_live_windows() const noexcept;
    [[nodiscard]] int run();

    void register_window(HWND hwnd) noexcept;
    void unregister_window(HWND hwnd) noexcept;

  private:
    void close_all_windows() noexcept;
    [[nodiscard]] std::vector<std::function<void()>> take_callbacks();
    [[nodiscard]] bool has_pending_callbacks() const noexcept;
    void run_pending_callbacks();
    void drain_pending_callbacks();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace winelement::platform::detail
