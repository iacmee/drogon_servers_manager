#pragma once

#include "SdBusProvider.hpp"
#include <atomic>
#include <cstdint>
// #include <json/json.h>
#include <string>
#include <vector>

class MinecraftConsole
{
  public:

    void sendCommand(std::string command, std::function<bool()> authorized,
                     std::function<void(std::string_view)> rejected);
    void attachToEventLoop();
    void detachFromEventLoop();
    void closeFd();
    void setOutputCallback(std::function<void(std::string_view)> callback);
    std::string getLast100Lines();
    
  private:
    void setNonBlock();
    void handleRead();
    void handleWrite();
    void detachReadChannel();
    void detachWriteChannel();
    void handleConsoleData();
    void appendLast100Lines(std::string_view data);

  public:

    int m_readFd{-1};
    int m_writeFd{-1};

  private:
    std::unique_ptr<trantor::Channel> m_readChannel;
    std::unique_ptr<trantor::Channel> m_writeChannel;

    std::string m_outputBuffer;
    std::string m_inputBuffer;
    // Counts bytes in both event-loop tasks and the write buffer.
    std::atomic<std::size_t> m_pendingCommandBytes{0};
    std::string m_last100Lines;

    std::function<void(std::string_view)> m_outputCallback;

};