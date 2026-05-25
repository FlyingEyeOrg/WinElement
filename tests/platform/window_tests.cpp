#include <winelement/controls.hpp>
#include <winelement/platform.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

using namespace winelement;

class ThreadRecordingElement final : public elements::UIElement {
  public:
    ThreadRecordingElement(std::atomic_bool& painted, std::thread::id& paint_thread_id,
                           std::mutex& mutex) noexcept
        : painted_(painted), paint_thread_id_(paint_thread_id), mutex_(mutex) {}

  protected:
    void on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const override {
        elements::UIElement::on_paint(context, absolute_frame);
        {
            const std::scoped_lock lock(mutex_);
            paint_thread_id_ = std::this_thread::get_id();
        }
        painted_.store(true, std::memory_order_release);
    }

  private:
    std::atomic_bool& painted_;
    std::thread::id& paint_thread_id_;
    std::mutex& mutex_;
};

class LowLatencyDragElement final : public elements::UIElement {
  public:
    explicit LowLatencyDragElement(std::atomic_int& paint_count) noexcept
        : paint_count_(paint_count) {}

  protected:
    void on_pointer_event(elements::PointerEvent& event) override {
        if (event.kind == elements::PointerEventKind::Down &&
            event.button == elements::PointerButton::Primary) {
            capture_pointer();
            event.handled = true;
            return;
        }

        if (event.kind == elements::PointerEventKind::Move && event.primary_button_down) {
            invalidate_paint();
            event.handled = true;
        }
    }

    void on_paint(rendering::RenderContext& context, layout::Rect absolute_frame) const override {
        elements::UIElement::on_paint(context, absolute_frame);
        paint_count_.fetch_add(1, std::memory_order_acq_rel);
    }

  private:
    std::atomic_int& paint_count_;
};

TEST(WindowTests, RenderThreadPoolPlannerSupportsConcurrentLeaseLifecycle) {
    platform::RenderThreadPoolPlanner planner(3U);
    auto workers = std::vector<std::thread>{};

    for (auto thread_index = 0; thread_index < 8; ++thread_index) {
        workers.emplace_back([&planner, thread_index] {
            for (auto iteration = 0; iteration < 64; ++iteration) {
                const auto window_id =
                    static_cast<std::uint64_t>(thread_index * 1000 + iteration + 1);
                const auto lease = planner.lease(window_id);
                EXPECT_LT(lease.thread_index, planner.max_threads());
                planner.release(window_id);
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    EXPECT_EQ(planner.active_window_count(), 0U);
}

TEST(WindowTests, CreatesMultipleIndependentWindows) {
    platform::Window first(
        platform::WindowOptions{.title = L"WinElement Test A", .width = 320, .height = 240});
    platform::Window second(
        platform::WindowOptions{.title = L"WinElement Test B", .width = 480, .height = 320});

    EXPECT_TRUE(first.is_open());
    EXPECT_TRUE(second.is_open());
    EXPECT_NE(&first.layout_engine(), &second.layout_engine());

    first.close();
    EXPECT_FALSE(first.is_open());
    EXPECT_TRUE(second.is_open());

    second.close();
    EXPECT_FALSE(second.is_open());
}

TEST(WindowTests, KeepsContentIsolatedPerWindow) {
    platform::Window first(
        platform::WindowOptions{.title = L"WinElement Content A", .width = 320, .height = 240});
    platform::Window second(
        platform::WindowOptions{.title = L"WinElement Content B", .width = 320, .height = 240});

    auto first_content = std::make_unique<controls::Panel>();
    auto* first_content_ptr = first_content.get();
    auto second_content = std::make_unique<controls::Panel>();
    auto* second_content_ptr = second_content.get();

    first.set_content(std::move(first_content));
    second.set_content(std::move(second_content));

    EXPECT_EQ(first.content(), first_content_ptr);
    EXPECT_EQ(second.content(), second_content_ptr);
    EXPECT_NE(first.content(), second.content());

    first.set_content(nullptr);
    EXPECT_EQ(first.content(), nullptr);
    EXPECT_EQ(second.content(), second_content_ptr);

    first.close();
    second.close();
}

TEST(WindowTests, BindsDetachedContentTreeWithoutExplicitEngine) {
    platform::Window window(platform::WindowOptions{
        .title = L"WinElement Detached Content", .width = 320, .height = 240});

    auto root = std::make_unique<controls::Panel>();
    auto* root_ptr = root.get();
    auto child = std::make_unique<controls::Button>();
    auto* child_ptr = child.get();
    root->append_child(std::move(child));

    window.set_content(std::move(root));

    ASSERT_EQ(window.content(), root_ptr);
    ASSERT_EQ(window.content()->child_count(), 1U);
    EXPECT_EQ(&window.content()->child_at(0), child_ptr);

    window.close();
}

TEST(WindowTests, SupportsCustomCreateParametersAndNativeMessageHooks) {
#ifdef _WIN32
    std::atomic_int pre_hook_hits = 0;
    std::atomic_int post_hook_hits = 0;
    std::atomic_int observer_hits = 0;
    std::atomic_int closed_hits = 0;
    std::atomic_int option_closed_hits = 0;
    platform::Window window(platform::WindowOptions{
        .title = L"WinElement Hook Source",
        .width = 320,
        .height = 200,
        .on_before_create =
            [](platform::WindowCreateParams& params) {
                params.title = L"WinElement Hooked Window";
                params.width = 420;
            },
        .on_message =
            [&pre_hook_hits](platform::WindowMessage& message) {
                if (message.id == WM_APP + 45U) {
                    pre_hook_hits.fetch_add(1, std::memory_order_acq_rel);
                    message.handled = true;
                    message.result = 145;
                }
            },
        .on_post_message =
            [&post_hook_hits](platform::WindowMessage& message) {
                if (message.id == WM_APP + 46U) {
                    post_hook_hits.fetch_add(1, std::memory_order_acq_rel);
                    message.handled = true;
                    message.result = 146;
                }
            },
        .on_closed =
            [&option_closed_hits] { option_closed_hits.fetch_add(1, std::memory_order_acq_rel); }});
    const auto pre_option_token =
        window.add_window_message_filter([&pre_hook_hits](platform::WindowMessage& message) {
            if (message.id == WM_APP + 41U) {
                pre_hook_hits.fetch_add(1, std::memory_order_acq_rel);
                message.handled = true;
                message.result = 91;
            }
        });
    const auto post_option_token =
        window.add_post_window_message_filter([&post_hook_hits](platform::WindowMessage& message) {
            if (message.id == WM_APP + 42U) {
                post_hook_hits.fetch_add(1, std::memory_order_acq_rel);
                message.handled = true;
                message.result = 123;
            }
        });
    const auto pre_filter_token =
        window.add_window_message_filter([&pre_hook_hits](platform::WindowMessage& message) {
            if (message.id == WM_APP + 43U) {
                pre_hook_hits.fetch_add(1, std::memory_order_acq_rel);
                message.handled = true;
                message.result = 77;
            }
        });
    const auto observer_token =
        (window.window_message_observers() += [&observer_hits](platform::WindowMessage& message) {
            if (message.id == WM_APP + 44U) {
                observer_hits.fetch_add(1, std::memory_order_acq_rel);
                message.handled = true;
                message.result = 66;
            }
        });
    const auto closed_token = (window.closed_event() += [&closed_hits]() {
        closed_hits.fetch_add(1, std::memory_order_acq_rel);
    });

    auto* hwnd = static_cast<HWND>(window.native_handle());
    ASSERT_NE(hwnd, nullptr);
    EXPECT_EQ(FindWindowW(L"WinElementWindow", L"WinElement Hooked Window"), hwnd);

    EXPECT_EQ(SendMessageW(hwnd, WM_APP + 41U, 0, 0), 91);
    EXPECT_EQ(pre_hook_hits.load(std::memory_order_acquire), 1);
    EXPECT_EQ(SendMessageW(hwnd, WM_APP + 42U, 0, 0), 123);
    EXPECT_EQ(post_hook_hits.load(std::memory_order_acquire), 1);
    EXPECT_EQ(SendMessageW(hwnd, WM_APP + 43U, 0, 0), 77);
    EXPECT_EQ(SendMessageW(hwnd, WM_APP + 45U, 0, 0), 145);
    EXPECT_EQ(SendMessageW(hwnd, WM_APP + 44U, 0, 0), 66);
    EXPECT_EQ(observer_hits.load(std::memory_order_acquire), 1);
    EXPECT_EQ(SendMessageW(hwnd, WM_APP + 46U, 0, 0), 146);

    window.remove_window_message_filter(pre_filter_token);
    window.remove_window_message_filter(pre_option_token);
    window.remove_post_window_message_filter(post_option_token);
    window.window_message_observers() -= observer_token;
    window.closed_event() -= closed_token;
    window.close();
    EXPECT_EQ(closed_hits.load(std::memory_order_acquire), 0);
    EXPECT_EQ(option_closed_hits.load(std::memory_order_acquire), 1);
#else
    GTEST_SKIP() << "Native hook test is only available on Win32.";
#endif
}

TEST(WindowTests, ModalWindowDisablesOwnerUntilClose) {
#ifdef _WIN32
    const auto owner_title = std::wstring{L"WinElement Modal Owner"};
    const auto dialog_title = std::wstring{L"WinElement Modal Child"};
    platform::Window owner(
        platform::WindowOptions{.title = owner_title, .width = 360, .height = 240});
    platform::Window dialog(platform::WindowOptions{
        .title = dialog_title, .width = 320, .height = 180, .owner = &owner, .modal = true});

    owner.show();
    dialog.show();

    auto* owner_hwnd = FindWindowW(L"WinElementWindow", owner_title.c_str());
    auto* dialog_hwnd = FindWindowW(L"WinElementWindow", dialog_title.c_str());
    ASSERT_NE(owner_hwnd, nullptr);
    ASSERT_NE(dialog_hwnd, nullptr);
    EXPECT_FALSE(IsWindowEnabled(owner_hwnd));

    dialog.close();
    EXPECT_TRUE(IsWindowEnabled(owner_hwnd));

    owner.close();
#else
    GTEST_SKIP() << "Modal owner test is only available on Win32.";
#endif
}

TEST(WindowTests, MessageLoopReturnsWhenNoWindowsAreLive) {
    {
        platform::Window first(platform::WindowOptions{.title = L"WinElement Loop A"});
        platform::Window second(platform::WindowOptions{.title = L"WinElement Loop B"});
        first.close();
        second.close();
    }

    EXPECT_EQ(platform::Window::run_message_loop(), 0);
}

TEST(WindowTests, ApplicationRunReturnsAfterAsyncQuitFromAnotherThread) {
    platform::Application application;
    platform::Window window(platform::WindowOptions{.title = L"WinElement Async Quit"});
    auto dispatcher = application.dispatcher();

    std::thread worker([dispatcher] {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        dispatcher.request_quit(7);
    });

    EXPECT_EQ(application.run(), 7);
    worker.join();
    EXPECT_FALSE(window.is_open());
}

TEST(WindowTests, DispatcherPostRunsCallbackOnOwningUiThread) {
    platform::Application application;
    platform::Window window(platform::WindowOptions{.title = L"WinElement Dispatcher Post"});
    auto dispatcher = application.dispatcher();
    std::atomic_bool callback_ran = false;

    std::thread worker([dispatcher, &callback_ran] {
        dispatcher.post([dispatcher, &callback_ran] {
            callback_ran.store(dispatcher.is_current_thread(), std::memory_order_release);
            dispatcher.request_quit(3);
        });
    });

    EXPECT_EQ(application.run(), 3);
    worker.join();
    EXPECT_TRUE(callback_ran.load(std::memory_order_acquire));
    EXPECT_FALSE(window.is_open());
}

TEST(WindowTests, UiFramePipelineRunsLayoutAndRecordOnDedicatedThread) {
    platform::Application application;
    platform::Window window(platform::WindowOptions{.title = L"WinElement UI Pipeline"});
    auto dispatcher = application.dispatcher();
    const auto window_thread_id = std::this_thread::get_id();

    std::mutex thread_id_mutex;
    std::thread::id layout_thread_id;
    std::thread::id paint_thread_id;
    std::atomic_bool layout_measured = false;
    std::atomic_bool paint_recorded = false;

    auto root = std::make_unique<controls::Panel>();
    auto recorder =
        std::make_unique<ThreadRecordingElement>(paint_recorded, paint_thread_id, thread_id_mutex);
    recorder->set_measure_callback([&](const layout::MeasureInput&) {
        {
            const std::scoped_lock lock(thread_id_mutex);
            layout_thread_id = std::this_thread::get_id();
        }
        layout_measured.store(true, std::memory_order_release);
        return layout::Size{120.0F, 80.0F};
    });
    root->append_child(std::move(recorder));
    window.set_content(std::move(root));
    window.show();

    std::thread quitter([dispatcher, &layout_measured, &paint_recorded] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline) {
            if (layout_measured.load(std::memory_order_acquire) &&
                paint_recorded.load(std::memory_order_acquire)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        dispatcher.request_quit(layout_measured.load(std::memory_order_acquire) &&
                                        paint_recorded.load(std::memory_order_acquire)
                                    ? 0
                                    : 9);
    });

    EXPECT_EQ(application.run(), 0);
    quitter.join();

    const std::scoped_lock lock(thread_id_mutex);
    EXPECT_NE(layout_thread_id, std::thread::id{});
    EXPECT_NE(paint_thread_id, std::thread::id{});
    EXPECT_NE(layout_thread_id, window_thread_id);
    EXPECT_NE(paint_thread_id, window_thread_id);
    EXPECT_EQ(layout_thread_id, paint_thread_id);
    EXPECT_FALSE(window.is_open());
}

TEST(WindowTests, HandledPointerDragFlushesFrameSynchronously) {
#ifdef _WIN32
    platform::Application application;
    platform::Window window(platform::WindowOptions{
        .title = L"WinElement Low Latency Drag", .width = 320, .height = 240});
    auto dispatcher = application.dispatcher();
    auto* hwnd = static_cast<HWND>(window.native_handle());
    ASSERT_NE(hwnd, nullptr);

    std::atomic_int paint_count = 0;
    std::atomic_bool drag_move_painted = false;
    auto content = std::make_unique<LowLatencyDragElement>(paint_count);
    content->set_measure_callback(
        [](const layout::MeasureInput&) { return layout::Size{200.0F, 120.0F}; });
    window.set_content(std::move(content));
    window.show();

    std::thread driver([dispatcher, hwnd, &paint_count, &drag_move_painted] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline &&
               paint_count.load(std::memory_order_acquire) == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        dispatcher.post([dispatcher, hwnd, &paint_count, &drag_move_painted] {
            if (paint_count.load(std::memory_order_acquire) == 0) {
                dispatcher.request_quit(9);
                return;
            }

            const auto before_drag = paint_count.load(std::memory_order_acquire);
            static_cast<void>(SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(16, 16)));
            static_cast<void>(SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(64, 48)));
            drag_move_painted.store(paint_count.load(std::memory_order_acquire) > before_drag,
                                    std::memory_order_release);
            dispatcher.request_quit(drag_move_painted.load(std::memory_order_acquire) ? 0 : 8);
        });
    });

    EXPECT_EQ(application.run(), 0);
    driver.join();
    EXPECT_TRUE(drag_move_painted.load(std::memory_order_acquire));
    EXPECT_FALSE(window.is_open());
#else
    GTEST_SKIP() << "Low latency drag flush test is only available on Win32.";
#endif
}

TEST(WindowTests, ApplicationRequestQuitBroadcastsAcrossUiThreads) {
    std::promise<platform::Dispatcher> worker_dispatcher_promise;
    auto worker_dispatcher_future = worker_dispatcher_promise.get_future();
    std::atomic_int worker_exit_code = -1;
    std::atomic_bool worker_window_open_after_run = true;

    std::thread ui_thread([&] {
        platform::Application worker_application;
        platform::Window worker_window(
            platform::WindowOptions{.title = L"WinElement Worker UI Thread"});
        worker_dispatcher_promise.set_value(worker_application.dispatcher());

        worker_exit_code.store(worker_application.run(), std::memory_order_release);
        worker_window_open_after_run.store(worker_window.is_open(), std::memory_order_release);
    });

    auto worker_dispatcher = worker_dispatcher_future.get();
    platform::Application application;
    platform::Window window(platform::WindowOptions{.title = L"WinElement Main UI Thread"});

    std::thread quitter([&application, worker_dispatcher] {
        worker_dispatcher.post([] {});
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        application.request_quit(11);
    });

    EXPECT_EQ(application.run(), 11);
    quitter.join();
    ui_thread.join();

    EXPECT_FALSE(window.is_open());
    EXPECT_FALSE(worker_window_open_after_run.load(std::memory_order_acquire));
    EXPECT_EQ(worker_exit_code.load(std::memory_order_acquire), 11);
}

class RecordingTextServiceClient final : public platform::TextServiceClient {
  public:
    void update_composition(platform::TextCompositionState state) override {
        composition = std::move(state);
    }

    void commit_text(std::string_view text) override {
        committed = std::string(text);
    }

    void cancel_composition() noexcept override {
        cancelled = true;
    }

    platform::TextCompositionState composition;
    std::string committed;
    bool cancelled = false;
};

TEST(WindowTests, PlatformFoundationPlansSharedRenderThreadsAndTextServices) {
    platform::RenderThreadPoolPlanner pool(2U);
    const auto first = pool.lease(10U);
    const auto second = pool.lease(11U);
    const auto third = pool.lease(12U);

    EXPECT_EQ(first.thread_index, 0U);
    EXPECT_EQ(second.thread_index, 1U);
    EXPECT_EQ(third.thread_index, 0U);
    EXPECT_EQ(pool.active_window_count(), 3U);
    pool.release(11U);
    EXPECT_EQ(pool.active_window_count(), 2U);

    platform::VSyncFrameClock clock;
    EXPECT_EQ(clock.next_frame().frame_id, 1U);
    clock.set_visible(false);
    EXPECT_EQ(clock.next_frame().frame_id, 1U);

    RecordingTextServiceClient client;
    platform::TextServiceAdapter adapter;
    adapter.attach(client);
    adapter.update_composition(platform::TextCompositionState{.text = "ime", .active = true});
    adapter.commit_text("text");
    adapter.cancel_composition();

    EXPECT_TRUE(adapter.attached());
    EXPECT_EQ(client.composition.text, "ime");
    EXPECT_EQ(client.committed, "text");
    EXPECT_TRUE(client.cancelled);
}

TEST(WindowTests, RenderThreadPoolServiceRunsJobsAndParallelWork) {
    platform::RenderThreadPoolService pool(3U);
    std::atomic_int job_count = 0;
    std::atomic_int pool_thread_hits = 0;

    auto futures = std::vector<std::future<void>>{};
    futures.reserve(24U);
    for (auto index = 0; index < 24; ++index) {
        futures.push_back(pool.submit([&pool, &job_count, &pool_thread_hits]() {
            job_count.fetch_add(1, std::memory_order_acq_rel);
            if (pool.running_on_pool_thread()) {
                pool_thread_hits.fetch_add(1, std::memory_order_acq_rel);
            }
        }));
    }

    for (auto& future : futures) {
        future.get();
    }

    std::atomic<std::uint64_t> sum = 0U;
    pool.parallel_for(64U, 4U, [&sum](std::size_t first, std::size_t last) {
        for (auto value = first; value < last; ++value) {
            sum.fetch_add(static_cast<std::uint64_t>(value + 1U), std::memory_order_acq_rel);
        }
    });
    pool.wait_idle();

    EXPECT_EQ(pool.worker_count(), 3U);
    EXPECT_EQ(job_count.load(std::memory_order_acquire), 24);
    EXPECT_GT(pool_thread_hits.load(std::memory_order_acquire), 0);
    EXPECT_EQ(sum.load(std::memory_order_acquire), 2080U);
    EXPECT_EQ(pool.queued_job_count(), 0U);
}

} // namespace
