#!/bin/zsh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PY311="/opt/homebrew/bin/python3.11"
CLI="$ROOT/.pio-venv"
LOCAL="$ROOT/.pio-local"
CORE="$LOCAL/core"
PACKAGES="$LOCAL/packages"
PLATFORMS="$LOCAL/platforms"
LIBDEPS="$LOCAL/libdeps"
CACHE="$LOCAL/cache"
GLOBAL_PACKAGES="$HOME/.platformio/packages"
GLOBAL_PLATFORM="$HOME/.platformio/offline-platforms/pioarduino-espressif32-55.03.37"
LOCAL_PLATFORM="$PLATFORMS/pioarduino-espressif32-55.03.37"

# Only this script process and its children get the proxy variables.
PROXY_DUMP="$(/usr/sbin/scutil --proxy 2>/dev/null || true)"
HTTP_ENABLED="$(printf '%s\n' "$PROXY_DUMP" | awk '/HTTPEnable :/ {print $3; exit}')"
HTTPS_ENABLED="$(printf '%s\n' "$PROXY_DUMP" | awk '/HTTPSEnable :/ {print $3; exit}')"
HTTP_HOST="$(printf '%s\n' "$PROXY_DUMP" | awk '/HTTPProxy :/ {print $3; exit}')"
HTTP_PORT="$(printf '%s\n' "$PROXY_DUMP" | awk '/HTTPPort :/ {print $3; exit}')"
HTTPS_HOST="$(printf '%s\n' "$PROXY_DUMP" | awk '/HTTPSProxy :/ {print $3; exit}')"
HTTPS_PORT="$(printf '%s\n' "$PROXY_DUMP" | awk '/HTTPSPort :/ {print $3; exit}')"

if [[ "$HTTP_ENABLED" == "1" && -n "$HTTP_HOST" && -n "$HTTP_PORT" ]]; then
  export HTTP_PROXY="http://${HTTP_HOST}:${HTTP_PORT}"
  export http_proxy="$HTTP_PROXY"
fi
if [[ "$HTTPS_ENABLED" == "1" && -n "$HTTPS_HOST" && -n "$HTTPS_PORT" ]]; then
  export HTTPS_PROXY="http://${HTTPS_HOST}:${HTTPS_PORT}"
  export https_proxy="$HTTPS_PROXY"
fi
export NO_PROXY="127.0.0.1,localhost,::1"
export no_proxy="$NO_PROXY"
export PIP_DEFAULT_TIMEOUT=120
export UV_HTTP_TIMEOUT=120

if [[ ! -x "$PY311" ]]; then
  echo "ERROR: required Python 3.11 was not found at $PY311"
  echo "This script will not install or modify any global Python."
  exit 2
fi

if [[ ! -d "$GLOBAL_PLATFORM" ]]; then
  echo "ERROR: verified offline PIOArduino platform is missing:"
  echo "  $GLOBAL_PLATFORM"
  echo "No global installation will be attempted."
  exit 3
fi

mkdir -p "$CORE" "$PACKAGES" "$PLATFORMS" "$LIBDEPS" "$CACHE"

# Copy the platform itself into the project so the build does not write to the global platform directory.
if [[ ! -d "$LOCAL_PLATFORM" ]]; then
  echo "[1/7] Copying verified PIOArduino 55.03.37 into the project..."
  /usr/bin/ditto "$GLOBAL_PLATFORM" "$LOCAL_PLATFORM"
else
  echo "[1/7] Local PIOArduino platform already present."
fi

# Expose already-installed global tool packages as read-only references inside the project.
# Only symlinks are created in this project; nothing under ~/.platformio is changed.
echo "[2/7] Linking existing tool packages into project-local package store..."
for src in "$GLOBAL_PACKAGES"/*; do
  [[ -e "$src" ]] || continue
  name="${src:t}"
  dst="$PACKAGES/$name"
  if [[ ! -e "$dst" && ! -L "$dst" ]]; then
    ln -s "$src" "$dst"
  fi
done

# CLI used only by this workspace.
REBUILD_CLI=0
if [[ ! -x "$CLI/bin/python" ]]; then
  REBUILD_CLI=1
else
  CLI_VER="$($CLI/bin/python -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")' 2>/dev/null || true)"
  [[ "$CLI_VER" == "3.11" ]] || REBUILD_CLI=1
fi
if [[ "$REBUILD_CLI" == "1" ]]; then
  echo "[3/7] Creating project-local PlatformIO CLI with Python 3.11..."
  rm -rf "$CLI"
  "$PY311" -m venv "$CLI"
  "$CLI/bin/python" -m pip install --upgrade pip setuptools wheel
  "$CLI/bin/python" -m pip install \
    "https://github.com/pioarduino/platformio-core/archive/refs/tags/v6.1.19.zip" \
    "uv>=0.1.0"
else
  echo "[3/7] Project-local PlatformIO CLI already ready."
fi

# PIOArduino 55.03.37 always uses <core_dir>/penv internally.
# Pre-create it with Python 3.11 and every dependency it checks, so build-time setup is a no-op.
PENV="$CORE/penv"
REBUILD_PENV=0
if [[ ! -x "$PENV/bin/python" ]]; then
  REBUILD_PENV=1
else
  PENV_VER="$($PENV/bin/python -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")' 2>/dev/null || true)"
  [[ "$PENV_VER" == "3.11" ]] || REBUILD_PENV=1
fi
if [[ "$REBUILD_PENV" == "1" ]]; then
  echo "[4/7] Creating PIOArduino internal penv with Python 3.11..."
  rm -rf "$PENV"
  "$PY311" -m venv "$PENV"
  "$PENV/bin/python" -m pip install --upgrade pip setuptools wheel uv
else
  echo "[4/7] PIOArduino internal penv already uses Python 3.11."
fi

UV="$PENV/bin/uv"
echo "[5/7] Installing/verifying PIOArduino Python dependencies locally..."
"$UV" pip install --python "$PENV/bin/python" --upgrade \
  "https://github.com/pioarduino/platformio-core/archive/refs/tags/v6.1.19.zip" \
  "littlefs-python>=0.16.0" \
  "fatfs-ng>=0.1.14" \
  "pyyaml>=6.0.2" \
  "rich-click>=1.8.6" \
  "zopfli>=0.2.2" \
  "intelhex>=2.3.0" \
  "rich>=14.0.0" \
  "urllib3<2" \
  "cryptography>=45.0.3" \
  "certifi>=2025.8.3" \
  "ecdsa>=0.19.1" \
  "bitstring>=4.3.1" \
  "reedsolo>=1.5.3,<1.8" \
  "esp-idf-size>=2.0.0" \
  "esp-coredump>=1.14.0"

# Install esptool into the local PIOArduino penv from the already-installed tool package.
if [[ -d "$PACKAGES/tool-esptoolpy" ]]; then
  echo "[6/7] Preparing local esptool runtime..."
  "$UV" pip install --python "$PENV/bin/python" --force-reinstall -e "$PACKAGES/tool-esptoolpy"
else
  echo "ERROR: tool-esptoolpy is not available in the existing global package cache."
  echo "No global package will be installed by this script."
  exit 4
fi

# Install project libraries into .pio-local/libdeps while proxy vars are available.
echo "[7/7] Resolving project libraries and validating metadata..."

# PIOArduino 55.03.37 has a legacy m5stick-c board manifest that points to
# variant "m5stick_c", while Arduino-ESP32 3.3.7 provides StickC Plus/SE
# pins under "m5stack_stickc_plus".  Verify the actual variant exists before
# metadata/build so a missing pins_arduino.h is caught here with a clear error.
VARIANT_HEADER="$PACKAGES/framework-arduinoespressif32/variants/m5stack_stickc_plus/pins_arduino.h"
if [[ ! -f "$VARIANT_HEADER" ]]; then
  echo "ERROR: expected StickC Plus variant header is missing:"
  echo "  $VARIANT_HEADER"
  echo "The existing Arduino-ESP32 3.3.7 package is incomplete or not the expected build."
  exit 5
fi
echo "      Variant OK: m5stack_stickc_plus"

cd "$ROOT"
"$CLI/bin/pio" pkg install -e m5stick-c-plus-se
"$CLI/bin/pio" project metadata >/dev/null

echo ""
echo "============================================================"
echo " HomeAIAgent Mini project-local toolchain is ready"
echo "============================================================"
echo "Python CLI : $($CLI/bin/python --version 2>&1)"
echo "PIO Core   : $($CLI/bin/pio --version | tail -n 1)"
echo "PIO penv   : $($PENV/bin/python --version 2>&1)"
echo "Project    : $ROOT"
echo ""
echo "No files under ~/.platformio, ~/.zshrc, Git config, or system Python were modified."
echo "Close VS Code completely, reopen this folder, then use PlatformIO Build/Upload."
