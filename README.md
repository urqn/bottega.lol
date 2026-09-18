# bottega.lol

External cheat overlay for Roblox. Reads game state with `ReadProcessMemory` / `WriteProcessMemory` and draws a Direct3D 11 ImGui overlay over the game window — no code is injected into the game process.


> **Join the Discord for updated offsets and updates**: https://discord.gg/shvMwDHFF9


---


## Features


**Aimbot** — FOV circle, smoothness, hit-part selection, mouse / memory methods, projectile prediction, auto fire (delay + trigger FOV), repositions in real time, silent aim with viewport / raycast methods, magic bullet, tracer.


**ESP** — boxes (static / dynamic, bounding / corner, filled and gradient modes), names, distance, health bars, skeleton (thickness + outline), head dot, view direction, tool in hand, flags, friendly check, self ESP, max distance.


**Movement** — walk speed, jump power, hip height, gravity, FOV changer, bunny hop, noclip (all parts / root only), fly (speed, vertical boost, damping).


**Freecam** — speed, sensitivity, character freeze, always / hold / toggle activation.


**Players** — searchable player list, spectate, teleport to player.


**Misc** — config system (create / save / load / reset), team check, dead check, watermark, streamproof, vertical sync, fully customizable theme.


---


## Building


1. Clone the repository.
2. Open `bottega.lol.sln` in Visual Studio 2022.
3. Set configuration to **Release / x64**.
4. Build the solution.


Output:


```
build\bottega.lol.exe
```


---


## Usage


> Run the menu **after** the Roblox game window is open (in-game, not just in the lobby).


1. Start Roblox and join a game.
2. Run `build\bottega.lol.exe`.
3. Press **INSERT** to toggle the menu.


Most features have a bindable key — assign one in the menu, or switch it between hold / toggle / always at the bottom of the key popup. Right-click a keybind to clear it back to `None`. The offsets table used is shown live on the **Info** tab.


Configurations are stored as `.cfg` files in `C:\bottega\configs\` and can be created, saved, loaded, and reset from the **Misc ▸ Configs** tab.


---


## Offsets


Offsets are **fetched at runtime** from `bottega-lol-offsets.vercel.app` matched against the detected Roblox client build, and cached locally next to the executable (`offsets.json` / `offsets.version`). The Info tab displays the loaded Roblox version and offset count.


Offsets go stale every time Roblox ships a client build. Updated offsets are released in the **Discord**:


**[https://discord.gg/shvMwDHFF9](https://discord.gg/shvMwDHFF9)**


---


## Disclaimer


This project is provided **for educational and research purposes only**. It is not affiliated with, endorsed by, or connected to Roblox Corporation. Using third-party software with Roblox violates the Roblox Terms of Service and carries a **risk of account termination**; use entirely at your own risk. You are responsible for how you use this code.