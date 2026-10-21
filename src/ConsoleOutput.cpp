#include "ConsoleOutput.hpp"
#include "ConsoleLimits.hpp"
#include "SessionAccess.hpp"
#include <sodium.h>

ConsoleOutput::ConsoleOutput(const drogon::WebSocketConnectionPtr &connection) :
    m_connection(connection) {}

void ConsoleOutput::close()
{
    m_closed = true;
    m_queue.clear();
    m_bytes = 0;
    m_pendingToken.clear();
    if (auto conn = m_connection.lock())
        conn->forceClose();
}

void ConsoleOutput::enqueue(std::string_view text)
{
    std::lock_guard lock(m_mutex);
    if (m_closed || text.empty())
        return ;
    if (text.size() > console_limits::clientQueueBytes - m_bytes)
    {
        close();
        return ;
    }
    m_bytes += text.size();
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
        m_queue.emplace_back(text.substr(0, length));
        text.remove_prefix(length);
    }
    pump();
}

void ConsoleOutput::pump()
{
    if (m_closed || !m_pendingToken.empty() || m_queue.empty())
        return ;

    auto conn = m_connection.lock();
    if (!conn || !SessionAccess::allowed(conn))
    {
        close();
        return ;
    }
    unsigned char random[16];
    char encoded[33];
    randombytes_buf(random, sizeof(random));
    sodium_bin2hex(encoded, sizeof(encoded), random, sizeof(random));
    m_pendingToken = encoded;
    conn->send(m_queue.front());
    conn->send(m_pendingToken, drogon::WebSocketMessageType::Ping);
    m_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(static_cast<int>(console_limits::acknowledgementSeconds));
    if (!m_timerActive)
    {
        m_timerActive = true;
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
    std::lock_guard lock(m_mutex);
    if (m_closed || m_pendingToken.empty() || token != m_pendingToken)
        return ;
    m_bytes -= m_queue.front().size();
    m_queue.pop_front();
    m_pendingToken.clear();
    pump();
}

void ConsoleOutput::checkTimeout()
{
    std::lock_guard lock(m_mutex);
    if (m_closed || m_pendingToken.empty())
    {
        m_timerActive = false;
        return ;
    }
    const auto remaining = std::chrono::duration<double>(m_deadline - std::chrono::steady_clock::now()).count();
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
