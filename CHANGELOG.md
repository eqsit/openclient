# Changes

## 0.1.1 Preview

- Fix Fly mouse steering, held keys and releases through menus.
- Reset stale Pilot plans on reconnect/profile changes and remove the custom time budget.
- Add Serial killer behavior, target selection and main/dummy support.
- Add Warlist editing, persistence and commands.
- Add local Trace/Autopsy reports and clear/dump commands. Trace is enabled by its original default and can be disabled with `null_godmode_trace 0`.
- Simulate projectile trajectories with collisions, bounces and lifetimes.
- Restore original KRX labels and all 22 hotkey rows/commands.

Linux builds and native/input/UI checks passed. Windows is cross-built with dependency checks; runtime execution was not tested. Serial and report behavior remain a reconstruction with known differences from the original client.
