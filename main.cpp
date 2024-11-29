#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/windows/stream_handle.hpp>
#include <boost/asio.hpp>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <shared_mutex>

#include "tracy/Tracy.hpp"
#include "tracy/TracyC.h"

// https://www.reddit.com/r/cpp/comments/p132c7/comment/h8b8nml/?share_id=-NRyj9iRw5TqSi4Mm381j
template <
    class T,
    class Mutex                            = std::mutex,
    template <typename...> typename WriteLock = std::unique_lock,
    template <typename...> typename ReadLock = std::unique_lock>
struct Synchronised
{
    Synchronised()  = default;
    ~Synchronised() = default;

    explicit Synchronised(T in)
    : target(std::move(in))
    {
    }

    auto read(auto f) const
    {
        auto l = lock();
        LockMark(mutex);
        return f(target);
    }

    auto get_copy() const
    {
        return read([&](const auto& obj) { return obj; });
    }

    auto write(auto f)
    {
        auto l = lock();
        LockMark(mutex);
        return f(target);
    }

private:
    mutable TracyLockable(Mutex, mutex);

    T target;

    auto lock() const
    {
        return ReadLock<LockableBase(Mutex)>(mutex);
    }

    auto lock()
    {
        return WriteLock<LockableBase(Mutex)>(mutex);
    }
};

auto get_thread_name()
{
    static std::unordered_map<std::thread::id, std::size_t> thread_ids;

    std::thread::id this_id = std::this_thread::get_id();

    if (thread_ids.find(this_id) == thread_ids.end())
    {
        thread_ids[this_id] = thread_ids.size();
    }

    return thread_ids[this_id] == 0 ? "Main" : "Worker-" + std::to_string(thread_ids[this_id]);
}

void log(const std::string& message)
{
    static std::mutex cout_mutex;

    std::lock_guard<std::mutex> lock(cout_mutex);

    std::cout << "[" << get_thread_name() << "] " << message << std::endl;
}

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

namespace asio = boost::asio;
using namespace boost::asio::experimental::awaitable_operators;

template <typename T>
using task = asio::awaitable<T>;

enum class WhenAllShuffleMode
{
    None,
    Shuffle,
};

// Locked, shared resource
Synchronised<int> g_zone_counter{};

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

class Console
{
public:
    Console(boost::asio::io_context& io_context)
        : io_context(io_context),
#ifdef _WIN32
          input_handle(io_context, ::GetStdHandle(STD_INPUT_HANDLE))
#else
          input_handle(io_context, ::dup(STDIN_FILENO))
#endif
    {
        start_reading();
    }

    void start_reading() {
        boost::asio::async_read_until(input_handle, input_buffer, '\n',
            // Completion handler
            [this](const boost::system::error_code& error, std::size_t length) {
                this->handle_read(error, length);
            });
    }

    void handle_read(const boost::system::error_code& error, std::size_t length) {
        if (error) {
            std::cerr << "Error: " << error.message() << std::endl;
            return;
        }

        std::istream is(&input_buffer);
        std::string line;
        std::getline(is, line);

        const auto trim = [](const std::string& str) {
            std::string out;
            out.reserve(str.size());
            for (const auto& c : str) {
                if (c != '\n' && c != '\r' && c != '\t') {
                    out.push_back(c);
                }
            }
            return out;
        };

        line = trim(line);

        std::cout << "Received: " << line << std::endl;

        // Start reading again
        start_reading();
    }

private:
    boost::asio::io_context& io_context;
    boost::asio::streambuf input_buffer;

#ifdef _WIN32
    boost::asio::windows::stream_handle input_handle;
#else
    boost::asio::posix::stream_descriptor input_handle;
#endif
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

    template <typename Func>
    auto offload_work(Func&& work) -> task<std::invoke_result_t<Func>>
    {
        using result_type = std::invoke_result_t<Func>;

        TracyZoneScoped;

        // Create a promise and a future to synchronize the work
        auto promise = std::promise<result_type>();
        auto future  = promise.get_future();

        // Schedule the work in the thread pool
        asio::post(
            thread_pool_,
            [work = std::move(work), promise = std::move(promise)]() mutable
            {
                try
                {
                    if constexpr (std::is_same_v<result_type, void>)
                    {
                        work(); // Perform the actual work
                        promise.set_value();
                    }
                    else
                    {
                        result_type result = work(); // Perform the actual work
                        promise.set_value(std::move(result));
                    }
                }
                catch (...)
                {
                    promise.set_exception(std::current_exception());
                }
            });

        // Suspend the coroutine until the work completes
        co_await asio::post(io_context_, asio::use_awaitable);

        future.wait(); // Wait for the thread pool work to complete

        if constexpr (std::is_void_v<result_type>)
        {
            co_return;
        }
        else
        {
            co_return future.get(); // Return the result of the work
        }
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
    TracyZoneScoped;

    std::this_thread::sleep_for(duration_cast<std::chrono::milliseconds>(std::chrono::duration<double>(std::rand() % ms) / 100.0));
}

auto offloaded_work()
{
    TracyZoneScoped;

    log("This is offloaded work");
    random_sleep(100);

    return 42;
}

task<void> simulate_zone(int zone_id, Scheduler& scheduler)
{
    log("Simulating zone " + std::to_string(zone_id));

    // With a return value
    auto result = co_await scheduler.offload_work(offloaded_work);
    log("Result: " + std::to_string(result));

    // With void
    co_await scheduler.offload_work([&]() {
        TracyZoneScoped;

        log("This is offloaded work");

        g_zone_counter.write([](auto& counter) {
            counter++;
        });

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
    // TODO: How do I get Tracy to work here, inside a coroutine?

    log("Simulating zones");

    // Some zone-indpendent work
    co_await some_main_thread_task();

    // Spawn tasks for all zones
    std::vector<task<void>> zone_tasks;
    for (int zone_id = 0; zone_id < 32; ++zone_id)
    {
        zone_tasks.emplace_back(simulate_zone(zone_id, scheduler));
    }

    // Start executing all zone tasks cooperatively
    co_await when_all(std::move(zone_tasks));

    log("*** All zones have finished simulating ***");

    auto counter = g_zone_counter.get_copy();
    log("Zone counter: " + std::to_string(counter));
}

task<void> simulate(Scheduler& scheduler)
{
    TracyFiberEnter("Main");

    Timer t;

    // These are sequential
    for (int i = 0; i < 3; ++i)
    {
        TracyFiberLeave;
        co_await simulate_zones(scheduler);
        TracyFiberEnter("Main");
    }

    t.end();

    scheduler.get_io_context().stop();

    TracyFiberLeave;
}

int main()
{
    TracyZoneScoped;

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

    Console console(scheduler.get_io_context());

    auto signal_set = asio::signal_set(scheduler.get_io_context(), SIGINT, SIGTERM);
    signal_set.async_wait([&](const boost::system::error_code& error, int signal_number) {
        log("Received signal: " + std::to_string(signal_number));
        if (error) {
            log("Error: " + error.message());
        }
        scheduler.get_io_context().stop();
    });

    scheduler.run();

    return 0;
}
