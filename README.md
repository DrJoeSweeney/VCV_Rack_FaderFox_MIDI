# Virtual FaderFox PCn

A free, open-source [VCV Rack](https://vcvrack.com/) plugin that turns any MIDI controller into a matrix of control voltages — styled after the legendary [FaderFox PC12](https://faderfox.de/) hardware controller.

## What it does

**Virtual FaderFox PCn** listens for MIDI Continuous Controller (CC) messages and converts them into CV on a grid of output ports — one port per knob, fader, or encoder on your controller:

- **Configurable grid** — from 1×1 up to 8 rows × 16 columns (128 CV outputs). Defaults to the PC12's 6×12 layout, and the panel title even follows FaderFox's naming scheme (`PC12`, `PC16`, …).
- **One-click MIDI learn** — click a port, wiggle a control, done. No mapping dialogs.
- **Six voltage ranges** per port or per row: 0–10 V, 0–5 V, 0–1 V, ±10 V, ±5 V, ±1 V.
- **Works with any MIDI device** — FaderFox boxes, keyboard controller encoder banks, DJ mixers, iPad apps, virtual MIDI from software. Anything that sends CC.
- **PC12 bonus** — press the red SHIFT + green 12 buttons on the hardware to snapshot all encoder positions straight into the module.
- **Ready-made presets** — including the PC12 factory setups for Elektron Rytm, Octatrack + A4, and Ableton Live, plus quick-assign layouts for your own mappings.
- Red LED CC/value display, activity indicator, per-port value arcs, and output smoothing so 7-bit MIDI doesn't zipper-click your audio.

## Documentation

📖 **[User Manual](docs/MANUAL.md)** — quick start, panel tour, MIDI learn, menu reference, presets, and tips.

## About this project

This is a fan-made tribute, built by an admirer of FaderFox hardware. It is **not** made by, endorsed by, or affiliated with FaderFox — if you like the workflow, check out the real thing at [faderfox.de](https://faderfox.de/): built like tanks, smooth to play, no better performance machines.

The plugin runs in [VCV Rack](https://vcvrack.com/), the free and open-source virtual modular synthesizer.

Development of this project was assisted by Kimi 3 AI code generation.

## Building

Requires the [VCV Rack SDK](https://vcvrack.com/manual/PluginDevelopmentTutorial) and a MinGW toolchain (MSYS2 on Windows). See `build.sh`, which mirrors the sources to a local directory and builds:

```bash
./build.sh        # build + install into your Rack 2 user plugins folder
./build.sh clean  # remove local build artifacts
```

Or with the Rack SDK directly:

```bash
make dist RACK_DIR=/path/to/Rack-SDK
```

## License

[MIT](LICENSE)
