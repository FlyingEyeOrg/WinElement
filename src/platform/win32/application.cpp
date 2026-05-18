#include <winelement/platform/application.hpp>

#include "dispatcher_internal.hpp"

#include <utility>

namespace winelement::platform {

Application::Application() = default;

Application::~Application() = default;

Application::Application(Application&&) noexcept = default;

Application& Application::operator=(Application&&) noexcept = default;

Dispatcher Application::dispatcher() const noexcept {
    return dispatcher_;
}

int Application::run() {
    return dispatcher_.run();
}

void Application::request_quit(int exit_code) noexcept {
    detail::request_all_dispatchers_quit(exit_code);
}

Dispatcher Application::current_dispatcher() {
    return Dispatcher(detail::ensure_current_dispatcher_state());
}

} // namespace winelement::platform
