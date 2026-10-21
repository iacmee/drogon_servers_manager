#include "ConsoleOutput.hpp"
#include "ConsoleLimits.hpp"
#include "SessionAccess.hpp"
#include <sodium.h>

ConsoleOutput::ConsoleOutput(const drogon::WebSocketConnectionPtr &connection) :
    connection(connection) {}

void ConsoleOutput::close()
{
    closed = true;
    queue.clear();
    bytes = 0;
    pendingToken.clear();
    if (auto conn = connection.lock())
        conn->forceClose();
}

void ConsoleOutput::enqueue(std::string_view text)
{
    std::lock_guard lock(mutex);
    if (closed || text.empty())
        return ;
    if (text.size() > console_limits::clientQueueBytes - bytes)
    {
        close();
        return ;
    }
    bytes += text.size();
    while (!text.empty())
    {
        auto length = std::min(text.size(), console_limits::outputChunk);
        // Keep UTF-8 code points together when splitting a log frame.
        if (length < text.size())
            while (length > 0 && (static_cast<unsigned char>(text[length]) & 0xc0) == 0x80)
                --length;
        if (length == 0)
        {
            close();
            return;
        }
        queue.emplace_back(text.substr(0, length));
        text.remove_prefix(length);
    }
    pump();
}

void ConsoleOutput::pump()
{
    if (closed || !pendingToken.empty() || queue.empty())
        return ;

    auto conn = connection.lock();
    if (!conn || !SessionAccess::allowed(conn))
    {
        close();
        return ;
    }
    unsigned char random[16];
    char encoded[33];
    randombytes_buf(random, sizeof(random));
    sodium_bin2hex(encoded, sizeof(encoded), random, sizeof(random));
    pendingToken = encoded;
    conn->send(queue.front());
    conn->send(pendingToken, drogon::WebSocketMessageType::Ping);
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(static_cast<int>(console_limits::acknowledgementSeconds));
    if (!timerActive)
    {
        timerActive = true;
        drogon::app().getLoop()->runAfter(console_limits::acknowledgementSeconds,
            [weak = weak_from_this()]
            {
                if (auto output = weak.lock())
                    output->checkTimeout();
            });
    }
}

void ConsoleOutput::acknowledge(std::string_view token)
{
    std::lock_guard lock(mutex);
    if (closed || pendingToken.empty() || token != pendingToken)
        return ;
    bytes -= queue.front().size();
    queue.pop_front();
    pendingToken.clear();
    pump();
}

void ConsoleOutput::checkTimeout()
{
    std::lock_guard lock(mutex);
    if (closed || pendingToken.empty())
    {
        timerActive = false;
        return ;
    }
    const auto remaining = std::chrono::duration<double>(deadline - std::chrono::steady_clock::now()).count();
    if (remaining <= 0)
    {
        close();
        return ;
    }
    drogon::app().getLoop()->runAfter(remaining, [weak = weak_from_this()]
    {
        if (auto output = weak.lock())
            output->checkTimeout();
    });
}
