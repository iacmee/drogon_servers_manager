"use strict";
const $ = (id) => document.getElementById(id);
const state = { authenticated: false, servers: [], pending: new Set(), socket: null, reconnectTimer: null, reconnectDelay: 1000, loading: false, generation: 0 };
const statuses = { Running: ["En esecuzione", "running"], Stopped: ["Fermo", "stopped"], Starting: ["In avvio", "starting"], Stopping: ["In arresto", "stopping"], Failed: ["Errore", "failed"], Unknown: ["Da verificare", "unknown"] };
class ApiError extends Error { constructor(message, status) { super(message); this.status = status; } }
async function api(path, body) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 12000);
    try {
        const response = await fetch(path, {
            method: body === undefined ? "GET" : "POST", credentials: "same-origin", cache: "no-store", signal: controller.signal,
            headers: body === undefined ? { Accept: "application/json" } : { Accept: "application/json", "Content-Type": "application/json" },
            ...(body === undefined ? {} : { body: JSON.stringify(body) })
        });
        const data = await response.json().catch(() => null);
        recordEvent("HTTP", `${body === undefined ? "GET" : "POST"} ${path} → ${response.status}`);
        if (!response.ok) {
            const messages = { 401: "Accesso richiesto o credenziali non valide.", 403: "Operazione non consentita. Controlla il token di invito.", 404: "Server non trovato.", 429: "Troppi tentativi. Attendi prima di riprovare.", 500: "Il server non ha completato la richiesta. Riprova." };
            throw new ApiError(messages[response.status] || data?.error || `Richiesta non riuscita (${response.status}).`, response.status);
        }
        if (!data) throw new Error("Risposta del server non valida.");
        return data;
    } catch (error) {
        if (error.name === "AbortError") throw new Error("Il server impiega troppo tempo a rispondere. Riprova.");
        if (error instanceof TypeError) throw new Error("Connessione non disponibile. Controlla la rete e riprova.");
        throw error;
    } finally { clearTimeout(timeout); }
}
function notice(message, error = false) {
    $("notice").textContent = message;
    $("notice").classList.toggle("error", error);
    $("notice").hidden = !message;
}
function switchAuth(register) {
    $("loginForm").hidden = register;
    $("registerForm").hidden = !register;
    $("loginTab").setAttribute("aria-pressed", String(!register));
    $("registerTab").setAttribute("aria-pressed", String(register));
}
function setSession(authenticated, username) {
    state.authenticated = authenticated;
    $("loadingPanel").hidden = true;
    $("authPanel").hidden = authenticated;
    $("serverPanel").hidden = !authenticated;
    $("testPanel").hidden = !authenticated;
    $("logoutButton").hidden = !authenticated;
    $("refreshButton").hidden = !authenticated;
    $("sessionLabel").textContent = authenticated ? (username || "Sessione attiva") : "Accesso richiesto";
    stopServersSocket();
    if (authenticated) {
        connectServersSocket();
    } else {
        closeConsole();
        $("consoleOutput").textContent = "Apri una console per visualizzare i log.";
        $("consoleCommand").value = "";
        state.generation++;
        state.servers = [];
        state.pending.clear();
        $("serverGrid").replaceChildren();
        $("navCount").textContent = "—";
        if ($("confirmDialog").open) $("confirmDialog").close("cancel");
    }
}
function stopServersSocket() {
    clearTimeout(state.reconnectTimer);
    const socket = state.socket;
    state.socket = null;
    if (socket) socket.close();
    setServersConnection("Disconnesso");
}
function connectServersSocket() {
    if (!state.authenticated || state.socket) return;
    const socket = new WebSocket(`${location.protocol === "https:" ? "wss:" : "ws:"}//${location.host}/ws/servers`);
    state.socket = socket;
    setServersConnection("Connessione…");
    socket.addEventListener("open", () => {
        if (state.socket !== socket) return;
        setServersConnection("Connesso");
        recordEvent("STATO", "WebSocket /ws/servers connesso");
    });
    socket.addEventListener("error", () => {
        if (state.socket === socket) recordEvent("ERRORE", "WebSocket stato: errore di connessione");
    });
    socket.addEventListener("message", (event) => {
        if (state.socket !== socket || !state.authenticated) return;
        try {
            const message = JSON.parse(event.data);
            if (message.type === "servers.snapshot") {
                if (!Array.isArray(message.servers) || message.servers.some((s) => !s || typeof s.name !== "string" || !Number.isSafeInteger(s.id))) return;
                state.servers = message.servers;
                state.reconnectDelay = 1000;
            } else if (message.type === "server.status") {
                const server = state.servers.find((s) => s.id === message.serverId);
                if (!server || typeof message.status !== "string") return;
                server.status = message.status;
            } else return;
            recordEvent("STATO", event.data);
            renderServers();
            $("updatedAt").textContent = `Ultimo aggiornamento alle ${new Date().toLocaleTimeString("it-IT")}`;
        } catch { /* Ignore malformed events. */ }
    });
    socket.addEventListener("close", () => {
        if (state.socket !== socket) return;
        state.socket = null;
        setServersConnection("Riconnessione…");
        recordEvent("STATO", "WebSocket disconnesso; riconnessione automatica");
        if (!state.authenticated) return;
        $("updatedAt").textContent = "Aggiornamenti sospesi · riconnessione in corso…";
        state.reconnectTimer = setTimeout(async () => {
            // HTTP exposes session expiry, which the browser hides on a failed WS handshake.
            await loadServers();
            connectServersSocket();
        }, state.reconnectDelay);
        state.reconnectDelay = Math.min(state.reconnectDelay * 2, 30000);
    });
}
function element(tag, className, text) {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
}
function renderServers() {
    const total = state.servers.length;
    $("totalCount").textContent = total;
    $("runningCount").textContent = state.servers.filter((s) => s.status === "Running").length;
    $("stoppedCount").textContent = state.servers.filter((s) => s.status === "Stopped").length;
    $("navCount").textContent = total;
    $("fleetCount").textContent = total;
    const query = $("serverSearch").value.trim().toLocaleLowerCase("it");
    const servers = state.servers.filter((s) => s.name.toLocaleLowerCase("it").includes(query) || String(s.id).includes(query));
    const grid = $("serverGrid");
    const focused = document.activeElement;
    const focusKey = grid.contains(focused) ? [focused.dataset.id, focused.dataset.action] : null;
    const fragment = document.createDocumentFragment();
    for (const server of servers) {
        const [label, statusClass] = statuses[server.status] || statuses.Unknown;
        const card = element("article", "server-card");
        const art = element("div", "server-art");
        art.setAttribute("aria-hidden", "true");
        art.append(element("span", "server-number", `MONDO / ${String(server.id).padStart(2, "0")}`));
        const content = element("div", "server-content");
        const title = element("div", "server-title");
        const badge = element("span", `badge ${statusClass}`);
        badge.append(element("i", `dot ${statusClass}`), document.createTextNode(label));
        title.append(element("h3", "", server.name), badge);
        content.append(title, element("p", "server-subtitle", `Minecraft · Server #${server.id}`));
        const actions = element("div", "server-actions");
        for (const [action, text] of [["start", "Avvia"], ["stop", "Arresta"], ["restart", "Riavvia"], ["console", "Console"]]) {
            const button = element("button", `button ${action === "start" ? "primary" : "secondary"}`, text);
            button.type = "button";
            button.dataset.id = String(server.id);
            button.dataset.action = action;
            button.setAttribute("aria-label", `${text} ${server.name}`);
            button.disabled = action !== "console" && (state.pending.has(server.id) || ["Starting", "Stopping"].includes(server.status) || (action === "start" && server.status === "Running") || (action !== "start" && server.status === "Stopped"));
            actions.append(button);
        }
        content.append(actions, element("p", "action-note", state.pending.has(server.id) ? "Invio del comando in corso…" : ""));
        card.append(art, content);
        fragment.append(card);
    }
    grid.replaceChildren(fragment);
    if (focusKey) {
        const button = Array.from(grid.querySelectorAll("button")).find((node) => node.dataset.id === focusKey[0] && node.dataset.action === focusKey[1]);
        if (button && !button.disabled) button.focus({ preventScroll: true });
    }
    syncConsoleServers();
    $("emptyState").hidden = servers.length !== 0;
    $("emptyState").querySelector("h3").textContent = query ? "Nessun risultato" : "Nessun server configurato";
    $("emptyState").querySelector("p").textContent = query ? "Prova un altro nome o cerca per ID." : "I server compariranno qui appena saranno disponibili.";
}
async function loadServers(initial = false) {
    if (state.loading) return;
    state.loading = true;
    const generation = state.generation;
    $("refreshButton").disabled = true;
    $("serverGrid").setAttribute("aria-busy", "true");
    try {
        const data = await api("/api/servers");
        if (generation !== state.generation) return;
        if (!Array.isArray(data.servers) || data.servers.some((s) => !s || typeof s.name !== "string" || !Number.isSafeInteger(s.id))) throw new Error("La lista dei server non è valida.");
        // An open socket owns live state; avoid overwriting it with an older HTTP reply.
        if (state.socket?.readyState !== WebSocket.OPEN) state.servers = data.servers;
        if (!state.authenticated) setSession(true);
        renderServers();
        $("updatedAt").textContent = `Ultimo aggiornamento alle ${new Date().toLocaleTimeString("it-IT")}`;
    } catch (error) {
        if (generation !== state.generation) return;
        if (error.status === 401) {
            const expired = state.authenticated;
            setSession(false);
            if (expired) notice("La sessione è scaduta. Accedi di nuovo.", true);
        } else {
            if (initial) setSession(false);
            notice(error.message, true);
        }
    } finally {
        state.loading = false;
        $("refreshButton").disabled = false;
        $("serverGrid").setAttribute("aria-busy", "false");
    }
}
$("loginTab").addEventListener("click", () => switchAuth(false));
$("registerTab").addEventListener("click", () => switchAuth(true));
$("serverSearch").addEventListener("input", renderServers);
$("refreshButton").addEventListener("click", () => void loadServers());
document.addEventListener("visibilitychange", () => { if (!document.hidden && state.authenticated) connectServersSocket(); });
for (const register of [false, true]) {
    const form = $(register ? "registerForm" : "loginForm");
    form.addEventListener("submit", async (event) => {
        event.preventDefault();
        const prefix = register ? "register" : "login";
        const username = $(`${prefix}Username`).value.trim();
        const password = $(`${prefix}Password`).value;
        if (!username || !password) { notice("Inserisci nome utente e password.", true); return; }
        const payload = { username, password };
        if (register) {
            payload.token = $("registerToken").value.trim();
            if (!payload.token) { notice("Inserisci il token di invito.", true); return; }
        }
        const button = form.querySelector("button[type=submit]");
        button.disabled = true;
        $("loginTab").disabled = $("registerTab").disabled = true;
        notice(register ? "Creazione dell’account…" : "Accesso in corso…");
        try {
            const result = await api(`/api/auth/${register ? "register" : "login"}`, payload);
            $(`${prefix}Password`).value = "";
            if (register) {
                $("registerToken").value = "";
                $("loginUsername").value = username;
                switchAuth(false);
                notice("Account creato. Ora puoi accedere.");
                $("loginPassword").focus();
            } else {
                setSession(true, result.username);
                notice("");
                await loadServers();
            }
        } catch (error) { notice(error.message, true); }
        finally { button.disabled = false; $("loginTab").disabled = $("registerTab").disabled = false; }
    });
}
$("logoutButton").addEventListener("click", async () => {
    $("logoutButton").disabled = true;
    try {
        await api("/api/auth/logout", {});
        setSession(false);
        switchAuth(false);
        notice("Hai effettuato l’uscita.");
    } catch (error) { notice(error.message, true); }
    finally { $("logoutButton").disabled = false; }
});
function confirmAction(server, action) {
    const dialog = $("confirmDialog");
    if (dialog.open) return Promise.resolve(false);
    $("confirmTitle").textContent = `${action === "stop" ? "Arrestare" : "Riavviare"} ${server.name}?`;
    $("confirmDescription").textContent = action === "stop" ? "Il server verrà fermato e i giocatori saranno disconnessi." : "Il server verrà riavviato. I giocatori saranno disconnessi temporaneamente.";
    $("confirmAction").textContent = action === "stop" ? "Arresta server" : "Riavvia server";
    dialog.returnValue = "cancel";
    return new Promise((resolve) => {
        dialog.addEventListener("close", () => resolve(dialog.returnValue === "confirm"), { once: true });
        dialog.showModal();
    });
}
$("serverGrid").addEventListener("click", async (event) => {
    const button = event.target.closest("button[data-action]");
    if (!button || button.disabled || !state.authenticated) return;
    const server = state.servers.find((s) => String(s.id) === button.dataset.id);
    const action = button.dataset.action;
    if (server && action === "console") { openConsole(server.id); return; }
    if (!server || !["start", "stop", "restart"].includes(action) || state.pending.has(server.id)) return;
    if (action !== "start" && !await confirmAction(server, action)) return;
    if (!state.authenticated || state.pending.has(server.id)) return;
    const generation = state.generation;
    state.pending.add(server.id);
    renderServers();
    try {
        const result = await api(`/api/servers/${encodeURIComponent(server.id)}/${action}`, {});
        if (generation !== state.generation) return;
        if (result.success !== true) throw new Error("Il comando non è stato accettato dal server.");
        const label = { start: "Avvio", stop: "Arresto", restart: "Riavvio" }[action];
        notice(`${label} richiesto per ${server.name}. Lo stato si aggiornerà automaticamente.`);
    } catch (error) {
        if (generation !== state.generation) return;
        if (error.status === 401) setSession(false);
        notice(error.message, true);
    } finally {
        if (generation === state.generation) {
            state.pending.delete(server.id);
            if (state.authenticated) renderServers();
        }
    }
});
$("reconnectServers").addEventListener("click", () => {
    stopServersSocket();
    state.reconnectDelay = 1000;
    connectServersSocket();
});
void loadServers(true);
