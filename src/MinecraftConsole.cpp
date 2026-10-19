#include "MinecraftConsole.hpp"
#include <fcntl.h>
// #include <utility>
// #include <memory>
// #include <unistd.h>


void MinecraftConsole::setNonBlock()
{
    int r;
    if ((r = fcntl(m_writeFd, F_GETFL)) == -1)
        throw std::runtime_error(std::string("fcntl(F_GETFL): ") + strerror(errno));
    if (fcntl(m_writeFd, F_SETFL, r | O_NONBLOCK) == -1)
        throw std::runtime_error(std::string("fcntl(F_SETFL): ") + strerror(errno));
    if ((r = fcntl(m_readFd, F_GETFL)) == -1)
        throw std::runtime_error(std::string("fcntl(F_GETFL): ") + strerror(errno));
    if (fcntl(m_readFd, F_SETFL, r | O_NONBLOCK) == -1)
        throw std::runtime_error(std::string("fcntl(F_SETFL): ") + strerror(errno));
}

void MinecraftConsole::closeFd()
{

    if (m_writeFd >= 0 && close(m_writeFd) < 0)
        LOG_ERROR << "MinecraftConsole::closeFd (m_writeFd)" << std::strerror(errno);
    if (m_readFd >= 0 && close(m_readFd) < 0)
        LOG_ERROR << "MinecraftConsole::closeFd (m_readFd)" << std::strerror(errno);
        
    m_writeFd = -1;
    m_readFd = -1;
}

void MinecraftConsole::attachToEventLoop()
{
    auto *loop = drogon::app().getLoop();

    if (!loop->isInLoopThread())
        throw std::logic_error("attachConsole must run on main loop thread");

    if (m_readChannel || m_writeChannel)
        throw std::logic_error("MinecraftConsole already attached");
    if (m_readFd < 0 || m_writeFd < 0)
        throw std::logic_error("MinecraftConsole invalid file descriptors");

    setNonBlock();

    m_readChannel = std::make_unique<trantor::Channel>(loop, m_readFd);

    m_readChannel->setReadCallback(
    [this]
    {
        handleRead();
    });

    m_readChannel->enableReading();
    m_writeChannel = std::make_unique<trantor::Channel>(loop, m_writeFd);

    m_writeChannel->setWriteCallback(
    [this]
    {
        handleWrite();
    });
}

void MinecraftConsole::handleRead()
{
    char buffer[8192];

    while (true)
    {
        ssize_t n = read(m_readFd, buffer, sizeof(buffer));

        if (n > 0)
        {
            m_outputBuffer.append(buffer, n);
            continue ;
        }
        else if (n == 0)
        {
            handleConsoleData();
            detachReadChannel();
            return ;
        }

        if (errno == EINTR)
            continue ;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            handleConsoleData();
            return ;
        }

        LOG_ERROR << "Console read : " << std::strerror(errno);

        handleConsoleData();
        detachReadChannel();
        return ;
    }
}


void MinecraftConsole::handleConsoleData()
{
    std::size_t nlPos = m_outputBuffer.rfind("\n");
    if (nlPos == std::string::npos)
        return ;

    const auto length = nlPos + 1;


    auto data = std::string_view(m_outputBuffer.data(), length);
    appendLast100Lines(data);

    if (m_outputCallback)
        m_outputCallback(data);

    m_outputBuffer.erase(0, length);
}

std::string MinecraftConsole::getLast100Lines()
{
    return (m_last100Lines);
}

void MinecraftConsole::appendLast100Lines(std::string_view data)
{
    m_last100Lines.append(data);

    std::size_t pos = m_last100Lines.size();

    for (int i = 0; i < 101; ++i)
    {
        if (pos == 0)
            return ;

        pos = m_last100Lines.rfind('\n', pos - 1);

        if (pos == std::string::npos)
            return ;
    }

    m_last100Lines.erase(0, pos + 1);
}

void MinecraftConsole::sendCommand(std::string command)
{
    auto *loop = drogon::app().getLoop();

    loop->queueInLoop(
        [this, command = std::move(command)]
        {
            if (!m_writeChannel)
                return;

            m_inputBuffer += command;
            m_inputBuffer += '\n';

            m_writeChannel->enableWriting();
        }
    );
}

void MinecraftConsole::handleWrite()
{
    while (!m_inputBuffer.empty())
    {
        ssize_t n = ::write(m_writeFd, m_inputBuffer.data(), m_inputBuffer.size());

        if (n > 0)
        {
            m_inputBuffer.erase(0, n);
            continue ;
        }

        if (n < 0 && errno == EINTR)
            continue ;

        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;

        LOG_ERROR << "Console write failed";

        detachWriteChannel();
        return;
    }

    m_writeChannel->disableWriting();
}

void MinecraftConsole::detachReadChannel()
{
    if (!m_readChannel)
        return ;

    m_readChannel->disableAll();
    m_readChannel->remove();

    auto *loop = drogon::app().getLoop();

    auto keepAlive = std::shared_ptr<trantor::Channel>(std::move(m_readChannel));

    int fd = m_readFd;
    m_readFd = -1;

    loop->queueInLoop(
    [keepAlive, fd]
    {
        if (fd >= 0)
            close(fd);
    });
}

void MinecraftConsole::detachWriteChannel()
{
    if (!m_writeChannel)
        return ;

    m_writeChannel->disableAll();
    m_writeChannel->remove();

    auto *loop = drogon::app().getLoop();

    auto keepAlive = std::shared_ptr<trantor::Channel>(std::move(m_writeChannel));

    int fd = m_writeFd;
    m_writeFd = -1;

    loop->queueInLoop(
    [keepAlive, fd]
    {
        if (fd >= 0)
            close(fd);
    });
}

void MinecraftConsole::detachFromEventLoop()
{
    auto *loop = drogon::app().getLoop();

    loop->runInLoop([&]
    {
        detachReadChannel();
        detachWriteChannel();

        m_inputBuffer.clear();
        m_outputBuffer.clear();

        LOG_DEBUG << "Detached from EventLoop";
    });
}

void MinecraftConsole::setOutputCallback(std::function<void(std::string_view)> callback)
{
    m_outputCallback = callback;
}