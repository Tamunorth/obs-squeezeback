# OBS Squeezeback Roadmap

## Planned

### Cross-Platform Build Support

- **Linux:** CMake + GCC/Clang build, generate .deb/.tar.gz packages, install to ~/.config/obs-studio/plugins/ or /usr/share/obs/obs-plugins/
- **macOS:** CMake + Clang build, generate .pkg installer, install to ~/Library/Application Support/obs-studio/plugins/, universal binary (arm64 + x86_64) for Apple Silicon support
- **CI/CD:** GitHub Actions workflow to build on all three platforms on push/tag, auto-attach artifacts to GitHub Releases
- **CMakeLists.txt updates:** The existing CMake already has some cross-platform scaffolding (BUILD_OUT_OF_TREE, platform detection). Needs testing and fixes for non-Windows paths, shader compilation (HLSL on Windows vs GLSL on Linux/macOS via OBS's abstraction layer), and correct lib linking per platform.

### Squeezeback Control Dock

A portable OBS dock panel (similar to the built-in Scene Transitions dock) that gives quick access to Squeezeback controls without opening the filter properties.

- **Video source dropdown:** Select which video source the zoom filter targets. Changing this updates the filter on the fly without opening the filter settings dialog.
- **Background source dropdown:** Select the background/graphics source. Allows switching the L-shape background on the fly during a live show (swap lower thirds, switch graphic packages between sessions).
- **Recapture button:** Re-detect the source position from the dock.
- **Toggle button:** Trigger the squeeze animation from the dock.
- **Implementation:** Register as an OBS dock via `obs_frontend_add_dock` (requires linking against obs-frontend-api). Qt-based widget with two combo boxes and two buttons. The dock reads and writes to the active filter's settings in real time.
