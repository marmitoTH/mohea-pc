# Medal of Honor: European Assault - XBE Recomp

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
- **Cheats** - Squad takes no hits, player takes no damage, unlimited ammo, no shell shock.

### Controls
- **Controllers** - Xbox, PlayStation and Switch controllers, with hot plugging and rumble.
- **Keyboard and mouse** - Play with WASD and the mouse, with adjustable sensitivity and inverted Y. Switching between controller and keyboard is seamless.
- **Remapping** - Every action can be rebound in the overlay, for the controller and the keyboard, with Reload and Use on buttons of their own if you like. The controller has the game's four layouts (Sharp Shooter, Commander, Recon, Reloader), a **Modern** one (A jump, B crouch, X use, Y switch weapon, bumpers for grenade and adrenaline) and your own **Custom** one. The menus always keep the standard controls.

### Miscellaneous
- **Saves** - Profiles and saved games are kept beside the exe, as on the console's hard drive.
- **Boot Splash** - Place a `splash.png` (or `.bmp`) beside the exe to display a splash screen at launch.

## Known Issues

- A rare crash has been seen with many soldiers about at once (the game runs out of sound buffers). See [CRASHES.md](CRASHES.md).

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
