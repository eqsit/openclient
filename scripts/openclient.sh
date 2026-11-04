#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
if [[ -z "${SDL_VIDEODRIVER:-}" && -z "${SDL_VIDEO_DRIVER:-}" && -n "${WAYLAND_DISPLAY:-}" && -n "${XDG_RUNTIME_DIR:-}" ]]; then
    task_wayland_socket="$WAYLAND_DISPLAY"
    if [[ "$task_wayland_socket" != /* ]]; then task_wayland_socket="$XDG_RUNTIME_DIR/$task_wayland_socket"; fi
    if [[ -S "$task_wayland_socket" ]]; then export SDL_VIDEODRIVER=wayland; fi
fi
exec "$root/dist/OpenClient" "$@"
