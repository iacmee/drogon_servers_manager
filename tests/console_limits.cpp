#include "ConsoleLimits.hpp"
#include "ConsoleOutput.hpp"
#include "MinecraftConsole.hpp"
#include "SessionAccess.hpp"
#include <cassert>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

class TestConnection : public drogon::WebSocketConnection
{
  public:
    std::vector<std::pair<std::string, drogon::WebSocketMessageType>> m_sent;
    bool m_open{true};
    trantor::InetAddress m_address;
    void send(const char *data, uint64_t size, drogon::WebSocketMessageType type) override
    { send(std::string_view(data, size), type); }
    void send(std::string_view data, drogon::WebSocketMessageType type) override
    { m_sent.emplace_back(data, type); }
    void sendJson(const Json::Value &, drogon::WebSocketMessageType) override { assert(false); }
    const trantor::InetAddress &localAddr() const override { return m_address; }
    const trantor::InetAddress &peerAddr() const override { return m_address; }
    bool connected() const override { return m_open; }
    bool disconnected() const override { return !m_open; }
    void shutdown(drogon::CloseCode, const std::string &) override { m_open = false; }
    void forceClose() override { m_open = false; }
    void setPingMessage(const std::string &, const std::chrono::duration<double> &) override {}
    void disablePing() override {}
};

int main()
{
    using namespace console_limits;
    assert(validCommand("say hello"));
    assert(validCommand(std::string(commandBytes, 'x')));
    for (const auto &bad : {std::string(), std::string("  "), std::string("stop\nsay hi"),
                           std::string("stop\r"), std::string("x\0y", 3), std::string(commandBytes + 1, 'x')})
        assert(!validCommand(bad));

    auto access = std::make_shared<SessionAccess>(std::chrono::seconds(60));
    for (int i = 0; i < 5; ++i)
        assert(access->allowCommand());
    assert(!access->allowCommand());
    std::this_thread::sleep_for(std::chrono::milliseconds(220));
    assert(access->allowCommand());
    access->revoke();
    assert(!access->allowCommand());

    auto connection = std::make_shared<TestConnection>();
    connection->setContext(std::make_shared<SessionAccess>(std::chrono::seconds(60)));
    auto output = std::make_shared<ConsoleOutput>(connection);
    output->enqueue("first\n");
    assert(connection->m_sent.size() == 2);
    assert(connection->m_sent[1].second == drogon::WebSocketMessageType::Ping);
    const auto token = connection->m_sent[1].first;
    output->enqueue("second\n");
    output->acknowledge("forged");
    assert(connection->m_sent.size() == 2);
    output->acknowledge(token);
    assert(connection->m_sent.size() == 4);
    assert(connection->m_sent[2].first == "second\n");
    output->acknowledge(token); // replay must not release the next frame
    assert(connection->m_sent.size() == 4);
    output->enqueue(std::string(clientQueueBytes, 'x'));
    assert(!connection->m_open);
    assert(connection->m_sent.size() == 4); // backlog never reached TCP

    auto slow = std::make_shared<TestConnection>();
    slow->setContext(std::make_shared<SessionAccess>(std::chrono::seconds(60)));
    auto silentOutput = std::make_shared<ConsoleOutput>(slow);
    silentOutput->enqueue("no acknowledgement\n");

    int commands[2], logs[2];
    assert(pipe2(commands, O_NONBLOCK | O_CLOEXEC) == 0);
    assert(pipe2(logs, O_CLOEXEC) == 0);
    std::string fill(4096, 'f');
    while (write(commands[1], fill.data(), fill.size()) > 0) {}
    assert(errno == EAGAIN);
    MinecraftConsole console;
    console.m_writeFd = commands[1];
    console.m_readFd = logs[0];
    auto *loop = drogon::app().getLoop();
    std::size_t busy = 0, unavailable = 0;
    bool truncationObserved = false;
    auto reject = [&](std::string_view error)
    {
        if (error.find("queue is full") != std::string_view::npos)
            ++busy;
        else if (error.find("unavailable") != std::string_view::npos)
            ++unavailable;
        else
            assert(false);
    };
    console.setOutputCallback([&](std::string_view text)
    {
        truncationObserved |= text.find("overlong log line") != std::string_view::npos;
    });
    console.attachToEventLoop();
    for (int i = 0; i < 100; ++i)
        console.sendCommand(std::string(commandBytes, 'x'), [] { return true; }, reject);
    assert(busy == 85); // 15 * 4097 bytes reserved, including queued loop tasks
    bool secondWave = false;
    loop->runAfter(.05, [&]
    {
        console.sendCommand(std::string(commandBytes, 'x'), [] { return true; }, reject);
        assert(busy == 86); // blocked pipe: queued bytes were not discarded
        char buffer[8192];
        while (read(commands[0], buffer, sizeof(buffer)) > 0) {}
        secondWave = true;
    });
    std::atomic<bool> writerDone{false};
    std::thread writer([&]
    {
        std::string data(2 * 1024 * 1024, 'L'); // no newline: must stay bounded
        data += "\nlast line\n";
        std::size_t offset = 0;
        while (offset < data.size())
        {
            const auto n = write(logs[1], data.data() + offset, data.size() - offset);
            assert(n > 0);
            offset += static_cast<std::size_t>(n);
        }
        writerDone = true;
    });
    bool pipeDone = false;
    std::string receivedCommands;
    bool recoveryCommand = false;
    loop->runEvery(.02, [&]
    {
        if (pipeDone)
            return;
        if (secondWave)
        {
            char buffer[8192];
            ssize_t count;
            while ((count = read(commands[0], buffer, sizeof(buffer))) > 0)
                receivedCommands.append(buffer, count);
            if (receivedCommands.size() == 15 * (commandBytes + 1) && !recoveryCommand)
            {
                assert(std::count(receivedCommands.begin(), receivedCommands.end(), '\n') == 15);
                console.sendCommand("list", [] { return true; }, reject);
                recoveryCommand = true;
            }
        }
        const auto history = console.getLast100Lines();
        assert(history.size() <= historyBytes);
        if (writerDone && recoveryCommand && receivedCommands.ends_with("list\n") && history.ends_with("last line\n"))
        {
            assert(truncationObserved);
            console.detachFromEventLoop();
            console.sendCommand(std::string(commandBytes, 'x'), [] { return true; }, reject);
            loop->queueInLoop([&]
            {
                assert(unavailable == 1); // detached consoles report an error
                pipeDone = true;
            });
        }
    });
    loop->runAfter(acknowledgementSeconds + .1, [&]
    {
        assert(pipeDone);
        assert(!slow->m_open); // a silent peer is disconnected even without new logs
        loop->quit();
    });
    loop->runAfter(15, [] { std::abort(); }); // fail rather than hang
    loop->loop();
    writer.join();
    close(commands[0]);
    close(logs[1]);
    std::cout << "PASS: command validation, rate limit, bounded output acknowledgements, blocked pipe, long log lines\n";
}
