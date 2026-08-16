#pragma once

// #include <drogon/HttpController.h>
// #include <sodium.h>
// #include <sqlite3.h>


// class UserController : public drogon::HttpController<UserController, false>
// {
//   public:
// 	explicit UserController(const std::string &databaseFile);

// 	~UserController();

// 	METHOD_LIST_BEGIN

// 	ADD_METHOD_TO(UserController::login, "/api/login", drogon::Post);

// 	ADD_METHOD_TO(UserController::logout, "/api/logout", drogon::Post);

//     ADD_METHOD_TO(UserController::createNewUser, "/api/register", drogon::Post);

// 	METHOD_LIST_END

// 	void login(const drogon::HttpRequestPtr &req,
// 		std::function<void(const drogon::HttpResponsePtr &)> &&callback);

// 	void logout(const drogon::HttpRequestPtr &req,
// 		std::function<void(const drogon::HttpResponsePtr &)> &&callback);

// 	void createNewUser(const drogon::HttpRequestPtr &req,
// 		std::function<void(const drogon::HttpResponsePtr &)> &&callback);

//   private:
// 	sqlite3 *m_db;

//     int readUserFromDb(const std::string &username, std::int64_t *id, std::string *pHash, std::int64_t *created_at);

// };
#pragma once

#include <cstdint>
#include <drogon/HttpController.h>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

// Forward declaration.
// sqlite3.h is only needed in the .cpp file.
struct sqlite3;

class UserController : public drogon::HttpController<UserController, false>
{
  public:
	using Callback = std::function<void(const drogon::HttpResponsePtr &)>;

	UserController(const std::string &databaseFile);
	UserController(const UserController &) = delete;
	UserController &operator=(const UserController &) = delete;

    ~UserController();

	METHOD_LIST_BEGIN

	ADD_METHOD_TO(UserController::login, "/api/auth/login", drogon::Post);
	ADD_METHOD_TO(UserController::logout, "/api/auth/logout", drogon::Post);
	ADD_METHOD_TO(UserController::createNewUser, "/api/auth/register", drogon::Post);

	METHOD_LIST_END

	void login(const drogon::HttpRequestPtr &req, Callback &&callback);
	void logout(const drogon::HttpRequestPtr &req, Callback &&callback);
	void createNewUser(const drogon::HttpRequestPtr &req, Callback &&callback);

  private:
    sqlite3 *m_db{nullptr};
    bool m_isRegistrationOn{false};

	struct	UserRecord
	{
		std::int64_t id{};
		std::string username;
		std::string passwordHash;
		std::int64_t createdAt{};
	};

	void initializeDatabase();
	std::optional<UserRecord> findUser(std::string_view username);
	std::optional<std::int64_t> insertUser(std::string_view username, std::string_view passwordHash);
	static bool verifyPassword(std::string_view password, const std::string &storedHash);
	static std::string hashPassword(std::string_view password);
	static drogon::HttpResponsePtr jsonError(drogon::HttpStatusCode status, std::string_view message);
};