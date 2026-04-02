# OBS Squeezeback Transition

A broadcast-quality DVE squeezeback transition plugin for OBS Studio. The current scene smoothly scales down and slides into a corner while the next scene pushes in from behind with physical counter-motion.

## Features

- **Push physics**: Source B doesn't just appear statically. It pushes in with subtle counter-motion (scale + offset), making the squeeze feel physical and intentional.
- **Squeeze In & Out**: Squeeze the current scene into a corner, or reverse it to expand from a corner back to fullscreen.
- **7 position presets**: Top Right, Top Left, Bottom Right, Bottom Left, Top Center, Bottom Center, Center.
- **13 easing curves**: Linear, Quad, Cubic, Expo, and Back (with overshoot) in In/Out/InOut variants.
- **Configurable border**: Width, color, and rounded corners with anti-aliased edges.
- **Drop shadow**: Offset, blur, and color for depth behind the squeezed rectangle.
- **GPU-accelerated**: All rendering done in HLSL shaders for smooth 60fps transitions.
- **Cross-platform**: Windows, macOS, and Linux via OBS's D3D11/OpenGL abstraction.

## Installation

1. Download the latest release for your platform from [Releases](../../releases)
2. Extract to your OBS plugins directory:
   - **Windows**: `C:\Program Files\obs-studio\`
   - **Linux**: `/usr/share/obs/` or `~/.config/obs-studio/plugins/`
   - **macOS**: `~/Library/Application Support/obs-studio/plugins/`
3. Restart OBS Studio
4. In the Scene Transitions dropdown, select "Squeezeback"

## Building from Source

### Prerequisites
- CMake 3.16+
- OBS Studio SDK / development headers
- C compiler (MSVC, GCC, or Clang)

### Build
```bash
cmake -S . -B build -DBUILD_OUT_OF_TREE=ON
cmake --build build --config Release
```

## Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| Squeeze To | Top Right | Corner/position to squeeze into |
| Final Size | 30% | How small the squeezed rectangle gets |
| Edge Padding | 20px | Distance from screen edge |
| Easing | Ease In-Out (Cubic) | Animation curve |
| Push Intensity | 0.3 | How much the incoming scene pushes (0=static, 1=aggressive) |
| Reverse | Off | Squeeze Out mode (expand from corner) |
| Border | On, 3px white | Rectangle border |
| Corner Radius | 8px | Rounded corners |
| Shadow | Off | Drop shadow behind rectangle |

## License

GPL-2.0 (same as OBS Studio)
