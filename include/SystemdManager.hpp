#pragma once

#include <cstdint>
#include <string>
#include <systemd/sd-bus.h>

// struct	UnitInfo
// {
// 	std::string id;
// 	std::string description;

// 	std::string loadState;
// 	std::string activeState;
// 	std::string subState;
// };

// class SystemdManager
// {
//   public:
// 	// constructor / destructor
// 	SystemdManager();
// 	SystemdManager(SystemdManager &&) = delete;
// 	SystemdManager(const SystemdManager &) = delete;
// 	~SystemdManager();

// 	// operator
// 	SystemdManager &operator=(const SystemdManager &) = delete;
// 	SystemdManager &operator=(SystemdManager &&) = delete;

// 	std::string startUnit(const std::string &unit);
// 	std::string stopUnit(const std::string &unit);
// 	std::string restartUnit(const std::string &unit);
// 	UnitInfo getUnitInfo(const std::string &unit);

//   private:
// 	sd_bus *m_bus = nullptr;

// 	std::string callUnitMethod(const char *method, const std::string &unit);
// 	std::string loadUnitPath(const std::string &unit);
// 	std::string getStringProperty(const std::string &objectPath,
// 		const char *interface, const char *property);
// };

// #include <memory>
// #include <systemd/sd-bus.h>
// #include <trantor/net/Channel.h>
// #include <trantor/net/EventLoop.h>

// class SystemdManager
// {
//   public:
// 	void start(trantor::EventLoop *loop);
// 	void stop();

//   private:
// 	void processBus();
// 	void updateWatch();
// 	void onTimeout();

// 	trantor::EventLoop *m_loop = nullptr;
// 	sd_bus *m_bus = nullptr;
// 	trantor::TimerId m_timerId = trantor::InvalidTimerId;
// 	std::unique_ptr<trantor::Channel> m_channel;
// };

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <systemd/sd-bus.h>
#include <trantor/net/Channel.h>
#include <trantor/net/EventLoop.h>
#include <unordered_map>

struct							UnitInfo
{
	std::string id;
	std::string description;
	std::string loadState;
	std::string activeState;
	std::string subState;
};

template <typename T> struct SystemdResult
{
	bool ok = false;

	T value{};

	// Codice negativo errno-style, quando disponibile.
	int errorCode = 0;

	std::string error;

	explicit operator bool() const noexcept
	{
		return (ok);
	}
};

class SystemdManager
{
  public:
	using StringResult = SystemdResult<std::string>;
	using UnitInfoResult = SystemdResult<UnitInfo>;
	using StringCallback = std::function<void(StringResult)>;
	using UnitInfoCallback = std::function<void(UnitInfoResult)>;

	SystemdManager() = default;
	SystemdManager(const SystemdManager &) = delete;
	SystemdManager(SystemdManager &&) = delete;

	SystemdManager &operator=(const SystemdManager &) = delete;
	SystemdManager &operator=(SystemdManager &&) = delete;

	~SystemdManager();

	/**
		* Deve essere chiamato quando il loop Drogon è già partito.
		*
		* Normalmente:
		*
		* app().registerBeginningAdvice([&]() {
		*     manager.start(app().getLoop());
		* });
		*/
	void start(trantor::EventLoop *loop);
	void startUnit(std::string unit, StringCallback callback);
	void stopUnit(std::string unit, StringCallback callback);
	void restartUnit(std::string unit, StringCallback callback);
	void getUnitInfo(std::string unit, UnitInfoCallback callback);

  private:
	static constexpr const char *SYSTEMD_DESTINATION = "org.freedesktop.systemd1";
	static constexpr const char *SYSTEMD_PATH = "/org/freedesktop/systemd1";
	static constexpr const char *SYSTEMD_MANAGER_INTERFACE = "org.freedesktop.systemd1.Manager";
	static constexpr const char *SYSTEMD_UNIT_INTERFACE = "org.freedesktop.systemd1.Unit";
	static constexpr const char *DBUS_PROPERTIES_INTERFACE = "org.freedesktop.DBus.Properties";

	enum class ReplyType
	{
		ObjectPath,
		VariantString
	};

    using InternalStringCallback = std::function<void(StringResult)>;

	struct  PendingStringCall
	{
		SystemdManager  *owner = nullptr;
		sd_bus_slot *slot = nullptr;
		ReplyType   replyType = ReplyType::ObjectPath;
		InternalStringCallback	callback;
		std::string operation;

		~PendingStringCall()
		{
			slot = sd_bus_slot_unref(slot);
		}
	};

	struct  UnitInfoState
	{
		UnitInfo    info;
		std::size_t remaining = 0;
		bool    completed = false;
		UnitInfoCallback    callback;
	};

	std::atomic<trantor::EventLoop *> m_loop{nullptr};
	sd_bus						*m_bus = nullptr;
	std::unique_ptr<trantor::Channel> m_channel;
	trantor::TimerId m_timerId = trantor::InvalidTimerId;
	std::unordered_map<PendingStringCall *, std::unique_ptr<PendingStringCall>> m_pendingCalls;

	// -------------------------------------------------------
	// Event-loop / bus integration
	// -------------------------------------------------------

	void stopInLoop();
	void processBus();
	void updateBusWatch();
	void updateBusTimeout();
	void createOrUpdateChannel(int fd);
	static uint64_t monotonicUsec();

	// -------------------------------------------------------
	// Public -> DBus loop dispatch
	// -------------------------------------------------------

	void callUnitMethodInLoop(const char *method, const std::string &unit,
		InternalStringCallback callback);

	void loadUnitPathInLoop(const std::string &unit,
		InternalStringCallback callback);

	void getStringPropertyInLoop(const std::string &objectPath,
		const char *interface, const char *property,
		InternalStringCallback callback);

	void getUnitInfoInLoop(const std::string &unit, UnitInfoCallback callback);

	// -------------------------------------------------------
	// Generic async D-Bus request
	// -------------------------------------------------------

	using MessageBuilder = std::function<int(sd_bus_message *)>;

	void callAsyncStringInLoop(const char *destination, const char *path,
		const char *interface, const char *method, ReplyType replyType,
		std::string operation, MessageBuilder builder,
		InternalStringCallback callback);

	static int onStringReply(sd_bus_message *message, void *userdata,
		sd_bus_error *retError);

	void completePendingCall(PendingStringCall *call, StringResult result);

	// -------------------------------------------------------
	// Helpers
	// -------------------------------------------------------

	static StringResult makeStringSuccess(std::string value);
	static StringResult makeStringError(int errorCode, std::string message);
	static UnitInfoResult makeUnitInfoSuccess(UnitInfo value);
	static UnitInfoResult makeUnitInfoError(int errorCode, std::string message);
	static std::string messageError(sd_bus_message *message, const std::string &operation);
	static void deliverStringResult(trantor::EventLoop *destinationLoop, StringCallback callback, StringResult result);
	static void deliverUnitInfoResult(trantor::EventLoop *destinationLoop, UnitInfoCallback callback, UnitInfoResult result);
};