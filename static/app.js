"use strict";


const statusElement = document.getElementById("status");

const registerForm =
    document.getElementById("registerForm");

const loginForm =
    document.getElementById("loginForm");

const logoutButton =
    document.getElementById("logoutButton");


// ================================================================
// OUTPUT
// ================================================================

function showStatus(message, data = null)
{
    let text = message;

    if (data !== null)
        text += "\n\n" + JSON.stringify(data, null, 2);

    statusElement.textContent = text;
}


// ================================================================
// GENERIC API REQUEST
// ================================================================

async function apiRequest(path, options = {})
{
    const response = await fetch(path,
    {
        credentials: "same-origin",

        ...options,

        headers:
        {
            "Content-Type": "application/json",

            ...(options.headers || {})
        }
    });


    let body = null;

    try
    {
        body = await response.json();
    }
    catch
    {
        body =
        {
            error: `Risposta non JSON (HTTP ${response.status})`
        };
    }


    if (!response.ok)
    {
        const message =
            body && typeof body.error === "string"
                ? body.error
                : `Errore HTTP ${response.status}`;

        throw new Error(message);
    }


    return body;
}


// ================================================================
// REGISTER
// ================================================================

registerForm.addEventListener(
    "submit",

    async (event) =>
    {
        event.preventDefault();


        const token =
            document
                .getElementById("registerToken")
                .value
                .trim();


        const username =
            document
                .getElementById("registerUsername")
                .value
                .trim();


        const password =
            document
                .getElementById("registerPassword")
                .value;


        showStatus("Creazione utente in corso...");


        try
        {
            const result =
                await apiRequest(
                    "/api/auth/register",
                    {
                        method: "POST",

                        body: JSON.stringify(
                        {
                            username: username,
                            password: password,
                            token: token
                        })
                    }
                );


            showStatus(
                "Utente creato correttamente.",
                result
            );


            // Cancella subito i dati sensibili
            document.getElementById(
                "registerPassword"
            ).value = "";

            document.getElementById(
                "registerToken"
            ).value = "";


            // Copia lo username nel login
            document.getElementById(
                "loginUsername"
            ).value = username;
        }
        catch (error)
        {
            showStatus(
                "Registrazione fallita: " +
                error.message
            );
        }
    }
);


// ================================================================
// LOGIN
// ================================================================

loginForm.addEventListener(
    "submit",

    async (event) =>
    {
        event.preventDefault();


        const username =
            document
                .getElementById("loginUsername")
                .value
                .trim();


        const password =
            document
                .getElementById("loginPassword")
                .value;


        showStatus("Login in corso...");


        try
        {
            const result =
                await apiRequest(
                    "/api/auth/login",
                    {
                        method: "POST",

                        body: JSON.stringify(
                        {
                            username: username,
                            password: password
                        })
                    }
                );


            showStatus(
                "Login effettuato.",
                result
            );


            document.getElementById(
                "loginPassword"
            ).value = "";
        }
        catch (error)
        {
            showStatus(
                "Login fallito: " +
                error.message
            );
        }
    }
);


// ================================================================
// LOGOUT
// ================================================================

logoutButton.addEventListener(
    "click",

    async () =>
    {
        showStatus("Logout in corso...");


        try
        {
            const result =
                await apiRequest(
                    "/api/auth/logout",
                    {
                        method: "POST",

                        body: JSON.stringify({})
                    }
                );


            showStatus(
                "Logout effettuato.",
                result
            );
        }
        catch (error)
        {
            showStatus(
                "Logout fallito: " +
                error.message
            );
        }
    }
);