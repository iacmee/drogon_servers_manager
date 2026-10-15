#include "MinecraftServer.hpp"
#include <json/json.h>
#include <stdexcept>
#include <utility>
#include <fcntl.h>


MinecraftServer::MinecraftServer(std::int64_t id, const std::string &name,
	const std::string &directory, const std::string &service,
	const std::string &logPath, const std::string &jarPath,
	const std::string &javaPath, SdBusProvider &sdBusProvider) : m_id(id),
	m_name(name), m_directory(directory), m_service(service),
	m_logPath(logPath), m_jarPath(jarPath), m_javaPath(javaPath),
	m_sdBusProvider(sdBusProvider)
{
    m_state.store(ServerState::Unknown, std::memory_order_relaxed);
}

MinecraftServer::MinecraftServer(MinecraftServer &&other)
    : m_id(other.m_id), m_name(other.m_name),
      m_directory(other.m_directory), m_service(other.m_service),
      m_logPath(other.m_logPath), m_jarPath(other.m_jarPath),
      m_javaPath(other.m_javaPath), m_state(other.m_state.load()),
      m_objectPath(std::move(other.m_objectPath)),
      m_sdBusProvider(other.m_sdBusProvider)
{
}

MinecraftServer::~MinecraftServer()
{
    executeUnitAction(UnitAction::Stop, [](const drogon::HttpResponsePtr &){});

    LOG_DEBUG << "MinecraftServer destroyd";
}

void MinecraftServer::start(Callback &&callback)
{
    // executeUnitAction(UnitAction::Start, std::move(callback));
    startTransient(std::move(callback));
}

void MinecraftServer::stop(Callback &&callback)
{
    drogon::app().getLoop()->queueInLoop([&]
    {            
        m_console.detachFromEventLoop();
    });
    executeUnitAction(UnitAction::Stop, std::move(callback));
}

void MinecraftServer::restart(Callback &&callback)
{
    executeUnitAction(UnitAction::Restart, std::move(callback));
}

void MinecraftServer::executeUnitAction(UnitAction action, Callback &&callback)
{
    auto &bus = m_sdBusProvider.current();

    const char *method;
    const char *actionName;

    switch (action)
    {
        case UnitAction::Start:
            method = "StartUnit";
            actionName = "start";
            break;

        case UnitAction::Stop:
            method = "StopUnit";
            actionName = "stop";
            break;

        case UnitAction::Restart:
            method = "RestartUnit";
            actionName = "restart";
            break;

        default:
            throw std::invalid_argument("Invalid UnitAction");
    }

    auto callbackPtr = std::make_shared<Callback>(std::move(callback));

    auto functionCallback =
    [serverName = m_name,
     actionName = std::string(actionName),
     callbackPtr]
    (sd_bus_message *message)
    {
        Json::Value json;

        json["server_name"] = serverName;
        json["action"] = actionName;

        auto respond =
        [&](bool success, drogon::HttpStatusCode status)
        {
            json["success"] = success;

            auto response = drogon::HttpResponse::newHttpJsonResponse(json);
            response->setStatusCode(status);

            (*callbackPtr)(response);
        };

        const int r = sd_bus_message_is_method_error(message, nullptr);

        if (r < 0)
        {
            LOG_ERROR << "sd_bus_message_is_method_error: "
                << std::strerror(-r);

            respond(false, drogon::k500InternalServerError);
            return ;
        }

        if (r > 0)
        {
            const sd_bus_error *error = sd_bus_message_get_error(message);

            if (error)
            {
                LOG_ERROR << "D-Bus error: "
                    << (error->name ? error->name : "") << " - "
                    << (error->message ? error->message : "");
            }

            respond(false, drogon::k500InternalServerError);
            return ;
        }

        respond(true, drogon::k200OK);
    };

    const int r = bus.callMethodAsync(
        SYSTEMD_DESTINATION,
        SYSTEMD_PATH,
        SYSTEMD_MANAGER_INTERFACE,
        method,
        std::move(functionCallback),
        "ss",
        (m_name + ".service").c_str(),
        "replace");

    if (r < 0)
    {
        LOG_ERROR << "MinecraftServer::" << actionName << " : "
            << std::strerror(-r);

        Json::Value json;

        json["server_name"] = m_name;
        json["action"] = actionName;
        json["success"] = false;

        auto response = drogon::HttpResponse::newHttpJsonResponse(json);
        response->setStatusCode(drogon::k500InternalServerError);

        (*callbackPtr)(response);
    }
}

int MinecraftServer::updateObjectPath(bool performInAsync)
{
    auto &bus = m_sdBusProvider.current();

    auto functionCallback =
    [this] (sd_bus_message *message)
    {
        int r = sd_bus_message_is_method_error(message, nullptr);

        if (r < 0)
        {
            LOG_ERROR << "sd_bus_message_is_method_error: "
                << std::strerror(-r);
            return ;
        }
        if (r > 0)
        {
            const sd_bus_error *error = sd_bus_message_get_error(message);

            if (error)
            {
                LOG_ERROR << "D-Bus error: "
                    << (error->name ? error->name : "") << " - "
                    << (error->message ? error->message : "");
            }
            return ;
        }
        const char* path = nullptr;
        r = sd_bus_message_read(message, "o", &path);
        if (r < 0)
            LOG_ERROR << "sd_bus_message_read in MinecraftServer::updateObjectPath : " << strerror(-r);
        else if (r == 0)
            LOG_ERROR << "sd_bus_message_read in MinecraftServer::updateObjectPath : No value present";
        else
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_objectPath = path;
        }
    };

    int r;
    if (performInAsync)
        r = bus.callMethodAsync(
            SYSTEMD_DESTINATION,
            SYSTEMD_PATH,
            SYSTEMD_MANAGER_INTERFACE,
            "GetUnit",
            std::move(functionCallback),
            "s",
            (m_name + ".service").c_str());
    else
    {
        r = bus.callMethod(
            SYSTEMD_DESTINATION,
            SYSTEMD_PATH,
            SYSTEMD_MANAGER_INTERFACE,
            "GetUnit",
            std::move(functionCallback),
            "s",
            (m_name + ".service").c_str());
    }

    if (r < 0)
    {
        LOG_ERROR << "MinecraftServer::updateObjectPath : "
            << std::strerror(-r);
        return (-1);
    }
    return (0);
}

int MinecraftServer::startMonitorOnState()
{
    auto *loop = drogon::app().getLoop();

    if (!loop->isInLoopThread())
        throw std::logic_error("startMonitorOnState() is not on main event loop thread");

    auto &bus = m_sdBusProvider.current();

    int r = updateObjectPath(false);
    if (r != 0)
        throw std::logic_error("updateObjectPath() can't update m_objectPath");

    auto functionCallback =
    [this] (sd_bus_message *message)
    {
        const char *interface = nullptr;
        int r = sd_bus_message_read(message, "s", &interface);
        if (r < 0)
        {
            LOG_ERROR << "sd_bus_message_read(interface): "
                << std::strerror(-r);
            return ;
        }

        if (std::strcmp(interface, SYSTEMD_UNIT_INTERFACE) != 0)
            return ;

        r = sd_bus_message_enter_container(message, SD_BUS_TYPE_ARRAY, "{sv}");
        if (r < 0)
        {
            LOG_ERROR << "sd_bus_message_enter_container(array): "
                << std::strerror(-r);
            return ;
        }

        while ((r = sd_bus_message_enter_container(message, SD_BUS_TYPE_DICT_ENTRY, "sv")) > 0)
        {
            const char *property = nullptr;

            r = sd_bus_message_read(message, "s", &property);
            if (r < 0)
            {
                LOG_ERROR << "sd_bus_message_read(property): "
                    << std::strerror(-r);
                return ;
            }

            if (std::strcmp(property, "ActiveState") == 0)
            {
                r = sd_bus_message_enter_container(message, SD_BUS_TYPE_VARIANT,"s");
                if (r < 0)
                {
                    LOG_ERROR << "sd_bus_message_enter_container(variant): "
                        << std::strerror(-r);
                    return ;
                }

                const char *state = nullptr;

                r = sd_bus_message_read(message, "s", &state);
                if (r < 0)
                {
                    LOG_ERROR << "sd_bus_message_read(ActiveState): "
                        << std::strerror(-r);
                    return ;
                }

                updateStateFromString(state);

                r = sd_bus_message_exit_container(message);
                if (r < 0)
                {
                    LOG_ERROR << "sd_bus_message_exit_container(variant): "
                        << std::strerror(-r);
                    return ;
                }
            }
            else
            {
                r = sd_bus_message_skip(message, "v");
                if (r < 0)
                {
                    LOG_ERROR << "sd_bus_message_skip: "
                        << std::strerror(-r);
                    return ;
                }
            }
            r = sd_bus_message_exit_container(message);
            if (r < 0)
            {
                LOG_ERROR << "sd_bus_message_exit_container(dict): "
                    << std::strerror(-r);
                return ;
            }

        }
        if (r < 0)
        {
            LOG_ERROR << "sd_bus_message_enter_container(dict): "
                << std::strerror(-r);
            return ;
        }

        r = sd_bus_message_exit_container(message);
        if (r < 0)
        {
            LOG_ERROR << "sd_bus_message_exit_container(array): "
                << std::strerror(-r);
            return ;
        }

    };

    std::string objPath;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        objPath = m_objectPath;
    }

    r = bus.matchSignal(
        SYSTEMD_DESTINATION,
        objPath.c_str(),
        DBUS_PROPERTIES_INTERFACE,
        "PropertiesChanged",
        std::move(functionCallback));

    if (r < 0)
    {
        LOG_ERROR << "MinecraftServer::startMonitorOnState : "
            << std::strerror(-r);
        return (r);
    }

    r = bus.callMethod(
        SYSTEMD_DESTINATION,
        SYSTEMD_PATH,
        SYSTEMD_MANAGER_INTERFACE,
        "Subscribe",
        [](sd_bus_message *message)
        {
            (void)message;
        },
        nullptr);

    if (r < 0)
    {
        LOG_ERROR << "MinecraftServer::startMonitorOnState Subscribe: "
            << std::strerror(-r);

        return (r);
    }


    r = bus.callMethodAsync(
        SYSTEMD_DESTINATION,
        objPath.c_str(),
        DBUS_PROPERTIES_INTERFACE,
        "Get",
        [this](sd_bus_message *message)
        {
            if (sd_bus_message_is_method_error(message, nullptr) != 0)
            {
                const auto *error = sd_bus_message_get_error(message);
                LOG_ERROR << "Failed to read initial ActiveState for " << m_service
                    << ": " << (error && error->message ? error->message : "Invalid D-Bus reply");
                return;
            }

            int result = sd_bus_message_enter_container(message, SD_BUS_TYPE_VARIANT, "s");
            if (result <= 0)
            {
                LOG_ERROR << "Invalid initial ActiveState variant for " << m_service
                    << ": " << (result < 0 ? std::strerror(-result) : "Missing value");
                return;
            }

            const char *state = nullptr;
            result = sd_bus_message_read(message, "s", &state);
            if (result <= 0)
            {
                LOG_ERROR << "Failed to parse initial ActiveState for " << m_service
                    << ": " << (result < 0 ? std::strerror(-result) : "Missing value");
                return;
            }

            result = sd_bus_message_exit_container(message);
            if (result < 0)
            {
                LOG_ERROR << "Failed to exit initial ActiveState variant for " << m_service
                    << ": " << std::strerror(-result);
                return;
            }

            updateStateFromString(state);
        },
        "ss",
        SYSTEMD_UNIT_INTERFACE,
        "ActiveState");

    if (r < 0)
    {
        LOG_ERROR << "Failed to request initial ActiveState for " << m_service
            << ": " << std::strerror(-r);
        return r;
    }

    return (0);
}

void MinecraftServer::updateStateFromString(const char *state)
{
    ServerState newState = ServerState::Unknown;

    if (state)
    {
        if (std::strcmp(state, "active") == 0)
            newState = ServerState::Running;
        else if (std::strcmp(state, "inactive") == 0)
            newState = ServerState::Stopped;
        else if (std::strcmp(state, "activating") == 0)
            newState = ServerState::Starting;
        else if (std::strcmp(state, "deactivating") == 0)
        {
            // devo fare il detach della console 
            drogon::app().getLoop()->queueInLoop([&]
            {            
                m_console.detachFromEventLoop();
            });
            newState = ServerState::Stopping;
        }
        else if (std::strcmp(state, "failed") == 0)
            newState = ServerState::Failed;
    }

    m_state.store(newState, std::memory_order_relaxed);
}

const char *MinecraftServer::statusToString() const
{
    switch (m_state.load(std::memory_order_relaxed))
    {
        case ServerState::Stopped:  return ("Stopped");
        case ServerState::Starting: return ("Starting");
        case ServerState::Running:  return ("Running");
        case ServerState::Stopping: return ("Stopping");
        case ServerState::Failed:   return ("Failed");
        case ServerState::Unknown:  return ("Unknown");
        default : return ("Unknown");
    }
}


void MinecraftServer::startTransient(Callback &&callback)
{
    Json::Value json;
    json["server_name"] = m_name;
    json["action"] = "start";

    if (m_console.m_writeFd >= 0 || m_console.m_readFd >= 0) // da aggiungere anche controlli sullo stato etc
    {
        auto state = m_state.load(std::memory_order_relaxed);
        auto response = drogon::HttpResponse::newHttpJsonResponse(json);
        if (state == ServerState::Running || state == ServerState::Starting)
        {
            json["succes"] = true;
            json["note"] = "already running";
            response->setStatusCode(drogon::k200OK);
        }
        else
        {
            json["succes"] = false;
            response->setStatusCode(drogon::k500InternalServerError);
        }
        callback(response);
        return ;
    }

    int stdinPipe[2] {-1, -1};
    int outputPipe[2] {-1, -1};

    sd_bus_message* msg = nullptr;
    sd_bus_message* reply   = nullptr;
    sd_bus_error error = SD_BUS_ERROR_NULL;

    auto cleanup = [&]
    {
        sd_bus_message_unref(msg);
        sd_bus_message_unref(reply);
        sd_bus_error_free(&error);

        if (stdinPipe[0] >= 0)
            close(stdinPipe[0]);
        if (stdinPipe[1] >= 0)
            close(stdinPipe[1]);
        if (outputPipe[0] >= 0)
            close(outputPipe[0]);
        if (outputPipe[1] >= 0)
            close(outputPipe[1]);
    };

    auto check = [](int r, const char *what)
    {
        if (r < 0)
            throw std::runtime_error(std::string(what) + ": " + strerror(-r));
    };

    try
    {
        if (pipe2(stdinPipe, O_CLOEXEC) < 0)
            throw std::runtime_error(std::string("pipe2 stdin: ") + strerror(errno));
        if (pipe2(outputPipe, O_CLOEXEC) < 0)
            throw std::runtime_error(std::string("pipe2 stdout: ") + strerror(errno));

        check(
            sd_bus_message_new_method_call(
                m_sdBusProvider.current().getSdBus(),
                &msg,
                SYSTEMD_DESTINATION,
                SYSTEMD_PATH,
                SYSTEMD_MANAGER_INTERFACE,
                "StartTransientUnit"
            ),
            "new StartTransientUnit message"
        );

// std::cerr << "Executable: [" << m_jarPath << "]\n";

// for (size_t i = 0; i < m_argv.size(); ++i) {
//     std::cerr << "argv[" << i << "]: ["
//               << m_argv[i] << "]\n";
// }
// std::cout << "java path : " << m_javaPath << std::endl;
// std::cout << "Unit name : " << (m_name + ".service") << std::endl;
// std::cerr << "WorkingDirectory: [" << m_directory << "]\n";

        check(sd_bus_message_append(msg, "ss", (m_name + ".service").c_str(), "fail"), "append unit name");
        check(sd_bus_message_open_container(msg, SD_BUS_TYPE_ARRAY, "(sv)"), "open properties");
        check(sd_bus_message_append(msg, "(sv)", "Description", "s", "PaperMC Minecraft Server"), "Description");
        check(sd_bus_message_append(msg, "(sv)", "Type","s","simple"), "Type");
        check(sd_bus_message_append(msg, "(sv)", "WorkingDirectory", "s", m_directory.c_str()), "WorkingDirectory");

        check(sd_bus_message_append(msg, "(sv)", "CollectMode", "s", "inactive-or-failed"), "CollectMode");
        check(sd_bus_message_open_container(msg, SD_BUS_TYPE_STRUCT, "sv"), "open SuccessExitStatus property");
        check(sd_bus_message_append(msg, "s", "SuccessExitStatus"), "append SuccessExitStatus name");
        check(sd_bus_message_open_container(msg, SD_BUS_TYPE_VARIANT, "(aiai)"), "open SuccessExitStatus variant");
        check(sd_bus_message_open_container(msg, SD_BUS_TYPE_STRUCT, "aiai"), "open SuccessExitStatus struct");
        check(sd_bus_message_append(msg, "ai", 2, 130, 143), "append success exit codes");
        check(sd_bus_message_append(msg, "ai", 0), "append success signals");
        check(sd_bus_message_close_container(msg), "close SuccessExitStatus struct");
        check(sd_bus_message_close_container(msg), "close SuccessExitStatus variant");
        check(sd_bus_message_close_container(msg), "close SuccessExitStatus property");

        check(sd_bus_message_append(msg, "(sv)","StandardInputFileDescriptor", "h", stdinPipe[0]),"StandardInputFileDescriptor");
        check(sd_bus_message_append(msg, "(sv)","StandardOutputFileDescriptor", "h", outputPipe[1]),"StandardOutputFileDescriptor");
        check(sd_bus_message_append(msg, "(sv)","StandardErrorFileDescriptor", "h" ,outputPipe[1]),"StandardErrorFileDescriptor");

        check(sd_bus_message_open_container(msg, SD_BUS_TYPE_STRUCT, "sv"), "open ExecStart property");
        check(sd_bus_message_append(msg, "s", "ExecStart"), "append ExecStart name");
        check(sd_bus_message_open_container(msg, SD_BUS_TYPE_VARIANT, "a(sasb)"), "open ExecStart variant");
        check(sd_bus_message_open_container(msg, SD_BUS_TYPE_ARRAY, "(sasb)")," open ExecStart array");
        check(sd_bus_message_open_container(msg, SD_BUS_TYPE_STRUCT, "sasb")," open ExecStart struct");
        check(sd_bus_message_append(msg, "s", m_javaPath.c_str()), "append executable");
        check(sd_bus_message_open_container(msg, SD_BUS_TYPE_ARRAY, "s"), "open argv");
        for (const auto& arg : m_argv)
            check(sd_bus_message_append(msg, "s", arg.c_str()), "append argv");
        check(sd_bus_message_close_container(msg), "close argv");
        check(sd_bus_message_append(msg, "b", 0), "append ignore-failure");
        check(sd_bus_message_close_container(msg), "close exec struct");
        check(sd_bus_message_close_container(msg), "close exec array");
        check(sd_bus_message_close_container(msg), "close exec variant");
        check(sd_bus_message_close_container(msg), "close exec property");


        check(sd_bus_message_close_container(msg), "close properties");
        check(sd_bus_message_append(msg, "a(sa(sv))", 0), "append auxiliary units");

        int r = sd_bus_call(m_sdBusProvider.current().getSdBus(), msg, 0, &error, &reply);
        if (r < 0)
        {
            const char* detail = (error.message) ? error.message : strerror(-r);
            throw std::runtime_error(std::string("StartTransientUnit: ") + detail);
        }

        // const char* jobPath = nullptr;
        // check(sd_bus_message_read(reply, "o", &jobPath), "read job path");

        close(stdinPipe[0]);
        m_console.m_writeFd = stdinPipe[1];
        close(outputPipe[1]);
        m_console.m_readFd = outputPipe[0];

        sd_bus_message_unref(msg);
        sd_bus_message_unref(reply);
        sd_bus_error_free(&error);
    }
    catch (const std::exception &e)
    {
        cleanup();
        LOG_ERROR << e.what();
        json["success"] = false;
        auto response = drogon::HttpResponse::newHttpJsonResponse(json);
        response->setStatusCode(drogon::k500InternalServerError);
        callback(response);
        return ;
    }

    // m_console.setOutputCallback(
    //     [] (std::string_view lines)
    //     {
    //         std::cout << lines << std::flush;
    //     }
    // );


    drogon::app().getLoop()->queueInLoop([&]
    {            
        m_console.attachToEventLoop();
    });

    json["success"] = true;
    auto response = drogon::HttpResponse::newHttpJsonResponse(json);
    response->setStatusCode(drogon::k200OK);
    if (monitorStarted.exchange(true) == false)
    {
        drogon::app().getLoop()->queueInLoop([&]
        {            
            startMonitorOnState();
        });
    }
    callback(response);
}
