#!/usr/bin/env bash

set -euo pipefail
TARGET="$HOME/.local/bin"

mkdir -p "$TARGET"

echo "Downloading kpm..."

URL=$(curl -s "https://api.github.com/repos/lPrimemaster/kpm/releases/latest" | grep '"browser_download_url"' | grep 'kpm"' | cut -d '"' -f 4)

if [[ -z "$URL" ]]; then
	echo "Failed to fetch kpm."
	exit 1
fi

curl -L "$URL" -o "/tmp/kpm.tempbin"

mv "/tmp/kpm.tempbin" "$TARGET/kpm"
chmod +x "$TARGET/kpm"

echo "Successfuly installed kpm."
echo "Ensure that '~/.local/bin' is in your path."
