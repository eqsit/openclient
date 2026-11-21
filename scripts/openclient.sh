#!/usr/bin/env bash
export LD_LIBRARY_PATH="/home/arch/.local/share/ddnet-runtime/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
if [[ -z "${SDL_VIDEODRIVER:-}" && -z "${SDL_VIDEO_DRIVER:-}" && -n "${WAYLAND_DISPLAY:-}" && -n "${XDG_RUNTIME_DIR:-}" ]]; then
    task_wayland_socket="$WAYLAND_DISPLAY"
    if [[ "$task_wayland_socket" != /* ]]; then task_wayland_socket="$XDG_RUNTIME_DIR/$task_wayland_socket"; fi
    if [[ -S "$task_wayland_socket" ]]; then export SDL_VIDEODRIVER=wayland; fi
fi
export SDL_APP_ID="${SDL_APP_ID:-openclient-null}"
exec "$root/dist/NullClient" "$@"
