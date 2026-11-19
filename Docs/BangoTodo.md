# Bango: future work

Ideas agreed on but not built yet. The spec (BangoSpec.md) describes only what exists.

## waitUntil

- **Timeout.** A way to stop waiting after a time, and tell the script whether the condition came true or the time ran out. One possible form: `bool _ok = waitUntil (cond, 10.0);`.
- **Interval setting.** A waitUntil whose condition calls a command is checked every frame. Let a script ask for less often, for example `waitUntil (IsAlive(@soldier) == false) every 0.5;`.

## Conditions and commands

- A `bPure` flag on commands, so a pure command (like `Max`) doesn't make a waitUntil or a watched condition polled.
- Commands that say when their answer may change (an "inventory changed" event), so a condition like `_activator HasItem("Gasmask")` can be checked on that event instead of polled.
