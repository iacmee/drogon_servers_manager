"use strict";
const testConsole = { socket: null, serverId: null, events: [] };
function recordEvent(kind, message) {
    testConsole.events.push(`${new Date().toLocaleTimeString("it-IT")}  ${kind}  ${message}`);
    testConsole.events = testConsole.events.slice(-100);
    const output = document.getElementById("eventOutput");
    output.textContent = testConsole.events.join("\n");
    output.scrollTop = output.scrollHeight;
}
function setServersConnection(label) {
    document.getElementById("serversConnection").textContent = label;
}
function updateConsoleControls() {
    const server = state.servers.find((s) => s.id === testConsole.serverId);
    const connected = testConsole.socket?.readyState === WebSocket.OPEN;
    const canSend = state.authenticated && connected && server && !["Stopped", "Stopping", "Failed"].includes(server.status);
    document.getElementById("consoleCommand").disabled = !canSend;
    document.getElementById("sendConsoleCommand").disabled = !canSend;
    document.getElementById("disconnectConsole").disabled = !testConsole.socket;
}
function syncConsoleServers() {
    const select = document.getElementById("consoleServer");
    const selected = select.value;
    select.replaceChildren(new Option("Seleziona un server", ""));
    for (const server of state.servers) select.add(new Option(`${server.name} (#${server.id}) · ${server.status}`, String(server.id)));
    if (state.servers.some((s) => String(s.id) === selected)) select.value = selected;
    if (testConsole.serverId !== null && !state.servers.some((s) => s.id === testConsole.serverId)) closeConsole();
    updateConsoleControls();
}
function closeConsole() {
    const socket = testConsole.socket;
    testConsole.socket = null;
    testConsole.serverId = null;
    if (socket) {
        socket.close();
        recordEvent("CONSOLE", "Connessione chiusa dal pannello");
    }
    document.getElementById("consoleConnection").textContent = "Disconnessa";
    updateConsoleControls();
}
function appendConsole(text) {
    const output = document.getElementById("consoleOutput");
    // Bound memory even when a busy server emits logs continuously.
    output.textContent = (output.textContent + text).slice(-200000);
    if (document.getElementById("consoleAutoscroll").checked) output.scrollTop = output.scrollHeight;
}
function openConsole(id) {
    if (!state.authenticated || !state.servers.some((s) => s.id === id)) return;
    closeConsole();
    document.getElementById("consoleServer").value = String(id);
    document.getElementById("consoleOutput").textContent = "";
    document.getElementById("consoleCommand").value = "";
    const path = `/ws/console/${encodeURIComponent(id)}`;
    document.getElementById("consoleEndpoint").textContent = path;
    document.getElementById("consoleConnection").textContent = "Connessione…";
    const socket = new WebSocket(`${location.protocol === "https:" ? "wss:" : "ws:"}//${location.host}${path}`);
    testConsole.socket = socket;
    testConsole.serverId = id;
    recordEvent("CONSOLE", `Apertura ${path}`);
    updateConsoleControls();
    socket.addEventListener("open", () => {
        if (testConsole.socket !== socket) return;
        document.getElementById("consoleConnection").textContent = `Connessa · #${id}`;
        document.getElementById("consoleHint").textContent = "Comandi inviati direttamente al server. L’invio non costituisce conferma di esecuzione: controlla i log.";
        recordEvent("CONSOLE", `Connessa al server #${id}`);
        updateConsoleControls();
        if (!document.getElementById("consoleCommand").disabled) document.getElementById("consoleCommand").focus();
    });
    socket.addEventListener("message", (event) => {
        if (testConsole.socket === socket && typeof event.data === "string") appendConsole(event.data);
    });
    socket.addEventListener("error", () => {
        if (testConsole.socket !== socket) return;
        recordEvent("ERRORE", `Console #${id}: connessione non riuscita`);
    });
    socket.addEventListener("close", (event) => {
        if (testConsole.socket !== socket) return;
        testConsole.socket = null;
        testConsole.serverId = null;
        document.getElementById("consoleConnection").textContent = "Disconnessa";
        document.getElementById("consoleHint").textContent = "Connessione chiusa. Premi Apri console per riconnetterti e ricevere lo storico disponibile.";
        recordEvent("CONSOLE", `Server #${id}: connessione chiusa (${event.code})`);
        updateConsoleControls();
    });
    document.getElementById("testPanel").scrollIntoView({ block: "start" });
}
document.getElementById("connectConsole").addEventListener("click", () => {
    const value = document.getElementById("consoleServer").value;
    if (value) openConsole(Number(value));
    else document.getElementById("consoleHint").textContent = "Seleziona prima un server.";
});
document.getElementById("disconnectConsole").addEventListener("click", closeConsole);
document.getElementById("clearConsole").addEventListener("click", () => { document.getElementById("consoleOutput").textContent = ""; });
document.getElementById("clearEvents").addEventListener("click", () => {
    testConsole.events = [];
    document.getElementById("eventOutput").textContent = "";
});
document.getElementById("consoleCommandForm").addEventListener("submit", (event) => {
    event.preventDefault();
    updateConsoleControls();
    const input = document.getElementById("consoleCommand");
    const command = input.value.trim();
    if (input.disabled || !command) return;
    if (/[\r\n]/.test(command)) {
        document.getElementById("consoleHint").textContent = "Invia un solo comando per volta.";
        return;
    }
    try {
        testConsole.socket.send(command);
        recordEvent("COMANDO", `Inviato al server #${testConsole.serverId}`);
        document.getElementById("consoleHint").textContent = "Comando inviato. Controlla l’output per il risultato.";
        input.value = "";
        input.focus();
    } catch {
        document.getElementById("consoleHint").textContent = "Invio non riuscito: riconnetti la console.";
    }
});
