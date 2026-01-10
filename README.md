# TSR Speed Overlay

A simple external speed overlay for **TimeSplitters Rewind**.

This tool reads the player’s in-game **X / Y / Z coordinates** and calculates movement speed in real time.  
It’s mainly intended for **movement practice / speedrunning**, not as a polished end-user app.

---

## Features

- Live speed display (based on player **X, Y, Z world coordinates**)
- Lightweight Win32 overlay
- Use **windowed fullscreen**
- No game injection, no memory writing (read-only)
- **F10** hotkey to close the overlay

---

## How it works (roughly)

- Finds the game process by name
- Resolves player position (X/Y/Z) through memory pointers
- Calculates speed using distance moved over time
- Displays the result in a small always-on-top window

This is **not** a hook or DLL injection — it uses `ReadProcessMemory`.

---

## Limitations / Important Notes

- Pointer paths are **not guaranteed to work on every PC**
- Different machines may need different pointer paths
- Updates to the game can break memory addresses
- Currently Windows-only (Linux via Proton is experimental)

If it fails to attach or shows zero values, that usually means the pointer chain needs updating.
