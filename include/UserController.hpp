#pragma once

#include "InviteTokens.h"
#include "Users.h"
#include <drogon/drogon.h>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <array>



class UserController : public drogon::HttpController<UserController>
{
  public:
	using Callback = std::function<void(const drogon::HttpResponsePtr &)>;

	UserController();
    void start(drogon::orm::DbClientPtr db);

	METHOD_LIST_BEGIN

	ADD_METHOD_TO(UserController::login, "/api/auth/login", drogon::Post);
	ADD_METHOD_TO(UserController::logout, "/api/auth/logout", drogon::Post);
	ADD_METHOD_TO(UserController::createNewUser, "/api/auth/register", drogon::Post);

	METHOD_LIST_END

	drogon::Task<> login(drogon::HttpRequestPtr req, Callback callback);
	drogon::Task<> createNewUser(drogon::HttpRequestPtr req, Callback callback);
	void logout(const drogon::HttpRequestPtr &req, Callback &&callback);

  private:
    drogon::orm::DbClientPtr m_db;
    std::string m_dummyPasswordHash;

	void initializeDatabase();
    std::string generateInviteToken();
    std::string hashInviteToken(std::string_view token);
    bool needBootStrap();
    bool verifyInviteToken(drogon_model::sqlite3::InviteTokens &inviteTokens);
    void useInviteToken(drogon_model::sqlite3::InviteTokens &inviteToken);

    drogon::Task<std::optional<drogon_model::sqlite3::InviteTokens>> findInviteTokenAsync(const std::string &tokenHash);
    drogon::Task<std::optional<drogon_model::sqlite3::Users>> findUserAsync(const std::string &username);
	drogon::Task<std::optional<std::int64_t>> insertUserAsync(std::string_view username, std::string_view passwordHash);
    drogon::Task<std::optional<std::int64_t>> registerUserWithInviteAsync(std::string_view username,
        std::string_view password, std::string_view tokenHash);

    std::optional<drogon_model::sqlite3::Users> findUser(const std::string &username);
	std::optional<std::int64_t> insertUser(std::string_view username, std::string_view password);
    std::optional<drogon_model::sqlite3::InviteTokens> findInviteToken(const std::string &tokenHash);
    std::optional<std::int64_t> insertInviteToken(const std::string &tokenHash, int times,
        std::optional<std::chrono::seconds> lifetime);

	static bool verifyPassword(std::string_view password, const std::string &storedHash);
	static std::string hashPassword(std::string_view password);
	static drogon::HttpResponsePtr jsonError(drogon::HttpStatusCode status, std::string_view message);
};