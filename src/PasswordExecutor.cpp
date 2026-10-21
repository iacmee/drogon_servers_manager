#include "PasswordExecutor.hpp"

PasswordExecutor::PasswordExecutor(std::size_t count,
	std::size_t capacity) : capacity(capacity)
{
	if (count == 0 || capacity == 0)
		throw std::invalid_argument("Password worker count and queue must be positive");
	try
	{
		for (std::size_t i = 0; i < count; ++i)
			workers.emplace_back([this] { run(); });
	}
	catch (...)
	{
		{
			std::lock_guard lock(mutex);
			stopping = true;
		}
		ready.notify_all();
		for (auto &worker : workers)
			worker.join();
		throw;
	}
}

PasswordExecutor::~PasswordExecutor()
{
	{
		std::lock_guard lock(mutex);
		stopping = true;
	}
	ready.notify_all();
	for (auto &worker : workers)
		worker.join();
}

bool PasswordExecutor::submit(std::function<void()> work)
{
	{
		std::lock_guard lock(mutex);
		if (stopping || queue.size() >= capacity)
			return (false);
		queue.push_back(std::move(work));
	}
	ready.notify_one();
	return (true);
}

void PasswordExecutor::run()
{
	while (true)
	{
		std::function<void()> work;
		{
			std::unique_lock lock(mutex);
			ready.wait(lock, [this] { return (stopping || !queue.empty()); });
			if (queue.empty())
				return ;
			work = std::move(queue.front());
			queue.pop_front();
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
