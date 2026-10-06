#!/usr/bin/env sh
# Build web UI via Docker; output goes to front/web/dist
set -e
cd "$(dirname "$0")/front/web"

if ! docker info >/dev/null 2>&1; then
    echo "Starting Docker Desktop..."
    case "$(uname -s)" in
        Darwin) open -a Docker ;;
        MINGW*|MSYS*|CYGWIN*) "/c/Program Files/Docker/Docker/Docker Desktop.exe" >/dev/null 2>&1 & ;;
        *) echo "Start the docker daemon manually." >&2; exit 1 ;;
    esac
    until docker info >/dev/null 2>&1; do sleep 2; done
fi

docker build -t netlab-web .
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W 2>/dev/null || pwd)/dist:/app/dist" netlab-web
