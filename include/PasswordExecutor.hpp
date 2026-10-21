#pragma once
#include <condition_variable>
#include <deque>
#include <drogon/drogon.h>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

struct PasswordQueueFull : std::runtime_error
{
	PasswordQueueFull() : std::runtime_error("Password queue full") {}
};

// Bounds both expensive simultaneous hashes and retained request data.
class PasswordExecutor
{
  public:
	explicit PasswordExecutor(std::size_t workers = 2, std::size_t capacity = 8);
	~PasswordExecutor();
	bool submit(std::function<void()> work);

  private:
	void run();

  private:
	const std::size_t m_capacity;
	std::vector<std::thread> m_workers;
	std::condition_variable m_ready;
	std::deque<std::function<void()>> m_queue;
	std::mutex m_mutex;
	bool m_stopping{false};
};

template <typename T> class PasswordWork : public drogon::CallbackAwaiter<T>
{
  public:
	PasswordWork(PasswordExecutor &executor, std::function<T()> work) : m_executor(executor), m_work(std::move(work)) {}
	bool await_suspend(std::coroutine_handle<> handle)
	{
		auto *loop = trantor::EventLoop::getEventLoopOfCurrentThread();
		if (!loop)
			throw std::logic_error("PasswordWork requires an event loop");
		if (m_executor.submit([this, loop, handle] {
				try
				{
					this->setValue(m_work());
				}
				catch (...)
				{
					this->setException(std::current_exception());
				}
				loop->queueInLoop([handle] { handle.resume(); });
			}))
			return (true);
		this->setException(std::make_exception_ptr(PasswordQueueFull()));
		return (false);
	}

  private:
	PasswordExecutor &m_executor;
	std::function<T()> m_work;
};
