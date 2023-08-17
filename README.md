![license](https://img.shields.io/github/license/diamante0018/iw4x-client.svg)
[![build](https://github.com/diamante0018/iw4x-client/workflows/Build/badge.svg)](https://github.com/diamante0018/iw4x-client/actions)

# IW4x: Client

## Compile from source

- Clone the Git repo. Do NOT download it as ZIP, that won't work.
- Update the submodules and run `premake5 vs2022` or simply use the delivered `generate.bat`.
- Build via solution file in `build\iw4x.sln`.

## AlterWare

It is recommended to use this IW4x client with the AlterWare launcher.
Find out more at https://alterware.dev/.

## Premake arguments

| Argument                    | Description                                    |
|:----------------------------|:-----------------------------------------------|
| `--copy-to=PATH`            | Optional, copy the DLL to a custom folder after build, define the path here if wanted. |
| `--copy-pdb`                | Copy debug information for binaries as well to the path given via --copy-to. |
| `--force-unit-tests`        | Always compile unit tests.                     |
| `--disable-binary-check`    | Do not perform integrity checks on the exe. |

## Command line arguments

| Argument                | Description                                    |
|:------------------------|:-----------------------------------------------|
| `-tests`                | Perform unit tests.                            |
| `-entries`              | Print to the console a list of every asset as they are loaded from zonefiles. |
| `-stdout`               | Redirect all logging output to the terminal iw4x is started from, or if there is none, creates a new terminal window to write log information in. |
| `-console`              | Allow the game to display its own separate interactive console window. |
| `-dedicated`            | Starts the game as a headless dedicated server. |
| `-bigminidumps`         | Include all code sections from loaded modules in the dump. |
| `-reallybigminidumps`   | Include data sections from all loaded modules in the dump. |
| `-dump`                 | Write info of loaded assets to the raw folder as they are being loaded. |
| `-nointro`              | Skip game's cinematic intro.                   |
| `-version`              | Print IW4x build info on startup.              |
| `-nosteam`              | Disable friends feature and do not update Steam about the game's current status just like an invisible mode. |
| `-unprotect-dvars`      | Allow the server to modify saved/archive dvars. |


## Disclaimer

This software has been created purely for the purposes of
academic research. It is not intended to be used to attack
other systems. Project maintainers are not responsible or
liable for misuse of the software. Use responsibly.
