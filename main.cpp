#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_awaitable.hpp>

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

#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>

void log(const std::string& message)
{
    static std::mutex cout_mutex;

    static std::unordered_map<std::thread::id, std::size_t> thread_ids;

    std::lock_guard<std::mutex> lock(cout_mutex);

    if (thread_ids.find(std::this_thread::get_id()) == thread_ids.end())
    {
        thread_ids[std::this_thread::get_id()] = thread_ids.size();
    }

    const auto thread_id = thread_ids[std::this_thread::get_id()];

    std::string thread_name = thread_id == 0 ? "Main" : "Worker-" + std::to_string(thread_id);

    std::cout << "[" << thread_name << "] " << message << std::endl;
}

using namespace std::chrono_literals;

namespace asio = boost::asio;
using namespace boost::asio::experimental::awaitable_operators;

template <typename T>
using task = asio::awaitable<T>;

enum class WhenAllShuffleMode
{
    None,
    Shuffle,
};

asio::awaitable<void> when_all(std::vector<asio::awaitable<void>> tasks, WhenAllShuffleMode shuffle_mode = WhenAllShuffleMode::None)
{
    if (tasks.empty())
    {
        co_return;
    }

    if (shuffle_mode == WhenAllShuffleMode::Shuffle)
    {
        std::random_device rd;
        std::mt19937       g(rd());
        std::shuffle(tasks.begin(), tasks.end(), g);
    }

    auto combined = std::move(tasks[0]);
    for (std::size_t i = 1; i < tasks.size(); ++i)
    {
        combined = std::move(combined) && std::move(tasks[i]);
    }

    co_await std::move(combined);
}

class Timer
{
public:
    Timer()
    : start_time_(std::chrono::steady_clock::now())
    {
    }

    void end()
    {
        auto end_time     = std::chrono::steady_clock::now();
        auto elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time_).count();
        log("=== Timer elapsed: " + std::to_string(elapsed_time) + "ms ===");
    }

private:
    std::chrono::time_point<std::chrono::steady_clock> start_time_;
};

class Scheduler
{
public:
    struct SchedulerOptions
    {
        std::size_t thread_count = 4;
    };

    explicit Scheduler(SchedulerOptions options)
    : thread_pool_(options.thread_count)
    , io_context_()
    {
    }

    ~Scheduler()
    {
        thread_pool_.stop();
    }

    task<void> offload_work(std::function<void()> work)
    {
        // Create a promise and a future to synchronize the work
        auto promise = std::promise<void>();
        auto future  = promise.get_future();

        // Schedule the work in the thread pool
        asio::post(
            thread_pool_,
            [work = std::move(work), promise = std::move(promise)]() mutable
            {
                try
                {
                    work(); // Perform the actual work
                    promise.set_value();
                }
                catch (...)
                {
                    promise.set_exception(std::current_exception());
                }
            });

        // Suspend the coroutine until the work completes
        co_await asio::post(io_context_, asio::use_awaitable);

        future.wait(); // Wait for the thread pool work to complete
        co_return;
    }

    void run()
    {
        io_context_.run();
        thread_pool_.join();
    }

    asio::io_context& get_io_context()
    {
        return io_context_;
    }

private:
    asio::thread_pool thread_pool_;
    asio::io_context  io_context_;
};

void random_sleep(unsigned int ms)
{
    std::this_thread::sleep_for(duration_cast<std::chrono::milliseconds>(std::chrono::duration<double>(std::rand() % ms) / 100.0));
}

task<void> simulate_zone(int zone_id, Scheduler& scheduler)
{
    TracyZoneScoped;

    log("Simulating zone " + std::to_string(zone_id));

    co_await scheduler.offload_work(
        [zone_id]
        {
            TracyZoneScoped;
            log("Simulating offloaded workload for zone " + std::to_string(zone_id));
            random_sleep(100);
        });

    log("Zone " + std::to_string(zone_id) + " simulation complete");
    co_return;
}

task<void> some_main_thread_task()
{
    TracyZoneScoped;

    log("This is a main thread task");
    random_sleep(20);

    co_return;
}

task<void> simulate_zones(Scheduler& scheduler)
{
    TracyZoneScoped;

    log("Simulating zones");

    // Some zone-indpendent work
    co_await some_main_thread_task();

    // Spawn tasks for all zones
    std::vector<task<void>> zone_tasks;
    for (int zone_id = 0; zone_id < 30; ++zone_id)
    {
        zone_tasks.emplace_back(simulate_zone(zone_id, scheduler));
    }

    // Start executing all zone tasks cooperatively
    co_await when_all(std::move(zone_tasks));

    log("*** All zones have finished simulating ***");
}

task<void> simulate(Scheduler& scheduler)
{
    Timer t;

    for (int i = 0; i < 3; ++i)
    {
        TracyFrameMark;
        TracySetThreadName("Main Thread");
        TracyZoneScoped;

        co_await simulate_zones(scheduler);
    }

    t.end();
}

int main()
{
    Scheduler scheduler({
        .thread_count = 32,
    });

    std::vector<task<void>> tasks;
    tasks.emplace_back(simulate(scheduler));

    asio::co_spawn(
        scheduler.get_io_context(),
        when_all(std::move(tasks)),
        [](std::exception_ptr eptr)
        {
            if (eptr)
            {
                try
                {
                    std::rethrow_exception(eptr);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Exception: " << e.what() << std::endl;
                }
            }
        });

    scheduler.run();

    return 0;
}
