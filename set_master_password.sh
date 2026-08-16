#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Uso: $0 <database.sqlite>"
    exit 1
fi

DB="$1"

for cmd in sqlite3 argon2 openssl; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "Errore: comando '$cmd' non trovato."
        exit 1
    fi
done

# Crea il file DB se non esiste e crea la tabella users se manca.
sqlite3 "$DB" <<'SQL'
CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY,
    username TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    created_at INTEGER NOT NULL
);
SQL

# Verifica se esiste già il record speciale.
EXISTING=$(
    sqlite3 -separator ' | ' "$DB" "
        SELECT id, username
        FROM users
        WHERE id = 0
           OR username = 'register_new_user';
    "
)

if [[ -n "$EXISTING" ]]; then
    echo
    echo "ATTENZIONE: esiste già un record riservato:"
    echo
    echo "$EXISTING"
    echo
    echo "Il record verrà eliminato e sostituito."

    read -r -p "Continuare? [y/N] " answer

    case "$answer" in
        y|Y|yes|YES)
            ;;
        *)
            echo "Operazione annullata."
            exit 0
            ;;
    esac
fi

while true; do
    echo

    read -r -s -p "Master password: " PASSWORD
    echo

    errors=0

    if (( ${#PASSWORD} < 13 )); then
        echo "- Deve essere lunga almeno 13 caratteri."
        errors=1
    fi

    if ! printf '%s' "$PASSWORD" | grep -q '[[:lower:]]'; then
        echo "- Deve contenere almeno una lettera minuscola."
        errors=1
    fi

    if ! printf '%s' "$PASSWORD" | grep -q '[[:upper:]]'; then
        echo "- Deve contenere almeno una lettera maiuscola."
        errors=1
    fi

    if ! printf '%s' "$PASSWORD" | grep -q '[[:digit:]]'; then
        echo "- Deve contenere almeno un numero."
        errors=1
    fi

    if ! printf '%s' "$PASSWORD" | grep -q '[[:punct:]]'; then
        echo "- Deve contenere almeno un carattere speciale."
        errors=1
    fi

    if (( errors != 0 )); then
        echo
        echo "Password non valida. Riprova."
        unset PASSWORD
        continue
    fi

    read -r -s -p "Ripeti la master password: " PASSWORD2
    echo

    if [[ "$PASSWORD" != "$PASSWORD2" ]]; then
        echo "Le password non coincidono."
        unset PASSWORD PASSWORD2
        continue
    fi

    unset PASSWORD2
    break
done

# Salt casuale da 16 byte.
SALT="$(openssl rand -hex 16)"

# Argon2id:
# -t 3  = 3 iterazioni
# -m 16 = 64 MiB
# -p 1  = parallelismo 1
# -e    = formato encoded completo
PASSWORD_HASH="$(
    printf '%s' "$PASSWORD" |
        argon2 "$SALT" -id -t 3 -m 16 -p 1 -e
)"

unset PASSWORD
unset SALT

if [[ -z "$PASSWORD_HASH" ]]; then
    echo "Errore durante la generazione dell'hash."
    exit 1
fi

# Escape dell'apostrofo per SQL.
PASSWORD_HASH_SQL="${PASSWORD_HASH//\'/\'\'}"

sqlite3 "$DB" <<SQL
BEGIN IMMEDIATE;

DELETE FROM users
WHERE id = 0
   OR username = 'register_new_user';

INSERT INTO users (
    id,
    username,
    password_hash,
    created_at
)
VALUES (
    0,
    'register_new_user',
    '$PASSWORD_HASH_SQL',
    CAST(strftime('%s','now') AS INTEGER)
);

COMMIT;
SQL

unset PASSWORD_HASH
unset PASSWORD_HASH_SQL

echo
echo "Master password registrata correttamente."
echo "Database: $DB"
echo "Utente:   register_new_user"
echo "ID:       0"