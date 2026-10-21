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
		Clock::time_point start;
		unsigned count{0};
	};
	std::mutex mutex;
	Window global{Clock::time_point::min(), 0};
	std::unordered_map<std::string, Window> addresses;
};

class RegistrationFilter : public drogon::HttpFilter<RegistrationFilter>
{
  public:
	void doFilter(const drogon::HttpRequestPtr &req, drogon::FilterCallback
		&&reject, drogon::FilterChainCallback &&next) override;

  private:
	RegistrationLimiter limiter;
};
