#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/this_coro.hpp>

#include <chrono>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>

#include <spdlog/spdlog.h>

#include "tracy/Tracy.hpp"
#include <cstdint>
#include <fmt/format.h>
#include <string>

#define TracyFrameMark          FrameMark
#define TracyZoneScoped         ZoneScoped
#define TracyZoneScopedN(n)     ZoneScopedN(n)
#define TracyZoneNamed(var)     ZoneNamedN(var, #var, true)
#define TracyZoneText(n, l)     ZoneText(n, l)
#define TracyZoneScopedC(c)     ZoneScopedC(c)
#define TracyZoneString(str)    ZoneText(str.c_str(), str.size())
#define TracyZoneCString(cstr)  ZoneText(cstr, std::strlen(cstr))
#define TracyMessageStr(str)    TracyMessage(str.c_str(), str.size())
#define TracySetThreadName(str) tracy::SetThreadName(str)

using namespace std::chrono_literals;

// Alias for asio::awaitable
template <typename T>
using task = asio::awaitable<T>;

auto thread_id_to_string(std::thread::id id) -> std::string
{
    std::stringstream ss;
    ss << id;
    return ss.str();
}

auto random_ms() -> std::chrono::milliseconds
{
    return 50ms + static_cast<std::chrono::milliseconds>(std::rand() % 100);
}

class Scheduler
{
public:
    Scheduler()
    : io_context_main_()
    , thread_pool_(std::max(1u, std::thread::hardware_concurrency() - 1))
    , main_executor_(io_context_main_.get_executor())
    , worker_executor_(thread_pool_.get_executor())
    , work_main_(asio::make_work_guard(io_context_main_))
    , signals_(io_context_main_, SIGINT, SIGTERM)
    {
        TracyZoneScoped;
        signals_.async_wait([&](auto, auto)
                            { io_context_main_.stop(); });
    }

    void run()
    {
        TracyZoneScoped;
        io_context_main_.run();
        thread_pool_.join();
    }

    auto run_on_main_thread(std::function<task<void>()> task_func) -> task<void>
    {
        co_return co_await asio::co_spawn(main_executor_, task_func(), asio::use_awaitable);
    }

    auto run_on_worker_thread(std::function<task<void>()> task_func) -> task<void>
    {
        co_return co_await asio::co_spawn(worker_executor_, task_func(), asio::use_awaitable);
    }

    void release_work_guard()
    {
        work_main_.reset();
    }

    auto get_main_executor() const
    {
        return main_executor_;
    }

    auto get_worker_executor() const
    {
        return worker_executor_;
    }

    task<void> when_all_par(std::vector<task<void>>&& tasks)
    {
        std::vector<std::future<void>> futures;

        for (auto& t : tasks)
        {
            futures.push_back(asio::co_spawn(worker_executor_, std::move(t), asio::use_future));
        }

        for (auto& f : futures)
        {
            f.get();
        }

        co_return;
    }

private:
    asio::io_context                                           io_context_main_;
    asio::thread_pool                                          thread_pool_;
    asio::any_io_executor                                      main_executor_;
    asio::any_io_executor                                      worker_executor_;
    asio::executor_work_guard<asio::io_context::executor_type> work_main_;
    asio::signal_set                                           signals_;
};

void blocking_sleep(std::chrono::steady_clock::duration duration)
{
    std::this_thread::sleep_for(duration);
}

auto coro_sleep(std::chrono::steady_clock::duration duration) -> task<void>
{
    auto               executor = co_await asio::this_coro::executor;
    asio::steady_timer timer(executor, duration);
    co_await timer.async_wait(asio::use_awaitable);
}

void sql_task()
{
    TracyZoneScoped;
    spdlog::info("Running SQL task on thread: {}", thread_id_to_string(std::this_thread::get_id()));
    blocking_sleep(random_ms());
}

void navmesh_task()
{
    TracyZoneScoped;
    spdlog::info("Running Navmesh task on thread: {}", thread_id_to_string(std::this_thread::get_id()));
    blocking_sleep(random_ms());
}

void rpc_task()
{
    TracyZoneScoped;
    spdlog::info("Running RPC task on thread: {}", thread_id_to_string(std::this_thread::get_id()));
    blocking_sleep(random_ms());
}

auto entity_tick() -> task<void>
{
    spdlog::info("Running entity tick on thread: {}", thread_id_to_string(std::this_thread::get_id()));
    blocking_sleep(random_ms());
    co_return;
}

auto zone_tick(Scheduler& scheduler, int i) -> task<void>
{
    spdlog::info("Running zone tick {} on thread: {}", i, thread_id_to_string(std::this_thread::get_id()));
    for (int i = 0; i < 1000; ++i)
    {
        co_await scheduler.run_on_worker_thread([]() -> task<void> {
            co_await entity_tick();
        });
    }
    co_return;
}

class Application
{
public:
    Application()
    : scheduler_()
    {
        TracyZoneScoped;
    }

    void run()
    {
        TracyZoneScoped;
        asio::co_spawn(
            scheduler_.get_main_executor(), [this]() -> task<void>
            {
                co_await main_thread_task();
                scheduler_.release_work_guard(); // Release the work guard after all tasks complete
                co_return; },
            asio::detached);
        scheduler_.run();
    }

private:
    auto main_thread_task() -> task<void>
    {
        TracyZoneScoped;

        // Timer start
        auto start = std::chrono::steady_clock::now();

        spdlog::info("Running main thread task: {}", thread_id_to_string(std::this_thread::get_id()));

        for (int i = 0; i < 200; ++i)
        {
            co_await scheduler_.run_on_worker_thread([&scheduler = scheduler_, i]() -> task<void>
                                                     {
                zone_tick(scheduler, i);
                co_return; });
        }

        // Timer end
        auto end = std::chrono::steady_clock::now();
        spdlog::info("Main thread task duration: {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

        co_return;
    }

    Scheduler scheduler_;
};

int main()
{
    TracyFrameMark;
    TracySetThreadName("Main Thread");

    Application app;
    app.run();

    return 0;
}
