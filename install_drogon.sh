#!/usr/bin/env bash

set -euo pipefail


REPO_URL="https://github.com/drogonframework/drogon.git"
SOURCE_DIR="$HOME/src/drogon"
BUILD_DIR="$SOURCE_DIR/build"

STAMP_DIR="/usr/local/share/drogon"
VERSION_FILE="$STAMP_DIR/installed_version"
COMMIT_FILE="$STAMP_DIR/installed_commit"

FORCE=0

if [ "${1:-}" = "--force" ]; then
	FORCE=1
fi


# ------------------------------------------------------------
# Git deve esserci per poter controllare la versione remota
# ------------------------------------------------------------

if ! command -v git >/dev/null 2>&1; then
	echo "==> Installazione git"

	sudo apt update
	sudo apt install -y git
fi


# ------------------------------------------------------------
# Repository
# ------------------------------------------------------------

mkdir -p "$(dirname "$SOURCE_DIR")"


if [ ! -d "$SOURCE_DIR/.git" ]; then
	echo "==> Clone repository Drogon"

	git clone "$REPO_URL" "$SOURCE_DIR"
else
	cd "$SOURCE_DIR"

	if [ -n "$(git status --porcelain)" ]; then
		echo "ERRORE: il repository contiene modifiche locali:"
		echo
		git status --short
		exit 1
	fi

	echo "==> Controllo nuove release"

	git fetch origin --tags --prune
fi


cd "$SOURCE_DIR"


# ------------------------------------------------------------
# Trova l'ultima release stabile
#
# Accetta:
#   v1.9.0
#   v1.9.11
#
# Ignora:
#   v1.10.0-rc1
#   v1.10.0-beta
# ------------------------------------------------------------

LATEST_TAG=$(
	git tag --list 'v*' |
	grep -E '^v[0-9]+\.[0-9]+\.[0-9]+$' |
	sort -V |
	tail -n 1
)


if [ -z "$LATEST_TAG" ]; then
	echo "ERRORE: nessuna release stabile trovata"
	exit 1
fi


LATEST_COMMIT=$(git rev-list -n 1 "$LATEST_TAG")


echo
echo "Ultima release disponibile: $LATEST_TAG"
echo "Commit: $LATEST_COMMIT"


# ------------------------------------------------------------
# Controlla versione installata
# ------------------------------------------------------------

INSTALLED_VERSION=""

if [ -f "$VERSION_FILE" ]; then
	INSTALLED_VERSION=$(cat "$VERSION_FILE")
fi


LIBRARY_OK=0

if [ -e /usr/local/lib/libdrogon.so ] &&
   [ -e /usr/local/lib/libtrantor.so ]; then
	LIBRARY_OK=1
fi


if [ "$FORCE" -eq 0 ] &&
   [ "$INSTALLED_VERSION" = "$LATEST_TAG" ] &&
   [ "$LIBRARY_OK" -eq 1 ]; then

	echo
	echo "Drogon è già aggiornato: $INSTALLED_VERSION"
	echo "Nessuna compilazione necessaria."
	echo

	exit 0
fi


if [ -n "$INSTALLED_VERSION" ]; then
	echo
	echo "Aggiornamento:"
	echo "  installata : $INSTALLED_VERSION"
	echo "  disponibile: $LATEST_TAG"
else
	echo
	echo "Drogon non risulta installato dallo script."
	echo "Verrà installata: $LATEST_TAG"
fi


# ------------------------------------------------------------
# Dipendenze necessarie
# ------------------------------------------------------------

PACKAGES=(
	git
	build-essential
	cmake
	libjsoncpp-dev
	uuid-dev
	zlib1g-dev
	openssl
	libssl-dev
)


MISSING_PACKAGES=()


for package in "${PACKAGES[@]}"; do

	if ! dpkg-query -W \
		-f='${Status}' "$package" 2>/dev/null |
		grep -q "ok installed"; then

		MISSING_PACKAGES+=("$package")
	fi

done


if [ "${#MISSING_PACKAGES[@]}" -gt 0 ]; then

	echo
	echo "==> Installazione dipendenze mancanti"

	sudo apt update
	sudo apt install -y "${MISSING_PACKAGES[@]}"

else
	echo
	echo "==> Dipendenze già installate"
fi


# ------------------------------------------------------------
# Rimuove eventuale Drogon gestito da APT
# ------------------------------------------------------------

APT_DROGON_PACKAGES=()


for package in libdrogon-dev libdrogon1t64; do

	if dpkg-query -W \
		-f='${Status}' "$package" 2>/dev/null |
		grep -q "ok installed"; then

		APT_DROGON_PACKAGES+=("$package")
	fi

done


if [ "${#APT_DROGON_PACKAGES[@]}" -gt 0 ]; then

	echo
	echo "==> Rimozione Drogon installato tramite APT"

	sudo apt remove -y "${APT_DROGON_PACKAGES[@]}"

fi


# ------------------------------------------------------------
# Checkout release
# ------------------------------------------------------------

echo
echo "==> Checkout $LATEST_TAG"

git checkout --detach "$LATEST_TAG"


echo "==> Aggiornamento submodule"

git submodule sync --recursive
git submodule update --init --recursive


# ------------------------------------------------------------
# Build pulita
# ------------------------------------------------------------

echo
echo "==> Pulizia build"

rm -rf "$BUILD_DIR"


echo "==> Configurazione"

cmake \
	-S "$SOURCE_DIR" \
	-B "$BUILD_DIR" \
	-DCMAKE_BUILD_TYPE=Release \
	-DBUILD_SHARED_LIBS=ON \
	-DCMAKE_INSTALL_PREFIX=/usr/local


echo
echo "==> Compilazione"

cmake --build "$BUILD_DIR" \
	--parallel "$(nproc)"


# ------------------------------------------------------------
# Elimina eventuali vecchi file installati manualmente
# ------------------------------------------------------------

echo
echo "==> Pulizia vecchia installazione"

sudo rm -rf /usr/local/include/drogon
sudo rm -rf /usr/local/include/trantor

sudo rm -f /usr/local/lib/libdrogon.a
sudo rm -f /usr/local/lib/libtrantor.a

sudo rm -f /usr/local/lib/libdrogon.so*
sudo rm -f /usr/local/lib/libtrantor.so*

sudo rm -rf /usr/local/lib/cmake/Drogon
sudo rm -rf /usr/local/lib/cmake/Trantor


# ------------------------------------------------------------
# Installazione
# ------------------------------------------------------------

echo
echo "==> Installazione $LATEST_TAG"

sudo cmake --install "$BUILD_DIR"

sudo ldconfig


# ------------------------------------------------------------
# Verifica
# ------------------------------------------------------------

if [ ! -e /usr/local/lib/libdrogon.so ]; then
	echo "ERRORE: libdrogon.so non trovata dopo l'installazione"
	exit 1
fi


if [ ! -e /usr/local/lib/libtrantor.so ]; then
	echo "ERRORE: libtrantor.so non trovata dopo l'installazione"
	exit 1
fi


# ------------------------------------------------------------
# Registra release installata
# ------------------------------------------------------------

sudo mkdir -p "$STAMP_DIR"

echo "$LATEST_TAG" |
	sudo tee "$VERSION_FILE" >/dev/null

echo "$LATEST_COMMIT" |
	sudo tee "$COMMIT_FILE" >/dev/null


# ------------------------------------------------------------
# Risultato
# ------------------------------------------------------------

echo
echo "========================================"
echo " Drogon installato correttamente"
echo "========================================"
echo
echo "Release : $LATEST_TAG"
echo "Commit  : $LATEST_COMMIT"
echo

echo "Librerie:"
ls -l /usr/local/lib/libdrogon.so*
ls -l /usr/local/lib/libtrantor.so*

echo
echo "Supporto OpenSSL:"
ldd /usr/local/lib/libtrantor.so |
	grep -E 'libssl|libcrypto' || true

echo
echo "Completato."
