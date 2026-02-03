# InGame Overlay

This is a fork of the [Nemirtingas/ingame_overlay](https://github.com/Nemirtingas/ingame_overlay) project.

## Project Intention

The primary goal of this fork is to modernize the hooking infrastructure and improve multi-renderer stability for advanced overlays on Windows and Linux (Proton/Wine).

### Key Improvements

- **MinHook Migration**: Replaced the legacy hooking engine with [MinHook](https://github.com/TsudaKageyu/minhook), providing superior 64-bit instruction relocation and stable RIP-relative jump handling.
- **Vulkan & DirectX 12 Stabilization**:
  - Resolved critical initialization crashes in Vulkan via lazy context capture.
  - Fixed swapchain recreation loops and uninitialized function pointer issues.
  - Improved DirectX 12 reliability by capturing the Command Queue directly from API calls.
- **Submodule Unification**: Consolidated all external dependencies (`ImGui`, `MinHook`, `GLFW`, and `System`) as git submodules to ensure build reproducibility and cleaner dependency management.
- **Windows Stability**: Fixed several system-level issues, including a cursor snap/lock bug on application exit.
- **Improved Tooling**: Added a unified `build.ps1` script for automated environment setup and project compilation.

## Build

To build the project on Windows, simply run:

```powershell
.\build.ps1
```

This will synchronize submodules, configure CMake, and perform a full Release build into the `OUT` directory.

## License

This project is licensed under the GNU Lesser General Public License v3.0 - see the [LICENSE](LICENSE) file for details.
