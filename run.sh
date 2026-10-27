#!/usr/bin/env bash
set -euo pipefail

APP_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$APP_DIR"

if [[ ! -f .env.local ]]; then
    printf 'Please first copy `.env.example` to `.env.local` and fill in your local configuration.\n' >&2
    exit 1
fi

set -a
source .env.local
set +a

exec ./build/server
