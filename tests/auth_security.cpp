#include "AuthLimits.hpp"
#include "PasswordExecutor.hpp"
#include "RegistrationFilter.hpp"
#include <cassert>
#include <future>
#include <latch>

int main()
{
    using namespace auth_limits;
    assert(username("admin"));
    assert(username(std::string(128, 'x')));
    assert(!username(""));
    assert(!username("bad\nname"));
    assert(!username(std::string(129, 'x')));
    assert(password("old", false)); // existing accounts remain usable
    assert(!password("short", true));
    assert(!password("abcdefghij!", true));
    assert(!password("abcdefghijkl", true));
    assert(!password("abcdefghijk ", true));
    assert(password("abcdefghijk!", true));
    assert(password("abcdefghijk!", false));
    assert(!password("éééééééééé!", true));
    assert(password("ééééééééééé!", true));
    assert(!password(std::string(12, char(0x80)) + "!", true));
    assert(password(std::string(127, 'x') + "!", true));
    assert(!password(std::string(128, 'x') + "!", true));
    assert(!password(std::string(129, 'x'), false));
    assert(invite(std::string(43, 'A')));
    assert(!invite(std::string(42, 'A')));
    assert(!invite(std::string(42, 'A') + "+"));

    RegistrationLimiter limiter;
    const auto now = RegistrationLimiter::Clock::now();
    for (int ip = 0; ip < 6; ++ip)
    {
        for (int i = 0; i < 5; ++i) assert(limiter.allow(std::to_string(ip), now));
        assert(!limiter.allow(std::to_string(ip), now));
    }
    assert(!limiter.allow("new-address", now));
    assert(limiter.allow("0", now + std::chrono::minutes(1)));

    auto *loop = drogon::app().getLoop();
    const auto originalThread = std::this_thread::get_id();
    PasswordExecutor executor;
    std::latch started(2);
    std::promise<void> release;
    auto gate = release.get_future().share();
    for (int i = 0; i < 2; ++i)
        assert(executor.submit([&] { started.count_down(); gate.wait(); }));
    started.wait();
    std::atomic<int> completed{0};
    for (int i = 0; i < 8; ++i) assert(executor.submit([&] { ++completed; }));
    assert(!executor.submit([] {}));
    bool rejected = false, heartbeat = false;
    loop->queueInLoop([&]
    {
        drogon::async_run([&]() -> drogon::Task<>
        {
            try
            {
                PasswordWork<bool> work(executor, [] { return true; });
                co_await work;
                assert(false);
            }
            catch (const PasswordQueueFull &) { rejected = true; }
        });
    });
    loop->runAfter(.02, [&]
    {
        assert(rejected && completed == 0);
        heartbeat = true;
        release.set_value();
    });
    bool scheduled = false;
    loop->runEvery(.01, [&]
    {
        if (scheduled || !heartbeat || completed != 8) return;
        scheduled = true;
        drogon::async_run([&]() -> drogon::Task<>
        {
            assert(heartbeat && completed == 8);
            PasswordWork<bool> work(executor, [originalThread]
            { return std::this_thread::get_id() != originalThread; });
            assert(co_await work);
            assert(std::this_thread::get_id() == originalThread);
            try
            {
                PasswordWork<bool> fail(executor, []() -> bool { throw std::runtime_error("test"); });
                co_await fail;
                assert(false);
            }
            catch (const std::runtime_error &) {}
            loop->quit();
        });
    });
    loop->runAfter(5, [] { std::abort(); });
    loop->loop();
    std::cout << "PASS: auth validation, per-IP/global rate limits, bounded worker queue, responsive event loop, worker exceptions\n";
}
