# OBS Squeezeback Roadmap

## Planned

### Cross-Platform Build Support

- **Linux:** CMake + GCC/Clang build, generate .deb/.tar.gz packages, install to ~/.config/obs-studio/plugins/ or /usr/share/obs/obs-plugins/
- **macOS:** CMake + Clang build, generate .pkg installer, install to ~/Library/Application Support/obs-studio/plugins/, universal binary (arm64 + x86_64) for Apple Silicon support
- **CI/CD:** GitHub Actions workflow to build on all three platforms on push/tag, auto-attach artifacts to GitHub Releases
- **CMakeLists.txt updates:** The existing CMake already has some cross-platform scaffolding (BUILD_OUT_OF_TREE, platform detection). Needs testing and fixes for non-Windows paths, shader compilation (HLSL on Windows vs GLSL on Linux/macOS via OBS's abstraction layer), and correct lib linking per platform.
