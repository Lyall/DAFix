# Dragon Age 1/2 Fix
[![Patreon-Button](https://raw.githubusercontent.com/Lyall/DAFix/refs/heads/master/.github/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/W7W01UAI9)<br />
[![Github All Releases](https://img.shields.io/github/downloads/Lyall/DAFix/total.svg)](https://github.com/Lyall/DAFix/releases)
This ASI plugin for Dragon Age: Origins and Dragon Age II fixes various ultrawide display issues, adjusts HUD scaling, and more.

## Features
### General
- [**DA:O**]: Adjustable HUD scale with automatic setting.

### Ultrawide
- [**DA:O**] / [**DA2**]: Disable pillarboxing during dialog/cutscenes.
- [**DA:O**]: Fix foliage culling and shadow issues at ultrawide aspect ratios.

### Graphics
- [**DA:O**]: Adjust various draw distances (Foliage, NPCs and objects).
- [**DA:O**]: Adjust shadow resolution.

## Installation
- Grab the latest release of DAFix from [here.](https://github.com/Lyall/DAFix/releases)
- Extract the contents of the release zip in to the the game folder. <br />
e.g. ("**steamapps\common\Dragon Age Ultimate Edition**" or "**steamapps\common\Dragon Age II**" for Steam).

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**
- Open up the game properties in Steam and add `WINEDLLOVERRIDES="dinput8=n,b" %command%` to the launch options.

## Configuration
- See **SotDFix.ini** to adjust settings.

## Screenshots
| ![ezgif-6-61a319619b](https://github.com/user-attachments/assets/2e2f76ff-ecd1-4920-8b02-1e8ecf8a1095) |
|:--------------------------:|
| [**DA:O**]: In-game |

| ![ezgif-6-2eb16dc058](https://github.com/user-attachments/assets/2ca570e5-e2d1-49a8-ae7c-682bfab8b04c) |
|:--------------------------:|
| [**DA:O**]: Dialog |

## Known Issues
Please report any issues you see.

- [**DA:O**] / [**DA2**]: Main menu background is vert- at ultrawide aspect ratios.

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
