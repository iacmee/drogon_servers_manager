# PaperPilot

**Un pannello web per gestire server Minecraft, scritto in C++20.**

PaperPilot riunisce stato dei server, avvio, arresto e console in un'unica interfaccia. Il backend usa Drogon per le API HTTP e i WebSocket, SQLite per utenti e inviti e D-Bus per comunicare con systemd. Il frontend è realizzato in HTML, CSS e JavaScript, senza una fase di build dedicata.

Il progetto esplora l'integrazione tra programmazione asincrona in C++, gestione dei processi Linux e comunicazione in tempo reale: dalle coroutine per l'accesso al database al controllo del flusso della console.

## Funzionalità

- **Dashboard multi-server** con ricerca, conteggi e aggiornamenti di stato via WebSocket.
- **Avvio, arresto e riavvio** attraverso API HTTP e unità systemd dell'utente.
- **Console interattiva** con streaming dell'output, cronologia limitata e invio di comandi dal browser.
- **Registrazione su invito**, con numero di utilizzi e durata configurabili tramite API.
- **Autenticazione a sessione**, password protette con libsodium e inviti memorizzati come hash.
- **HTTPS diretto o reverse proxy**, con origini consentite configurabili.
- **Suite di regressione** dedicate ad autenticazione, HTTP, WebSocket, console e proxy.

## Architettura

```text
Browser · HTML / CSS / JavaScript
  │
  ├── HTTP: autenticazione e azioni sui server
  └── WebSocket: stato e console
          │
     Backend C++20 / Drogon
          ├── SQLite: utenti e token di invito
          ├── libsodium: password e segreti di accesso
          └── D-Bus / systemd --user
                    └── Processi Java / PaperMC
```

L'avvio crea un'unità systemd transitoria e collega input e output del processo tramite pipe. I cambiamenti di stato arrivano dai segnali D-Bus e vengono inoltrati ai browser connessi. L'hashing delle password viene eseguito su un pool di worker con coda limitata, per mantenere disponibili gli event loop HTTP.

La console applica limiti a comandi, cronologia e code. L'output usa un meccanismo Ping/Pong per regolare l'invio ai client lenti ed evitare accumuli illimitati in memoria.

## Compilazione

### Requisiti

- Linux con systemd e un bus D-Bus di sessione per controllare i server.
- Compilatore C++20, Make, CMake, Git, `pkg-config` e `patch`.
- Librerie di sviluppo di JsonCpp, SQLite3, libsodium, libsystemd, OpenSSL e zlib.
- Java compatibile con la versione di PaperMC utilizzata.
- Python 3 e il pacchetto `websockets` per i test di integrazione.

Su Debian/Ubuntu, le dipendenze di compilazione possono essere installate con:

```bash
sudo apt install build-essential cmake git pkg-config patch \
  libjsoncpp-dev libsqlite3-dev libsodium-dev libsystemd-dev \
  libssl-dev zlib1g-dev uuid-dev openssl python3 python3-venv
```

### Drogon e build del progetto

Il Makefile richiede una build locale di Drogon al commit
`4c5430757ea5451a7c38fbbef4b4bef7dbb47f2f`, con le patch incluse nel repository. Queste intervengono sui messaggi WebSocket frammentati, sul parsing HTTP e sulla rotazione delle sessioni.

```bash
git clone https://github.com/iacmee/drogon_servers_manager.git
cd drogon_servers_manager

git clone https://github.com/drogonframework/drogon.git .deps/drogon-checkout
git -C .deps/drogon-checkout checkout 4c5430757ea5451a7c38fbbef4b4bef7dbb47f2f
git -C .deps/drogon-checkout submodule update --init --recursive

bash tools/build_drogon.sh "$PWD/.deps/drogon-checkout"
make -j2
```

Lo script costruisce e installa Drogon in `.deps/`, senza modificare le librerie di sistema. Può anche utilizzare un checkout già presente che contenga il commit richiesto e il relativo submodule Trantor. Il risultato della compilazione è `servers_manager`.

## Primo avvio locale

Le configurazioni presenti in `config/` contengono percorsi e impostazioni dell'ambiente di sviluppo. Per provare il pannello, crea `config/config.local.json` con questa configurazione indipendente, senza server Minecraft:

```json
{
  "app": {
    "bind": "127.0.0.1",
    "port": 8443,
    "threads": 2,
    "java_path": "/usr/bin/java",
    "document_root": "static",
    "backup_root": "db/test/backups",
    "database_file": "db/test/database.db",
    "allowed_origins": ["https://localhost:8443"],
    "https": {
      "enabled": true,
      "cert": "certs/test/server.crt",
      "key": "certs/test/server.key"
    },
    "session": {"enabled": true, "timeout": 3600}
  },
  "plugins": [
    {
      "name": "drogon::plugin::Hodor",
      "dependencies": [],
      "config": {
        "algorithm": "sliding_window",
        "urls": ["^/api/auth/login$"],
        "time_unit": 60,
        "capacity": 1000,
        "ip_capacity": 20,
        "user_capacity": 15,
        "multi_threads": true,
        "use_real_ip_resolver": false,
        "trust_ips": []
      }
    }
  ],
  "servers": []
}
```

Prepara le directory e il certificato autofirmato, poi avvia dalla radice del repository:

```bash
make prepare-test-config
./servers_manager config/config.local.json
```

Apri **https://localhost:8443**. Il certificato locale richiede un'eccezione nel browser. Al primo avvio, se il database non contiene utenti, il processo stampa un invito iniziale: è valido per **5 minuti e un solo utilizzo**. Usalo nella scheda di registrazione e poi accedi con le credenziali create.

Le nuove password richiedono almeno 12 caratteri Unicode e un simbolo di punteggiatura ASCII; il limite è 128 byte. Per fermare il pannello usa `Ctrl+C`.

## Configurare un server Minecraft

Aggiungi un oggetto all'array `servers` della configurazione locale:

```json
{
  "id": 1,
  "name": "survival",
  "directory": "/percorso/assoluto/survival",
  "service": "survival.service",
  "logPath": "/percorso/assoluto/survival/logs/latest.log",
  "jarPath": "/percorso/assoluto/survival/paper.jar"
}
```

Prepara la directory, il JAR e l'accettazione dell'EULA di Minecraft prima dell'avvio. Imposta `app.java_path` sul runtime appropriato e riavvia il pannello dopo aver modificato la configurazione.

Gli ID devono essere univoci; usa interi positivi per accedere anche alla console WebSocket. Il codice deriva il nome dell'unità da `name` aggiungendo `.service`: scegli un nome valido per systemd e mantieni coerente il campo `service`.

Il backend si collega al **bus dell'utente**: eseguilo con l'utente che deve gestire i processi Minecraft e con una sessione systemd utente disponibile. La console è collegata alle pipe dei processi avviati dal pannello; il collegamento a processi esterni già avviati non è implementato.

## API

| Metodo | Percorso | Funzione |
| --- | --- | --- |
| POST | `/api/auth/register` | Registrazione con `username`, `password` e `token` |
| POST | `/api/auth/login` | Accesso con `username` e `password` |
| POST | `/api/auth/logout` | Chiusura della sessione |
| POST | `/api/auth/new_token` | Creazione di inviti con `number_of_valid_uses` e `lifetime` in minuti |
| GET | `/api/servers` | Elenco e stato dei server |
| POST | `/api/servers/{id}/start` | Avvio |
| POST | `/api/servers/{id}/stop` | Arresto |
| POST | `/api/servers/{id}/restart` | Riavvio |
| WebSocket | `/ws/servers` | Aggiornamenti di stato |
| WebSocket | `/ws/console/{id}` | Output e comandi della console |

Login e registrazione sono pubblici; le altre operazioni richiedono una sessione autenticata. Le richieste HTTP che modificano lo stato richiedono un header `Origin` corrispondente a una delle origini consentite. Anche i WebSocket verificano l'origine. I client da riga di comando devono inviare esplicitamente l'header e conservare i cookie di accesso.

## Sicurezza e deployment

Sono presenti cookie `Secure`, `HttpOnly` e `SameSite=Lax`, un segreto di accesso rinnovato al login, controlli di origine, Content Security Policy e limiti sui tentativi di autenticazione. Il consumo degli inviti e la creazione degli utenti avvengono in una transazione SQLite.

Per il deployment, usa HTTPS diretto oppure un reverse proxy con terminazione TLS. Nel secondo caso, configura le origini pubbliche esatte e limita la fiducia negli header inoltrati al solo proxy. Mantieni `.deps/install/lib` accanto all'eseguibile secondo la struttura del repository: il binario carica le librerie tramite RPATH relativo.

I dettagli e i limiti delle protezioni sono descritti in:

- [Autenticazione e inviti](docs/auth-security.txt)
- [HTTP, cookie e controlli di origine](docs/http-security.txt)
- [Console, limiti e patch WebSocket](docs/console-security.txt)
- [Configurazione del reverse proxy](docs/reverse-proxy.txt)

## Test

Con le dipendenze compilate, prepara un ambiente Python per le suite di integrazione (`websockets` è stato verificato con la versione 10.4):

```bash
python3 -m venv .deps/test-venv
source .deps/test-venv/bin/activate
python -m pip install websockets==10.4

make test-console test-auth test-security test-http-security test-proxy test-audit
```

Le suite coprono limiti della console, autenticazione, uso concorrente degli inviti, sessioni, origini HTTP/WebSocket, messaggi frammentati, proxy fidati e regressioni del parser HTTP. I test HTTP usano fixture e database temporanei; per quelli di integrazione è necessario poter aprire socket locali.

## Struttura del repository

```text
src/          Backend, integrazione systemd, console e controller API
include/      Interfacce C++, filtri e limiti applicativi
models/       Modelli ORM per utenti e inviti
static/       Dashboard e console nel browser
config/       Configurazioni JSON
patches/      Patch applicate alla versione fissata di Drogon
tools/        Build locale delle dipendenze
tests/        Test C++ e suite Python di integrazione
docs/         Note tecniche e di deployment
```

## Stato del progetto

PaperPilot è in sviluppo. La configurazione dei server avviene tramite file JSON; creazione dei mondi, upload dei JAR, backup e gestione granulare dei permessi non sono attualmente esposti dal pannello. Il campo `backup_root` è previsto nella configurazione, ma non corrisponde a una funzionalità di backup implementata.
