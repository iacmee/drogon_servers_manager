#include "UserController.hpp"
#include <cstdint>
#include <drogon/drogon.h>
#include <json/json.h>
#include <sodium.h>
#include <sqlite3.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include "Users.h"

namespace
{
    // Small RAII wrapper around sqlite3_stmt.
    // The statement is always finalized when it goes out of scope.
    class SqliteStatement
    {
    public:
        SqliteStatement(sqlite3 *db, const char *sql) : m_db(db)
        {
            const int rc = sqlite3_prepare_v2(m_db, sql, -1, &m_stmt, nullptr);

            if (rc != SQLITE_OK)
            {
                throw std::runtime_error(std::string("sqlite3_prepare_v2 failed: ") + sqlite3_errmsg(m_db));
            }
        }

        ~SqliteStatement()
        {
            if (m_stmt)
            {
                sqlite3_finalize(m_stmt);
            }
        }

        SqliteStatement(const SqliteStatement &) = delete;
        SqliteStatement &operator=(const SqliteStatement &) = delete;

        sqlite3_stmt *get() const
        {
            return (m_stmt);
        }

        void bindText(int index, std::string_view value)
        {
            const int rc = sqlite3_bind_text(m_stmt, index, value.data(),
                                             static_cast<int>(value.size()), SQLITE_TRANSIENT);

            if (rc != SQLITE_OK)
            {
                throw std::runtime_error(std::string("sqlite3_bind_text failed: ") + sqlite3_errmsg(m_db));
            }
        }

    private:
        sqlite3 *m_db{nullptr};
        sqlite3_stmt *m_stmt{nullptr};
    };

    std::string readTextColumn(sqlite3_stmt *stmt, int column)
    {
        const unsigned char *value = sqlite3_column_text(stmt, column);

        if (!value)
            return (std::string(""));
        return (reinterpret_cast<const char *>(value));
    }

} // namespace

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------

UserController::UserController(const std::string &databaseFile)
{
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    int rc;

    if (sodium_init() < 0)
        throw std::runtime_error("Cannot initialize libsodium");

    rc = sqlite3_open_v2(databaseFile.c_str(), &m_db, flags, nullptr);
    if (rc != SQLITE_OK)
    {
        const std::string error = m_db ? sqlite3_errmsg(m_db) : "Unknown SQLite error";
        if (m_db)
        {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
        throw std::runtime_error("Cannot open database: " + error);
    }
    // Wait for a short time instead of immediately failing
    // if another process temporarily locks the database.
    rc = sqlite3_busy_timeout(m_db, 5000);
    if (rc != SQLITE_OK)
    {
        const std::string error = sqlite3_errmsg(m_db);
        sqlite3_close(m_db);
        m_db = nullptr;
        throw std::runtime_error("Cannot configure SQLite busy timeout: " + error);
    }
    try
    {
        initializeDatabase();
    }
    catch (...)
    {
        sqlite3_close(m_db);
        m_db = nullptr;
        throw;
    }

    try
    {
        auto masterUser = findUser("register_new_user");
        if (!masterUser)
        {
            LOG_INFO << "Registration master user "
                      << "'register_new_user' was not found"
                      << " cannot register new users";
            m_isRegistrationOn = false;
        }
        else
            m_isRegistrationOn = true;
    }
    catch (const std::exception &e)
    {
        m_isRegistrationOn = false;
        LOG_ERROR << e.what();
    }
}

// -----------------------------------------------------------------------------
// Destructor
// -----------------------------------------------------------------------------

UserController::~UserController()
{
    if (m_db)
    {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

// -----------------------------------------------------------------------------
// Database initialization
// -----------------------------------------------------------------------------

void UserController::initializeDatabase()
{
    char *error = nullptr;
    const char *sql = R"SQL(
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY,
            username TEXT NOT NULL UNIQUE,
            password_hash TEXT NOT NULL,
            created_at INTEGER NOT NULL
        );
    )SQL";

    const int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &error);
    if (rc != SQLITE_OK)
    {
        const std::string message = error ? error : sqlite3_errmsg(m_db);
        if (error)
            sqlite3_free(error);
        throw std::runtime_error("Cannot create users table: " + message);
    }
}

// -----------------------------------------------------------------------------
// Find user
// -----------------------------------------------------------------------------

std::optional<drogon_model::sqlite3::Users> UserController::findUser(const std::string &username)
{
    // const char *sql = R"SQL(
    //     SELECT
    //         id,
    //         username,
    //         password_hash,
    //         created_at
    //     FROM users
    //     WHERE username = ?1
    //     LIMIT 1;
    // )SQL";

    using namespace drogon::orm;
    using namespace drogon_model::sqlite3;

    auto db = drogon::app().getDbClient();
    Mapper<Users> mapper(db);
    try
    {
        auto user = mapper.findOne(Criteria(Users::Cols::_username, CompareOperator::EQ, username));

        return (user);
    }
    catch (const UnexpectedRows &e)
    {
        return (std::nullopt);
    }


    // SqliteStatement stmt(m_db, sql);
    // stmt.bindText(1, username);
    // const int rc = sqlite3_step(stmt.get());

    // if (rc == SQLITE_DONE)
    //     return (std::nullopt);
    // if (rc != SQLITE_ROW)
    //     throw std::runtime_error(std::string("Cannot read user from database: ") + sqlite3_errmsg(m_db));
    // UserController::UserRecord user;
    // user.id = sqlite3_column_int64(stmt.get(), 0);
    // user.username = readTextColumn(stmt.get(), 1);
    // user.passwordHash = readTextColumn(stmt.get(), 2);
    // user.createdAt = sqlite3_column_int64(stmt.get(), 3);
    // return (user);
}

// -----------------------------------------------------------------------------
// Insert user
// -----------------------------------------------------------------------------

std::optional<std::int64_t> UserController::insertUser(std::string_view username,
                                                       std::string_view passwordHash)
{
    /*
     * RETURNING avoids using sqlite3_last_insert_rowid().
     *
     * This is important because this controller uses one serialized SQLite
     * connection that may be accessed by multiple Drogon worker threads.
     */
    const char *sql = R"SQL(
        INSERT INTO users (
            username,
            password_hash,
            created_at
        )
        VALUES (
            ?1,
            ?2,
            unixepoch()
        )
        RETURNING id
    )SQL";
    SqliteStatement stmt(m_db, sql);
    stmt.bindText(1, username);
    stmt.bindText(2, passwordHash);
    int rc = sqlite3_step(stmt.get());
    /*
     * SQLITE_CONSTRAINT can be returned when the UNIQUE constraint
     * on username is violated.
     *
     * The low byte contains the primary SQLite result code, so this
     * also works if extended result codes are enabled.
     */
    if ((rc & 0xFF) == SQLITE_CONSTRAINT)
        return (std::nullopt);
    if (rc != SQLITE_ROW)
        throw std::runtime_error(std::string("Cannot insert user: ") + sqlite3_errmsg(m_db));
    const std::int64_t newId = sqlite3_column_int64(stmt.get(), 0);
    /*
     * Finish execution of the INSERT ... RETURNING statement.
     */
    rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE)
        throw std::runtime_error(std::string("Cannot complete user insertion: ") + sqlite3_errmsg(m_db));
    return (newId);
}

// -----------------------------------------------------------------------------
// Password hashing
// -----------------------------------------------------------------------------

std::string UserController::hashPassword(std::string_view password)
{
    char hash[crypto_pwhash_STRBYTES];
    const int rc = crypto_pwhash_str(hash, password.data(), password.size(),
                                     crypto_pwhash_OPSLIMIT_INTERACTIVE,
                                     crypto_pwhash_MEMLIMIT_INTERACTIVE);

    if (rc != 0)
        throw std::runtime_error("Password hashing failed");
    std::string result(hash);
    std::memset(hash, 0, sizeof(hash));
    return (result);
}

// -----------------------------------------------------------------------------
// Password verification
// -----------------------------------------------------------------------------

bool UserController::verifyPassword(std::string_view password,
                                    const std::string &storedHash)
{
    return (crypto_pwhash_str_verify(storedHash.c_str(), password.data(),
                                     password.size()) == 0);
}

// -----------------------------------------------------------------------------
// JSON error helper
// -----------------------------------------------------------------------------

drogon::HttpResponsePtr UserController::jsonError(drogon::HttpStatusCode status,
                                                  std::string_view message)
{
    Json::Value json;
    json["error"] = std::string(message);
    auto response = drogon::HttpResponse::newHttpJsonResponse(json);
    response->setStatusCode(status);
    return (response);
}

// -----------------------------------------------------------------------------
// Register new user
// -----------------------------------------------------------------------------

void UserController::createNewUser(const drogon::HttpRequestPtr &req, Callback &&callback)
{
    if (!m_isRegistrationOn)
    {
        callback(jsonError(drogon::k403Forbidden, "User registration is not configured"));
        return ;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isObject())
        return (callback(jsonError(drogon::k400BadRequest, "Invalid JSON body")));
    if (!json->isMember("username") || !json->isMember("password") || !json->isMember("master_password"))
        return (callback(jsonError(drogon::k400BadRequest, "Missing user, password or master_password")));
    if (!(*json)["username"].isString() || !(*json)["password"].isString() || !(*json)["master_password"].isString())
        return (callback(jsonError(drogon::k400BadRequest, "user, password and master_password must be strings")));

    const std::string username = (*json)["username"].asString();
    const std::string password = (*json)["password"].asString();
    const std::string masterPassword = (*json)["master_password"].asString();

    // Validate username
    if (username.size() < 4)
        return (callback(jsonError(drogon::k406NotAcceptable, "Username must contain at least 4 characters")));
    if (username.size() > 128)
        return (callback(jsonError(drogon::k406NotAcceptable, "Username cannot exceed 128 characters")));

    // Validate password
    if (password.size() < 13)
        return (callback(jsonError(drogon::k406NotAcceptable, "Password must contain at least 13 characters")));
    if (password.size() > 128)
        return (callback(jsonError(drogon::k406NotAcceptable, "Password cannot exceed 128 characters")));

    try
    {
        // Read the special registration user
        auto masterUser = findUser("register_new_user");
        if (!masterUser)
            return (callback(jsonError(drogon::k500InternalServerError, "Internal server error")));
        if (!verifyPassword(masterPassword, masterUser->passwordHash))
            return (callback(jsonError(drogon::k401Unauthorized, "Wrong master password")));

        // Hash the new user's password
        const std::string passwordHash = hashPassword(password);
        auto newUserId = insertUser(username, passwordHash);
        if (!newUserId)
            return (callback(jsonError(drogon::k409Conflict, "Username already in use")));

        LOG_INFO << "New registered user=" << username << " id=" << static_cast<Json::Int64>(*newUserId);
        Json::Value responseJson;
        responseJson["status"] = "ok";
        responseJson["username"] = username;
        responseJson["id"] = static_cast<Json::Int64>(*newUserId);
        auto response = drogon::HttpResponse::newHttpJsonResponse(responseJson);
        response->setStatusCode(drogon::k201Created);
        callback(response);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "createNewUser failed: " << e.what();
        callback(jsonError(drogon::k500InternalServerError, "Internal server error"));
    }
}

// -----------------------------------------------------------------------------
// Login
// -----------------------------------------------------------------------------

void UserController::login(const drogon::HttpRequestPtr &req, Callback
	&&callback)
{
	auto json = req->getJsonObject();
	if (!json || !json->isObject())
		return (callback(jsonError(drogon::k400BadRequest, "Invalid JSON body")));
	if (!json->isMember("username") || !json->isMember("password"))
		return (callback(jsonError(drogon::k400BadRequest, "Missing user or password")));
	if (!(*json)["username"].isString() || !(*json)["password"].isString())
		return (callback(jsonError(drogon::k400BadRequest, "user and password must be strings")));

	const std::string username = (*json)["username"].asString();
	std::string password = (*json)["password"].asString();
	if (username.empty() || username.size() > 128 ||
        password.empty() || password.size() > 128)
		return (callback(jsonError(drogon::k400BadRequest, "Invalid username or password")));

	try
	{
		auto user = findUser(username);
		if (!user || user->id == 0)
			return (callback(jsonError(drogon::k401Unauthorized, "Invalid username or password")));
		if (!verifyPassword(password, user->passwordHash))
			return (callback(jsonError(drogon::k401Unauthorized, "Invalid username or password")));

		auto session = req->session();
		session->clear();
		session->changeSessionIdToClient();
		session->insert("authenticated", true);
		session->insert("user_id", user->id);
		session->insert("username", user->username);
		Json::Value responseJson;
		responseJson["status"] = "ok";
		responseJson["username"] = user->username;
		responseJson["id"] = static_cast<Json::Int64>(user->id);
		auto response = drogon::HttpResponse::newHttpJsonResponse(responseJson);
		response->setStatusCode(drogon::k200OK);
		callback(response);
	}
	catch (const std::exception &e)
	{
		LOG_ERROR << "login failed: " << e.what();
		callback(jsonError(drogon::k500InternalServerError, "Internal server error"));
	}
}



// -----------------------------------------------------------------------------
// Logout
// -----------------------------------------------------------------------------

void UserController::logout(const drogon::HttpRequestPtr &req, Callback &&callback)
{
    auto session = req->session();

    session->clear();
    session->changeSessionIdToClient();

    Json::Value responseJson;
    responseJson["status"] = "ok";
    auto response = drogon::HttpResponse::newHttpJsonResponse(responseJson);

    response->setStatusCode(drogon::k200OK);

    callback(response);
}