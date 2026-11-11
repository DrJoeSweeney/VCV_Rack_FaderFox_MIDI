# Virtual FaderFox PCn — User Manual

**Virtual FaderFox PCn** is a configurable MIDI CC-to-CV matrix for VCV Rack, styled after the FaderFox PC12 hardware controller. It listens to MIDI Continuous Controller (CC) messages from any connected MIDI device and converts them into control voltages on a grid of output ports — one port per encoder on your controller.

It is designed to make connecting a FaderFox PC12 (or any similar MIDI controller) to VCV Rack fast and easy: no per-port MIDI mapping dialogs, just click a port and turn a knob.

---

## Quick start

1. Add the module to your rack. It appears as a 6 × 12 grid (the PC12 layout) and automatically selects the first available MIDI driver and device.
2. Right-click the panel and check **MIDI driver** and **MIDI device** — make sure your controller is selected.
3. **Left-click any blank (dark) port.** It starts pulsing yellow and the red display shows `Lrn` — the port is now in *learn* mode.
4. **Turn an encoder or move a fader on your MIDI controller.** The port flashes green and is now mapped to that CC.
5. Patch a cable from the port to any CV input. Turning the hardware control now outputs a voltage (0–10 V by default).
6. Repeat for as many ports as you need — or load one of the included presets to map the whole grid at once.

Everything — grid size, MIDI channel, CC assignments, voltage ranges — is saved with your patch.

---

## Panel tour

### The grid

Each cell is one CV output port, drawn as a FaderFox-style encoder knob with a jack socket in the center. Ports are numbered by row (left edge) and column (bottom edge), matching the layout of the hardware.

| Port appearance | Meaning |
|---|---|
| Dark, dull center dot | Blank (unmapped). Click to learn. Outputs 0 V. |
| Light knob, bright center, CC number below | Mapped. Shows the current value as a pointer and arc. |
| Pulsing yellow | In learn mode, waiting for a CC message. |
| Green flash | Just mapped (confirmation, ~1 second). |

The value arc fills clockwise from the 7 o'clock position (like a real pot). For bipolar ranges (±5 V etc.) the arc fills from the center-zero position instead, so you can see polarity at a glance. The arc and CC number brighten briefly whenever the value changes.

Blank ports are inert: you cannot drag a cable out of them, and they output 0 V.

### The red LED display (top right)

- **CC** — the CC number of the most recent MIDI message.
- **VALUE** — its value, 000–127.
- Shows `Lrn` (pulsing) while a port is in learn mode.
- The red dot to its left flashes on any MIDI activity; a yellow `LEARN` label appears while learning.

### Title

The panel title reads `FaderFox PCn`, where *n* is the current number of columns — matching FaderFox's naming scheme (PC12 = 12 columns).

---

## MIDI learn

| Action | Result |
|---|---|
| Left-click a blank port | Toggle learn mode on/off for that port (yellow pulse) |
| Send a CC while learning | Map that CC to the port (green flash) |
| Left-click a **mapped** port | Nothing (protects your mappings) |
| Double-click a mapped port | Unlearn — the port goes blank and outputs 0 V |
| Right-click anywhere | Module menu (see below) — never changes mappings |

Notes:

- **Learning moves, not copies.** If the CC you send is already mapped to another port, that port is cleared and the mapping moves to the one you're learning. (Presets, however, can intentionally map one CC to several ports — see below.)
- Only **CC messages** are used; notes, pitch bend, aftertouch etc. are ignored.
- A newly learned port gets the current default voltage range (the one ticked under *Output voltage range → All ports*).

---

## Right-click menu

### MIDI driver / MIDI device

Selects which MIDI input the module listens to. Defaults to the first available driver and device, so it usually works out of the box. If you plug in your controller after adding the module, reselect it here.

### Channel filter

- **Omni (all channels)** — default; responds to CC on any channel.
- **1–16** — respond only to the selected MIDI channel. Useful when several controllers (or several modules) share one MIDI device.

### Output voltage range

Sets the output scaling. CC value 0 always maps to the minimum, 127 to the maximum.

| Range | Best for |
|---|---|
| **0–10 V** (default) | General CV, modulation depths |
| **0–5 V** | 1V/octave-style duties, gentler modulation |
| **0–1 V** | Fine control, attenuverter inputs |
| **±10 V** | Bipolar modulation (pan, FM, etc.) |
| **±5 V** | Bipolar, standard Eurorack modulation |
| **±1 V** | Subtle bipolar movement |

Ranges can be applied:

- **All ports** — sets every port *and* becomes the default for newly learned ports.
- **Row → Row n** — sets one row only (does not change the default).

A port's current range is shown in its tooltip, e.g. `CC 34 (±5 V)`.

### Rows / Columns

Resizes the grid: **1–8 rows** and **1–16 columns**, up to 128 ports. The panel widens/narrows automatically, and cells shrink when you add rows so the module always stays standard rack height.

> Resizing never deletes CC assignments — if you shrink the grid, mappings on hidden ports are kept and reappear when you enlarge it again.

### Clear all mappings

Removes every CC assignment (leaves grid size, ranges, and MIDI settings alone).

### About FaderFox

A short note about the hardware that inspired the module, with a link to [faderfox.de](https://faderfox.de/).

---

## Included presets

The module ships with ready-made presets (module right-click → *Preset*, or the preset menu in the toolbar). They reflect the factory CC layouts of the FaderFox PC12:

| Preset | Contents |
|---|---|
| **PC12 factory (setups 01–20)** | Sequential CC 1–72 across the grid (row by row), as in the PC12's factory setups |
| **Elektron Rytm (setup 21)** | PC12 factory setup 21: CC groups tuned for the Elektron Analog Rytm |
| **Elektron Rytm MX12 extension (setup 22)** | Setup 22: extension layout for the Rytm + MX12 combo |
| **Elektron Octatrack + A4 (setup 23)** | Setup 23: Octatrack and Analog Four control |
| **Elektron Octatrack + A4 extension (setup 24)** | Setup 24: extension layout for the Octatrack + A4 combo |
| **Ableton Live (setup 30)** | Setup 30: CC banks laid out for Ableton Live |
| **Quick assign – row by row** | Contiguous CC 10+ filled along rows — a clean starting point for your own layouts |
| **Quick assign – column by column** | Contiguous CC 10+ filled down columns |
| **Quick assign – blocks of 4** | CC 10+ in 4-column blocks, one block per section |

Tip: the Elektron presets deliberately map **one CC to several ports**, so a single hardware encoder drives multiple Rack parameters at once. Load one to see this in action.

---

## Behavior details

- **Output smoothing:** outputs are smoothed with a ~2 ms slew, so 7-bit MIDI steps don't zipper-click your audio.
- **Unmapped ports output 0 V** and refuse cables.
- **Sampling:** MIDI is read at audio rate; multiple CC messages in one audio block are all applied.
- **Patch storage:** rows, columns, channel filter, all CC mappings, per-port ranges, and the default range are saved in the `.vcv` patch and in presets. The MIDI driver/device choice is *not* stored in the patch (it follows your system).
- **Maximum grid:** 8 rows × 16 columns = 128 simultaneous CV outputs.

---

## Disclaimer

This module is not made by, endorsed by, or affiliated with FaderFox. It is an independent, fan-made tribute to the PC12's workflow. If you like it, check out the real hardware at [faderfox.de](https://faderfox.de/) — built like tanks, and there are no better performance machines.
