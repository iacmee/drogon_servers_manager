#include "PasswordExecutor.hpp"

PasswordExecutor::PasswordExecutor(std::size_t count,
	std::size_t capacity) : m_capacity(capacity)
{
	if (count == 0 || capacity == 0)
		throw std::invalid_argument("Password worker count and queue must be positive");
	try
	{
		for (std::size_t i = 0; i < count; ++i)
			m_workers.emplace_back([this] { run(); });
	}
	catch (...)
	{
		{
			std::lock_guard lock(m_mutex);
			m_stopping = true;
		}
		m_ready.notify_all();
		for (auto &worker : m_workers)
			worker.join();
		throw;
	}
}

PasswordExecutor::~PasswordExecutor()
{
	{
		std::lock_guard lock(m_mutex);
		m_stopping = true;
	}
	m_ready.notify_all();
	for (auto &worker : m_workers)
		worker.join();
}

bool PasswordExecutor::submit(std::function<void()> work)
{
	{
		std::lock_guard lock(m_mutex);
		if (m_stopping || m_queue.size() >= m_capacity)
			return (false);
		m_queue.push_back(std::move(work));
	}
	m_ready.notify_one();
	return (true);
}

void PasswordExecutor::run()
{
	while (true)
	{
		std::function<void()> work;
		{
			std::unique_lock lock(m_mutex);
			m_ready.wait(lock, [this] { return (m_stopping || !m_queue.empty()); });
			if (m_queue.empty())
				return ;
			work = std::move(m_queue.front());
			m_queue.pop_front();
		}
		// PasswordWork delivers exceptions to the awaiting request.
		try
		{
			work();
		}
		catch (...)
		{
			LOG_ERROR << "Unhandled password worker exception";
		}
	}
}
