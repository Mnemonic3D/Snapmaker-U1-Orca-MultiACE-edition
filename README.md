<p align="center">
  <img src="Logo.png" alt="OrcaSlicer MultiACE Mnemonic 3D Edition" width="100%">
</p>

<h1 align="center">UPDATE RELEASED — 2026-09-05, 3:06 AM ET</h1>

# Orca Slicer - MultiACE Edition by Mnemonic3D

> **Compatibility Notice**
>
> This project is a **custom, reverse-engineered OrcaSlicer build** created to work with the existing Snapmaker U1, PAXX, and MultiACE firmware ecosystem. It does **not** replace, modify, maintain, or distribute those firmware projects.
>
> **All firmware updates, fixes, releases, and firmware-related support must be obtained through their respective official GitHub repositories.** This project only provides the OrcaSlicer-side integration and compatibility layer.

<div align="center">
  <a href="https://youtu.be/dXFPloWLsdc">
    <img src="https://img.shields.io/badge/Watch_on_YouTube-FF0000?style=for-the-badge&amp;logo=youtube&amp;logoColor=white" alt="Watch on YouTube">
  </a>
  <br><br>
  <a href="https://youtu.be/dXFPloWLsdc">
    <img src="https://img.youtube.com/vi/dXFPloWLsdc/hqdefault.jpg" alt="Orca Slicer MultiACE Edition by Mnemonic3D video" width="720">
  </a>
  <br>
  <strong>Orca Slicer - MultiACE Edition by Mnemonic3D</strong>
</div>

<a href="https://buymeacoffee.com/Mnemonic3d">
  <img src="https://img.buymeacoffee.com/button-api/?text=Buy%20me%20a%20coffee&emoji=%E2%98%95&slug=Mnemonic3d&button_colour=FFDD00&font_colour=000000&font_family=Lato&outline_colour=000000&coffee_colour=ffffff"
       alt="Buy Me a Coffee"
       width="163">
</a>

## Installation & Troubleshooting

- **[Installation Guide](docs/Installation-Guide.md)** — installing Orca Slicer - MultiACE Edition and the Mandatory Patch, start to finish
- **[Troubleshooting Commons](docs/Troubleshooting-Commons.md)** — something on your printer looking off after a fresh flash? Most of it is expected, documented behavior

---

## Overview

This custom **Orca Slicer MultiACE Edition by Mnemonic3D** adds dedicated Snapmaker U1 MultiACE support directly into OrcaSlicer. It introduces live MultiACE filament synchronization, support for more logical filament choices than the U1's four physical toolheads, **4 Head / All Colors** display modes, custom MultiACE printer and process profiles, MultiACE-aware start and filament-change G-code, and integration with the existing PAXX/MultiACE preflight and tool-mapping workflow.

Additional work includes a Snapmaker-specific printer agent, live ACE slot and toolhead-state handling, filament-state persistence, preservation of PAXX toolhead calibration and swap behavior, custom Mnemonic3D printer artwork, splash screen and Windows installer branding, and removal of the old local postprocessor dependency so the build can be distributed without hardcoded printer addresses or private local scripts.


## Features

See the **[COMPLETE FEATURES PAGE](FEATURES.md)** for the MultiACE integration,
live filament synchronization, logical-to-physical tool mapping, safety fixes,
profiles, interface improvements, and Mnemonic3D branding included in this
edition.

---

## Lineage / credits

Thanks and credit to the developers and contributors whose work made this project possible:

- [OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer)
- [PAXX12 Snapmaker U1 Extended Firmware](https://github.com/paxx12-snapmaker-u1/SnapmakerU1-Extended-Firmware)
- [multiACE](https://github.com/decay71/multiACE)
- [SnapACE](https://github.com/BlackFrogKok/SnapAce)

This build is based on and extends their work, bringing these projects together into a more integrated Snapmaker U1 MultiACE workflow.

This repository itself is a standalone (non-fork) publish of [Mnemonic3D/Snapmaker-U1-MultiACE-edition](https://github.com/Mnemonic3D/Snapmaker-U1-MultiACE-edition), pushed here directly rather than as a GitHub fork because of a persistent GitHub fork-network object storage issue that blocked pushes to the fork itself. Full commit history and authorship live at the fork above.

## Acknowledgments

## Development Environment

<p align="left">
  <a href="https://www.docker.com/"><img src="https://img.shields.io/badge/Container-Docker-2496ED?logo=docker&logoColor=white" alt="Docker"></a>
  <a href="https://code.visualstudio.com/"><img src="https://img.shields.io/badge/Editor-VS%20Code-007ACC?logo=visualstudiocode&logoColor=white" alt="VS Code"></a>
  <a href="https://ubuntu.com/"><img src="https://img.shields.io/badge/Base%20Image-Ubuntu%2024.04-E95420?logo=ubuntu&logoColor=white" alt="Ubuntu 24.04"></a>
  <a href="https://github.com/devcontainers"><img src="https://img.shields.io/badge/Dev%20Environment-Dev%20Containers-2496ED?logo=docker&logoColor=white" alt="Dev Containers"></a>
</p>

Development on this project also used a **Docker**-based dev container (Ubuntu 24.04, C++ toolchain) via the [devcontainers](https://github.com/devcontainers) "desktop-lite" feature for a reproducible GUI development environment, edited in **Visual Studio Code** with the CMake Tools and C++ Extension Pack extensions, with remote GUI access provided over noVNC.

## Build Tools

<p align="left">
  <a href="https://visualstudio.microsoft.com/"><img src="https://img.shields.io/badge/Compiler-Visual%20Studio%20%2F%20MSBuild-5C2D91?logo=visualstudio&logoColor=white" alt="Visual Studio / MSBuild"></a>
  <a href="https://cmake.org/"><img src="https://img.shields.io/badge/Build%20System-CMake-064F8C?logo=cmake&logoColor=white" alt="CMake"></a>
  <a href="https://nsis.sourceforge.io/"><img src="https://img.shields.io/badge/Installer-NSIS-FFA500" alt="NSIS"></a>
  <a href="https://strawberryperl.com/"><img src="https://img.shields.io/badge/Dependency%20Build-Strawberry%20Perl-blue" alt="Strawberry Perl"></a>
</p>

This build is compiled with **Visual Studio / MSBuild** and **CMake**, the same toolchain used by upstream OrcaSlicer, with **Strawberry Perl** required for building dependencies. The Windows installer for this edition is packaged with **NSIS (Nullsoft Scriptable Install System)**.


<p align="left">
  <a href="https://claude.ai"><img src="https://img.shields.io/badge/Made%20with-Claude-D97757?logo=claude&logoColor=white" alt="Made with Claude"></a>
  <a href="https://openai.com/chatgpt"><img src="https://img.shields.io/badge/Made%20with-ChatGPT-74AA9C?logo=openai&logoColor=white" alt="Made with ChatGPT"></a>
  <a href="https://www.klipper3d.org"><img src="https://img.shields.io/badge/Firmware-Klipper-FF7F32" alt="Klipper"></a>
  <a href="https://reprap.org/wiki/G-code"><img src="https://img.shields.io/badge/G--code-Community-4C8BF5" alt="G-code Community"></a>
</p>

Development of this build was assisted by AI coding tools — **Claude** (Anthropic) and **ChatGPT** (OpenAI) — used throughout for debugging the MultiACE synchronization logic, refactoring the OrcaSlicer integration code, and drafting project documentation. The G-code handling in this build also owes a debt to the broader open-source firmware community, in particular the **Klipper** project, whose macro system and documentation informed how MultiACE-aware start and filament-change G-code is structured here, and to the wider community of G-code contributors and toolchain authors whose conventions this project builds on.

## License

This project is licensed under the **[GNU Affero General Public License v3.0 (AGPL-3.0)](LICENSE)**.

In plain terms: anyone is free to use, fork, modify, and redistribute this code, including for commercial purposes, as long as they keep it open source under the same license. The one condition that goes beyond a standard GPL license is the "network use" clause — if someone runs a modified version of this software as a service over a network (for example, a hosted build server), they're required to make that modified source code available to the people using the service, not just to people who receive a copy of the binary.

This summary is provided for convenience only and isn't legal advice — the [full license text](LICENSE) is what governs actual use of this project.
