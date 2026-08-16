#include "config.h"
#include "UserController.hpp"
#include <drogon/drogon.h>
#include <filesystem>
#include <iostream>
#include <sodium.h>
#include <sqlite3.h>
#include <json/json.h>
#include <cstdint>


UserController::UserController(const std::string &databaseFile)
{
    if (sodium_init() < 0)
        throw std::runtime_error("Cannot initialize libsodium");

    int result;
    char *error;

    result = sqlite3_open(databaseFile.c_str(), &m_db);
    if (result != SQLITE_OK)
    {
        std::string error = m_db ? sqlite3_errmsg(m_db) : "Unknown SQLite error";
        if (m_db)
        {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
        throw std::runtime_error("Cannot open database: " + error);
    }

    // ------------------------------------------------------------
    // Creazione tabella users
    // ------------------------------------------------------------

    const char *createTableSql = R"(
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY,
            username TEXT NOT NULL UNIQUE,
            password_hash TEXT NOT NULL,
            created_at INTEGER NOT NULL
        );
    )";

    error = nullptr;

    if (sqlite3_exec(m_db, createTableSql, nullptr, nullptr,
                     &error) != SQLITE_OK)
    {
        std::string message = error ? error : sqlite3_errmsg(m_db);
        sqlite3_free(error);
        sqlite3_close(m_db);
        m_db = nullptr;
        throw std::runtime_error("Cannot create users table: " + message);
    }

    // ------------------------------------------------------------
    // Controllo dell'utente speciale id=0
    // ------------------------------------------------------------

    // const char *checkUserSql = R"(
    //     SELECT EXISTS (
    //         SELECT 1
    //         FROM users
    //         WHERE id = 0
    //           AND username = 'register_new_user'
    //     );
    // )";
    // stmt = nullptr;
    // result = sqlite3_prepare_v2(m_db, checkUserSql, -1, &stmt, nullptr);
    // if (result != SQLITE_OK)
    // {
    // 	std::string message = sqlite3_errmsg(m_db);
    // 	sqlite3_close(m_db);
    // 	m_db = nullptr;
    // 	throw std::runtime_error("Cannot query users table: " + message);
    // }
    // result = sqlite3_step(stmt);
    // if (result != SQLITE_ROW)
    // {
    // 	std::string message = sqlite3_errmsg(m_db);
    // 	sqlite3_finalize(stmt);
    // 	sqlite3_close(m_db);
    // 	m_db = nullptr;
    // 	throw std::runtime_error("Cannot check register_new_user: " + message);
    // }
    // sqlite3_finalize(stmt);
    // const bool registerNewUserExists = sqlite3_column_int(stmt, 0) != 0

    // // ------------------------------------------------------------
    // // Qui decidi cosa fare se non esiste
    // // ------------------------------------------------------------

    // if (!registerNewUserExists)
    // {
    // 	// L'utente speciale id=0 / register_new_user non esiste.
    // 	//
    // 	// Qui puoi, ad esempio:
    // 	// - crearlo;
    // 	// - disabilitare la registrazione;
    // 	// - impostare un flag interno;
    // 	// - lanciare un'eccezione.
    // }
}

UserController::~UserController()
{
    if (m_db)
    {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

void UserController::createNewUser(const drogon::HttpRequestPtr &req,
                                   std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    auto json = req->getJsonObject();
    std::string pHash;

    if (!json || !json->isMember("master_password") || !(*json)["master_password"].isString())
    {
        Json::Value errJson;
        errJson["error"] = "no 'master_password' member found";
        auto response = drogon::HttpResponse::newHttpJsonResponse(errJson);
        response->setStatusCode(drogon::k400BadRequest);
        callback(response);
        return ;
    }

    try
    {
        if (readUserFromDb("register_new_user", nullptr, &pHash, nullptr))
        {
            auto response = drogon::HttpResponse::newHttpResponse();
            response->setStatusCode(drogon::k500InternalServerError);
            callback(response);
            return ;
        }
    }
    catch (const std::runtime_error& e)
    {
        auto response = drogon::HttpResponse::newHttpResponse();
        response->setStatusCode(drogon::k500InternalServerError);
        callback(response);
        return ;
    }

    if (!json ||
        !json->isMember("user") || !json->isMember("password") ||
        !(*json)["user"].isString() || !(*json)["password"].isString()
        )
    {
        Json::Value errJson;
        errJson["error"] = "Invalid JSON";
        auto response = drogon::HttpResponse::newHttpJsonResponse(errJson);
        response->setStatusCode(drogon::k400BadRequest);
        callback(response);
        return ;
    }

    const std::string username = (*json)["user"].asString();
    const std::string password = (*json)["password"].asString();
    const std::string masterPassword = (*json)["master_password"].asString();

    try
    {
        if (readUserFromDb(username, nullptr, nullptr, nullptr) == 0)
        {
            Json::Value errJson;
            errJson["error"] = "Username already in use";
            auto response = drogon::HttpResponse::newHttpJsonResponse(errJson);
            response->setStatusCode(drogon::k409Conflict);
            callback(response);
            return ;
        }
    }
    catch (const std::runtime_error& e)
    {
        auto response = drogon::HttpResponse::newHttpResponse();
        response->setStatusCode(drogon::k500InternalServerError);
        callback(response);
        return ;
    }

    if (password.size() < 13 ||
        password.size() > 128 ||
        username.size() > 128 ||
        username.size() < 4)
    {
            Json::Value errJson;
            errJson["error"] = "username or password too short or too long";
            auto response = drogon::HttpResponse::newHttpJsonResponse(errJson);
            response->setStatusCode(drogon::k406NotAcceptable);
            callback(response);
            return ;
    }


    if (crypto_pwhash_str_verify(
            pHash.c_str(),
            masterPassword.c_str(),
            masterPassword.size()) != 0)
    {
        Json::Value errJson;
        errJson["error"] = "Wrong master_password";
        auto response = drogon::HttpResponse::newHttpJsonResponse(errJson);
        response->setStatusCode(drogon::k401Unauthorized);
        callback(response);
        return ;
    }


    char passwordHash[crypto_pwhash_STRBYTES];

    if (crypto_pwhash_str(
            passwordHash,
            password.c_str(),
            password.size(),
            crypto_pwhash_OPSLIMIT_INTERACTIVE,
            crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0)
    {
        auto response = drogon::HttpResponse::newHttpResponse();
        response->setStatusCode(drogon::k500InternalServerError);
        callback(response);
        return ;
    }


	const char *sql = R"(
        INSERT INTO users (
            username,
            password_hash,
            created_at
        )
        VALUES (?1, ?2, unixepoch());
    )";
    sqlite3_stmt* stmt = nullptr;
	int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
	if (rc != SQLITE_OK)
	{
        auto response = drogon::HttpResponse::newHttpResponse();
		response->setStatusCode(drogon::k500InternalServerError);
		callback(response);
		return ;
	}
	rc = sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
	if (rc != SQLITE_OK)
	{
		sqlite3_finalize(stmt);
        auto response = drogon::HttpResponse::newHttpResponse();
		response->setStatusCode(drogon::k500InternalServerError);
		callback(response);
		return ;
	}
	rc = sqlite3_bind_text(stmt, 2, passwordHash, -1, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK)
    {
        sqlite3_finalize(stmt);
        auto response = drogon::HttpResponse::newHttpResponse();
		response->setStatusCode(drogon::k500InternalServerError);
		callback(response);
		return ;
    }
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        Json::Value out;
        out["status"] = "ok";
        out["user"] = username;
        auto response = drogon::HttpResponse::newHttpJsonResponse(out);
        response->setStatusCode(drogon::k201Created);
        callback(response);
        return ;
    }

    sqlite3_finalize(stmt);
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(drogon::k500InternalServerError);
    callback(response);
}

void UserController::login(const drogon::HttpRequestPtr &req,
                           std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
}

void UserController::logout(const drogon::HttpRequestPtr &req,
                           std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
}

int UserController::readUserFromDb(const std::string &username,
                                    std::int64_t *id,
                                    std::string *pHash,
                                    std::int64_t *created_at)
{
    const char* sql = R"(
        SELECT id, password_hash, created_at
        FROM users
        WHERE username = ?1;
    )";

    sqlite3_stmt* stmt = nullptr;

	int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK)
        throw std::runtime_error(std::string("sqlite3_prepare_v2 error : ") + sqlite3_errmsg(m_db));

    rc = sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    if (rc != SQLITE_OK)
    {
        sqlite3_finalize(stmt);

        throw std::runtime_error(std::string("sqlite3_bind_text error: ") + sqlite3_errmsg(m_db));
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
        if (id)
            *id = sqlite3_column_int64(stmt, 0);
        if (pHash)
        {
            const unsigned char* value = sqlite3_column_text(stmt, 1);
            if (value)
                *pHash = reinterpret_cast<const char*>(value);
            else
                pHash->clear();

        }
        if (created_at)
            *created_at = sqlite3_column_int64(stmt, 2);

        sqlite3_finalize(stmt);
        return (0);

    }
    else if (rc == SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        return (1);
    }

    std::string error = sqlite3_errmsg(m_db);
    sqlite3_finalize(stmt);
    throw std::runtime_error("sqlite3_step error: " + error);
}