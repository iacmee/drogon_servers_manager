#pragma once
#include <chrono>
#include <drogon/drogon.h>
#include <mutex>
#include <unordered_map>

class RegistrationLimiter
{
  public:
	using Clock = std::chrono::steady_clock;
	bool allow(const std::string &ip, Clock::time_point now = Clock::now());

  private:
	struct	Window
	{
		Clock::time_point m_start;
		unsigned m_count{0};
	};
	std::mutex m_mutex;
	Window m_global{Clock::time_point::min(), 0};
	std::unordered_map<std::string, Window> m_addresses;
};

class RegistrationFilter : public drogon::HttpFilter<RegistrationFilter>
{
  public:
	void doFilter(const drogon::HttpRequestPtr &req, drogon::FilterCallback
		&&reject, drogon::FilterChainCallback &&next) override;

  private:
	RegistrationLimiter m_limiter;
};
