#!/bin/zsh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OLD="${1:-}"
if [[ -z "$OLD" ]]; then
  echo 'Usage: ./tools/migrate_from_backup.sh /path/to/previous/HomeAIAgent_Mini_backup'
  exit 2
fi
if [[ ! -d "$OLD" || ! -f "$OLD/platformio.ini" || ! -f "$OLD/src/main.cpp" ]]; then
  echo 'ERROR: backup does not look like a Mini project; no files transferred.'
  exit 2
fi
if [[ "$(cd "$OLD" && pwd -P)" == "$(cd "$ROOT" && pwd -P)" ]]; then
  echo 'ERROR: source backup equals new project.'
  exit 2
fi
if [[ ! -f "$ROOT/src/mini_a6_credentials.cpp" ]]; then
  echo 'ERROR: destination is not the new A6 NVS project.'
  exit 2
fi
# Never overwrite pre-existing private configuration.
if [[ -f "$ROOT/include/secrets.h" ]]; then
  echo 'PRESERVED: new project already has include/secrets.h.'
elif [[ -f "$OLD/include/secrets.h" && ! -L "$OLD/include/secrets.h" ]]; then
  /bin/cp -p "$OLD/include/secrets.h" "$ROOT/include/secrets.h"
  /bin/chmod 600 "$ROOT/include/secrets.h"
  echo 'IMPORTED locally: include/secrets.h (not contained in the ZIP).'
else
  echo 'ACTION REQUIRED: No previous include/secrets.h; configure it locally from include/secrets.example.h.'
fi
# Copy the actual installed toolchain and libraries, rather than downloading again.
for name in .pio-local .pio-venv; do
  if [[ -e "$ROOT/$name" || -L "$ROOT/$name" ]]; then
    echo "PRESERVED: $name is already present in the new project."
  elif [[ -d "$OLD/$name" && ! -L "$OLD/$name" ]]; then
    echo "COPYING: $name (may be large)..."
    /usr/bin/ditto "$OLD/$name" "$ROOT/$name"
    echo "IMPORTED: $name"
  else
    echo "MISSING: $name; old project has no usable local cache. See README.md section 3."
  fi
done
if [[ -x "$ROOT/.pio-venv/bin/pio" && -d "$ROOT/.pio-local" && -f "$ROOT/include/secrets.h" ]]; then
  echo 'LOCAL MIGRATION PASS: sources, private configuration and local PIO files are present.'
else
  echo 'LOCAL MIGRATION PARTIAL: see messages above before attempting to build.'
fi
