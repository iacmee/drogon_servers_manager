#pragma once
#include <drogon/WebSocketConnection.h>
#include <chrono>
#include <deque>
#include <mutex>

// Bounded console stream. A random ping follows each data frame; its pong
// proves that the peer has received that frame before another is queued in TCP.
class ConsoleOutput : public std::enable_shared_from_this<ConsoleOutput>
{
  public:
    explicit ConsoleOutput(const drogon::WebSocketConnectionPtr &connection);
    void enqueue(std::string_view text);
    void acknowledge(std::string_view token);

  private:
    void pump(); // mutex held
    void close(); // mutex held
    void checkTimeout();

  private:
    std::weak_ptr<drogon::WebSocketConnection> connection;
    std::mutex mutex;
    std::deque<std::string> queue;
    std::size_t bytes{0}; // includes the frame awaiting acknowledgement
    std::string pendingToken;
    bool closed{false};
    bool timerActive{false};
    std::chrono::steady_clock::time_point deadline;
};
