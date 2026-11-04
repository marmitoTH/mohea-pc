# Medal of Honor: European Assault - XBE Recomp

<image src="./screenshots/bulge_german_tank.png"/>

An unofficial native PC port of the original Xbox version of Medal of Honor: European Assault (USA, title ID 4541005E).

This port was created using the [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp) tool.

> [!NOTE]
> This project does not include any game assets. You must provide the files from your own legally acquired copy of the game to install or build.

## Installation

- Unzip the release anywhere you can write to.
- Put your game in the `assets` folder: the disc image (`.iso`), or the whole extracted disc (`default.xbe` with `DATA` and `EAGLRM.O` beside it). Only the USA release is supported; another release is offered to start anyway, but will most likely crash.
- Run `MOHEA.exe`. The first start compiles the game's code for your PC behind a progress window. This happens only once and can take a while; if you close it, the next start carries on where it left off.
- Press **F1** (or both sticks, **L3 + R3**, on a controller) for the settings overlay. Every setting is also kept in `mohea.toml` beside the exe.

## Features

<image src="./screenshots/africa_allies.png"/>

| Original | Enhanced |
| --- | --- |
| <image src="./screenshots/original-1.png"/> | <image src="./screenshots/enhanced-1.png"/> |
| <image src="./screenshots/original-2.png"/> | <image src="./screenshots/enhanced-2.png"/> |

The whole campaign is playable from start to finish, with every movie and cutscene.

### Display
- **Fullscreen, borderless or windowed** - **F11** or **Alt+Enter** switches between a window and the full screen. The window can be resized freely.
- **60 FPS** - In game at 60 frames a second instead of the original 30 (on by default, **F9** toggles it). Weapons, timers and scripted events keep their original pace.
- **Field of view** - From 60 to 110 degrees (70 is the original).
- **Hide HUD** and **hide squad markers** - For screenshots or a cleaner screen.

### Graphics
- **Presets** - *Original* looks as the Xbox drew it; *Enhanced* (the default) turns the improvements below on; anything else is *Custom*, and is remembered.
- **Resolution scale** - From 50% to 200% of the window's resolution. Above 100% it supersamples for a sharper, smoother picture.
- **SMAA antialiasing** - Low to Ultra (Ultra by default).
- **Anisotropic filtering** - Up to 16x, for sharp textures at an angle.
- **Detail distance** - Characters and objects keep their full detail up to four times farther away.
- **Color** - Gamma, contrast and saturation.
- **Sharpening** and **ambient occlusion (SSAO)** - With adjustable strength (**F8** and **F7** toggle them).

### Gameplay
All off by default, they leave the original game as it was.
- **Drops** - Weapons and items dropped by fallen enemies can stay longer, or never disappear.
- **Move while aiming** - Walk and strafe at half speed while aiming down the sights, instead of leaning in place.
- **Free camera** - Fly the view anywhere in the level (WASD / left stick to move, mouse / right stick to look, Q / E or the triggers for down and up, the wheel or the d-pad for speed) while the player stands still; the level's detail streams and fades in around the camera. Turned off, the view snaps back to the player, or the player can be dropped where the camera is.
- **Freeze** - Stop the game's time, sound and music included, while it is still drawn (**F6**, or the overlay's hacks): fly the free camera about the moment, or change a setting and see it on the same frame.
- **Cheats** - Squad takes no hits, player takes no damage, unlimited ammo, no shell shock.

### Controls
- **Controllers** - Xbox, PlayStation and Switch controllers, with hot plugging and rumble.
- **Keyboard and mouse** - Play with WASD and the mouse, with adjustable sensitivity and inverted Y. Switching between controller and keyboard is seamless.
- **Remapping** - Every action can be rebound in the overlay, for the controller and the keyboard, with Reload and Use on buttons of their own if you like. The controller has the game's four layouts (Sharp Shooter, Commander, Recon, Reloader), a **Modern** one (A jump, B crouch, X use, Y switch weapon, bumpers for grenade and adrenaline) and your own **Custom** one. On the keyboard, crouch (C) and prone (Ctrl) each toggle with standing, 1 / 2 / 3 pick the primary, the secondary and the grenades, and WASD also moves through the menus, as in modern shooters. The menus always keep the standard controls. The in-game button prompts and tutorials show the buttons as remapped, or the key names on the keyboard ("Press [E] to pick up"), and so do the menus' button bar and dialogs ("[E] Select  [Q] Back", "[E] Yes").

### Miscellaneous
- **Saves** - Profiles and saved games are kept beside the exe, as on the console's hard drive.
- **Screenshots** - **F12** saves the picture as shown, without the overlay, as a PNG in the `screenshots` folder beside the exe.
- **Boot Splash** - Place a `splash.png` (or `.bmp`) beside the exe to display a splash screen at launch.

## Known Issues

<image src="./screenshots/bulge_fallen_allies.png"/>

- ~~A rare crash has been seen with many soldiers about at once (the game runs out of sound buffers).~~ (fixed) See [CRASHES.md](CRASHES.md).

## System Requirements

### Minimum
- **OS**
  - Windows 10 64-bit
- **GPU** with support for Direct3D 12

## Building

### 1. Clone the Repository
Clone this repository using [Git](https://git-scm.com/).

### 2. Add the Required Game Files
Extract the whole disc into the `assets` folder: `python tools\xiso.py extract <image.iso> assets`. `default.xbe` must be there, with `DATA` and `EAGLRM.O` beside it. The build checks that `default.xbe` is the USA release (SHA-1 `f194e90b50f9f2a5259ed941971b0b4c11583eee`).

Extract all of it, not only `default.xbe`: with `default.xbe` in `assets` the game reads its files from that folder, and a disc image beside it is not used.

### 3. Install Dependencies
Install Visual Studio 2022 with **Desktop development with C++**, **C++ Clang tools for Windows** and **C++ CMake tools for Windows** (`build.bat` offers to add the last two).

### 4. Build the Project
Run `build.bat`. The first build recompiles the game and takes a while. The exe is written to `out\build\release`.

### 5. Package a Release
Run `release.bat` (needs [7-Zip](https://www.7-zip.org)). It writes `MOHEA.zip` with the executable, the compiler and the licenses, and no game code: each player's first start compiles it from their own copy.

## Credits

- [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp), the recompiler and runtime this is built on.
- [Finest Hour Remastered](https://github.com/crazyriddler/FinestHourRemastered), the Call of Duty: Finest Hour recomp this port was modelled on, for its project layout, build and release scripts, and the lessons on running a single-core Xbox game on a PC.
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) for the overridable function layout and instruction hooks.
- [Zydis](https://github.com/zyantific/zydis) for decoding x86 instructions, [toml++](https://github.com/marzer/tomlplusplus) for the configs, [Dear ImGui](https://github.com/ocornut/imgui) for the settings overlay and [SDL](https://github.com/libsdl-org/SDL) for the window, input and controllers.
- [SMAA](https://www.iryoku.com/smaa/) by Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro and Diego Gutierrez, for the antialiasing (MIT, see `SMAA-LICENSE.txt`).
