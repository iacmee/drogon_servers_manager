#include "UserController.hpp"
#include <sodium.h>

std::string UserController::generateInviteToken()
{
    constexpr std::size_t tokenBytes = 32;
    constexpr int variant = sodium_base64_VARIANT_URLSAFE_NO_PADDING;

    std::array<unsigned char, tokenBytes> randomData{};
    randombytes_buf(randomData.data(),randomData.size());

    std::array<char, sodium_base64_ENCODED_LEN(tokenBytes, variant)> encoded{};
    sodium_bin2base64(encoded.data(), encoded.size(), randomData.data(), randomData.size(), variant);

    sodium_memzero(randomData.data(), randomData.size());

    return (std::string(encoded.data()));
}

std::string UserController::hashInviteToken(std::string_view token)
{
    std::array<unsigned char, crypto_generichash_BYTES> hash{};

    if (crypto_generichash(
            hash.data(),
            hash.size(),
            reinterpret_cast<const unsigned char *>(token.data()),
            static_cast<unsigned long long>(token.size()),
            nullptr,
            0) != 0)
    {
        throw std::runtime_error("Invite token hashing failed");
    }

    std::array<char, crypto_generichash_BYTES * 2 + 1> encoded{};

    sodium_bin2hex(encoded.data(), encoded.size(), hash.data(), hash.size());
    sodium_memzero(hash.data(), hash.size());

    return std::string(encoded.data());
}


// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------

UserController::UserController()
{
    drogon::app().registerBeginningAdvice(
        [this]()
        {
            start(drogon::app().getDbClient());
        });
}

void UserController::start(drogon::orm::DbClientPtr db)
{
    if (!db)
        throw std::runtime_error("UserController: database client unavailable");
    if (sodium_init() < 0)
        throw std::runtime_error("Cannot initialize libsodium");

    m_db = std::move(db);
    initializeDatabase();
    m_dummyPasswordHash = hashPassword("dummy-password-for-timing-only");

    if (needBootStrap())
    {
        m_db->execSqlSync("DELETE FROM invite_tokens");
        const std::string token = generateInviteToken();
        const std::string tokenHash = hashInviteToken(token);

        auto id = insertInviteToken(tokenHash, 1, std::chrono::minutes(5));
        if (!id)
            throw std::runtime_error("Cannot create bootstrap invite token");

        std::cerr
            << "\n============================================\n"
            << " INITIAL BOOTSTRAP TOKEN\n"
            << " " << token << '\n'
            << " Valid for 5 minutes and one use only\n"
            << "============================================\n\n";
    }

}

// -----------------------------------------------------------------------------
// Destructor
// -----------------------------------------------------------------------------

// UserController::~UserController()
// {
// }

// -----------------------------------------------------------------------------
// Database initialization
// -----------------------------------------------------------------------------

void UserController::initializeDatabase()
{
    static constexpr std::string_view usersSql = R"SQL(
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY,
            username TEXT NOT NULL UNIQUE,
            password_hash TEXT NOT NULL,
            created_at INTEGER NOT NULL
        );
    )SQL";

    static constexpr std::string_view settingsSql = R"SQL(
        CREATE TABLE IF NOT EXISTS invite_tokens (
            id INTEGER PRIMARY KEY,
            token_hash TEXT NOT NULL UNIQUE,
            uses_left INTEGER NOT NULL CHECK (uses_left >= 0),
            created_at INTEGER NOT NULL DEFAULT (unixepoch()),
            expires_at INTEGER,
            enabled INTEGER NOT NULL DEFAULT 1
                CHECK (enabled IN (0, 1))
        );
    )SQL";

    m_db->execSqlSync(std::string(usersSql));
    m_db->execSqlSync(std::string(settingsSql));
}

// -----------------------------------------------------------------------------
// Find user
// -----------------------------------------------------------------------------

drogon::Task<std::optional<drogon_model::sqlite3::Users>>
UserController::findUserAsync(const std::string &username)
{
    using namespace drogon::orm;
    using namespace drogon_model::sqlite3;

    CoroMapper<Users> mapper(m_db);
    try
    {
        auto user = co_await mapper.findOne(Criteria(Users::Cols::_username, CompareOperator::EQ, username));
        co_return (user);
    }
    catch (const UnexpectedRows &e)
    {
        co_return (std::nullopt);
    }
}

std::optional<drogon_model::sqlite3::Users>
UserController::findUser(const std::string &username)
{
    using namespace drogon::orm;
    using namespace drogon_model::sqlite3;

    Mapper<Users> mapper(m_db);
    try
    {
        auto user = mapper.findOne(Criteria(Users::Cols::_username, CompareOperator::EQ, username));
        return (user);
    }
    catch (const UnexpectedRows &e)
    {
        return (std::nullopt);
    }
}

drogon::Task<std::optional<drogon_model::sqlite3::InviteTokens>>
UserController::findInviteTokenAsync(const std::string &tokeHash)
{
    using namespace drogon::orm;
    using namespace drogon_model::sqlite3;

    CoroMapper<InviteTokens> mapper(m_db);
    try
    {
        auto inviteToken = co_await mapper.findOne(Criteria(InviteTokens::Cols::_token_hash, CompareOperator::EQ, tokeHash));
        co_return (inviteToken);
    }
    catch (const UnexpectedRows &e)
    {
        co_return (std::nullopt);
    }
}

std::optional<drogon_model::sqlite3::InviteTokens>
UserController::findInviteToken(const std::string &tokeHash)
{
    using namespace drogon::orm;
    using namespace drogon_model::sqlite3;

    Mapper<InviteTokens> mapper(m_db);
    try
    {
        auto inviteToken = mapper.findOne(Criteria(InviteTokens::Cols::_token_hash, CompareOperator::EQ, tokeHash));
        return (inviteToken);
    }
    catch (const UnexpectedRows &e)
    {
        return (std::nullopt);
    }
}



// -----------------------------------------------------------------------------
// Insert user
// -----------------------------------------------------------------------------

drogon::Task<std::optional<std::int64_t>>
UserController::insertUserAsync(std::string_view username, std::string_view password)
{
    using namespace drogon::orm;
    using namespace drogon_model::sqlite3;

    std::string passwordHash = hashPassword(password);
    Users user;
    user.setUsername(std::string(username));
    user.setPasswordHash(passwordHash);
    user.setCreatedAt(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));


    CoroMapper<Users> mapper(m_db);

    try
    {
        auto insertedUser = co_await mapper.insert(user);
        co_return (insertedUser.getValueOfId());
    }
    catch (const UniqueViolation &)
    {
        co_return (std::nullopt);
    }
}

std::optional<std::int64_t>
UserController::insertUser(std::string_view username, std::string_view password)
{
    using namespace drogon::orm;
    using namespace drogon_model::sqlite3;

    std::string PasswordHash = hashPassword(password);
    Users user;
    user.setUsername(std::string(username));
    user.setPasswordHash(PasswordHash);
    user.setCreatedAt(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));


    Mapper<Users> mapper(m_db);

    try
    {
        mapper.insert(user);
        return (user.getValueOfId());
    }
    catch (const UniqueViolation &)
    {
        return (std::nullopt);
    }
}

std::optional<std::int64_t>
UserController::insertInviteToken(
    const std::string &tokenHash,
    int times,
    std::optional<std::chrono::seconds> lifetime)
{
    using namespace drogon::orm;
    using namespace drogon_model::sqlite3;

    InviteTokens token;
    token.setTokenHash(tokenHash);
    token.setUsesLeft(times);
    token.setEnabled(1);

    if (lifetime)
    {
        const auto expiresAt = std::chrono::system_clock::now() + *lifetime;

        const auto unixTime =
            std::chrono::duration_cast<std::chrono::seconds>(
                expiresAt.time_since_epoch()
            ).count();

        token.setExpiresAt(unixTime);
    }

    Mapper<InviteTokens> mapper(m_db);

    try
    {
        mapper.insert(token);
        return (token.getValueOfId());
    }
    catch (const UniqueViolation &)
    {
        return (std::nullopt);
    }
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
    sodium_memzero(hash, sizeof(hash));
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

// bool UserController::verifyInviteToken(drogon_model::sqlite3::InviteTokens &inviteToken)
// {
//     using namespace drogon::orm;
//     using namespace drogon_model::sqlite3;

//     bool removeToken = false;

//     if (inviteToken.getValueOfEnabled() == 0)
//         removeToken = true;
//     if (inviteToken.getValueOfUsesLeft() <= 0)
//         removeToken = true;

//     const auto &expiresAt = inviteToken.getExpiresAt();
//     if (expiresAt)
//     {
//         const auto now =
//             std::chrono::duration_cast<std::chrono::seconds>(
//                 std::chrono::system_clock::now().time_since_epoch()
//             ).count();

//         if (*expiresAt <= now)
//             removeToken = true;
//     }

//     if (removeToken)
//     {
//         Mapper<InviteTokens> mapper(m_db);
//         mapper.deleteOne(inviteToken);
//         return (false);
//     }
//     return (true);
// }

// void UserController::useInviteToken(drogon_model::sqlite3::InviteTokens &inviteToken)
// {
//     using namespace drogon::orm;
//     using namespace drogon_model::sqlite3;

//     Mapper<InviteTokens> mapper(m_db);

//     const auto usesLeft = inviteToken.getValueOfUsesLeft();

//     if (usesLeft <= 1)
//     {
//         mapper.deleteOne(inviteToken);
//         return ;
//     }

//     inviteToken.setUsesLeft(usesLeft - 1);
//     mapper.update(inviteToken);
// }

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

drogon::Task<> UserController::createNewUser(drogon::HttpRequestPtr req, Callback callback)
{

    auto json = req->getJsonObject();
    if (!json || !json->isObject())
        co_return (callback(jsonError(drogon::k400BadRequest, "Invalid JSON body")));
    if (!json->isMember("username") || !json->isMember("password") || !json->isMember("token"))
        co_return (callback(jsonError(drogon::k400BadRequest, "Missing user, password or token")));
    if (!(*json)["username"].isString() || !(*json)["password"].isString() || !(*json)["token"].isString())
        co_return (callback(jsonError(drogon::k400BadRequest, "user, password and token must be strings")));

    const std::string username = (*json)["username"].asString();
    const std::string password = (*json)["password"].asString();
    const std::string token = (*json)["token"].asString();

    // qua dovrei fare i controlli per il nome e la password lunghezza caratteri speciali etc...

    auto userId = co_await registerUserWithInviteAsync(username, password, token);

    if (!userId)
        co_return (callback(jsonError(drogon::k403Forbidden, "Invalid invite token or username already in use")));

    LOG_INFO << "New registered user=" << username << " id=" << static_cast<Json::Int64>(*userId);
    Json::Value responseJson;
    responseJson["status"] = "ok";
    responseJson["username"] = username;
    responseJson["id"] = static_cast<Json::Int64>(*userId);
    auto response = drogon::HttpResponse::newHttpJsonResponse(responseJson);
    response->setStatusCode(drogon::k201Created);
    callback(response);

}

drogon::Task<std::optional<std::int64_t>>
UserController::registerUserWithInviteAsync(std::string_view username, std::string_view password, std::string_view Token)
{
    const std::string tokenHash = hashInviteToken(Token);
    auto transaction = co_await m_db->newTransactionCoro();

    try
    {
        const auto now = std::chrono::system_clock::now();
        const auto nowSeconds =
            std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch())
                .count();

        auto result = co_await transaction->execSqlCoro(
            R"(
                UPDATE invite_tokens
                SET uses_left = uses_left - 1
                WHERE token_hash = ?
                AND enabled = 1
                AND uses_left > 0
                AND (expires_at IS NULL OR expires_at > ?)
            )",
            tokenHash,
            nowSeconds);

        if (result.affectedRows() != 1)
        {
            transaction->rollback();
            co_return (std::nullopt);
        }

        const std::string passwordHash = hashPassword(password);

        auto insertResult = co_await transaction->execSqlCoro(
            R"(
                INSERT INTO users(username, password_hash, created_at)
                VALUES (?, ?, ?)
            )",
            username,
            passwordHash,
            nowSeconds);

        const auto userId = static_cast<std::int64_t>(insertResult.insertId());

        co_await transaction->execSqlCoro(
            R"(
                DELETE FROM invite_tokens
                WHERE token_hash = ?
                  AND uses_left <= 0
            )",
            tokenHash);

        co_return (userId);
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        transaction->rollback();

        LOG_ERROR << e.base().what();

        co_return (std::nullopt);
    }
}

// -----------------------------------------------------------------------------
// Login
// -----------------------------------------------------------------------------

drogon::Task<> UserController::login(drogon::HttpRequestPtr req, Callback callback)
{
	auto json = req->getJsonObject();
	if (!json || !json->isObject())
		co_return (callback(jsonError(drogon::k400BadRequest, "Invalid JSON body")));
	if (!json->isMember("username") || !json->isMember("password"))
		co_return (callback(jsonError(drogon::k400BadRequest, "Missing user or password")));
	if (!(*json)["username"].isString() || !(*json)["password"].isString())
		co_return (callback(jsonError(drogon::k400BadRequest, "user and password must be strings")));

	const std::string username = (*json)["username"].asString();
	std::string password = (*json)["password"].asString();
	if (username.empty() || username.size() > 128 ||
        password.empty() || password.size() > 128)
		co_return (callback(jsonError(drogon::k400BadRequest, "Invalid username or password")));

	try
	{
		auto user = co_await findUserAsync(username);

        const bool userExist = user.has_value() && user->getValueOfId() == 0;
        const std::string &passwordHash =
            userExist ? user->getValueOfPasswordHash() : m_dummyPasswordHash;
        const bool passwordValid = verifyPassword(password, passwordHash);

        if (!userExist || ! passwordValid)
            co_return (callback(jsonError(drogon::k401Unauthorized, "Invalid username or password")));

		auto session = req->session();
		session->clear();
		session->changeSessionIdToClient();
		session->insert("authenticated", true);
		session->insert("user_id", user->getValueOfId());
		session->insert("username", user->getValueOfUsername());
		Json::Value responseJson;
		responseJson["status"] = "ok";
		responseJson["username"] = user->getValueOfUsername();
		responseJson["id"] = static_cast<Json::Int64>(user->getValueOfId());
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

bool UserController::needBootStrap()
{
    using namespace drogon::orm;
    using namespace drogon_model::sqlite3;

    Mapper<Users> mapper(m_db);

    return (mapper.count() == 0);
}
