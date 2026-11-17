# RF / IR Capture-&-Replay Demonstrator (Arduino UNO)

An educational Arduino UNO project that captures a short RF (nRF24L01) or IR
frame and replays it on demand, with a live serial log, status/activity LEDs,
and a ready-to-run Wokwi simulation. It demonstrates how **fixed-code**
capture/replay works on a wireless link — and why that approach does **not**
defeat modern rolling-code car key fobs.

## What it does

- **Capture** — on the CAPTURE button (or an incoming frame) it stores the next
  RF payload into a buffer.
- **Replay** — on the REPLAY button it re-transmits the exact captured bytes,
  and mirrors the action on the IR channel.
- **IR trigger** — a recognised IR code also fires a replay.
- **Reset** — clears the capture buffer.
- **Status** — a heartbeat LED, RX/TX activity LEDs, and a 9600-baud serial log
  of every action and payload.
- Gracefully reports and disables RF when no nRF24 module is present (e.g. in a
  simulator) instead of hanging.

Builds for `arduino:avr:uno` in **7764 B flash (24%)** / **511 B RAM (24%)**.

## Pin map (UNO)

| Signal | Pin | Note |
|--------|-----|------|
| IR receiver OUT | D7 | |
| IR LED (via transistor) | D3 | IRremote AVR carrier pin |
| nRF24 CE / CSN | D9 / D10 | |
| nRF24 SCK/MOSI/MISO | D13 / D11 / D12 | hardware SPI |
| Button CAPTURE / REPLAY / RESET | D4 / D5 / D6 | to GND, internal pull-up |
| Status LED (heartbeat) | D8 | +220 Ω |
| RX / TX activity LED | A0 / A1 | +220 Ω |
| USB serial log | D0 / D1 | 9600 baud |

Libraries: **IRremote 4.x**, **RF24 1.x**.

## Build

```bash
arduino-cli core install arduino:avr
arduino-cli lib install IRremote RF24
arduino-cli compile --fqbn arduino:avr:uno .
arduino-cli upload  --fqbn arduino:avr:uno -p COMx .
```

## Run in simulation (Wokwi)

Files `diagram.json`, `libraries.txt`, and `wokwi.toml` are included.

- **Browser:** open <https://wokwi.com>, create a new Arduino UNO project, paste
  the `.ino` + `diagram.json`, add the libraries via **Library Manager → +**
  (*IRremote*, then *RF24*), press ▶ and open the Serial Monitor.
- **VS Code:** install *Wokwi for VS Code*, build with
  `arduino-cli compile --fqbn arduino:avr:uno --output-dir build .`, then start it.

The nRF24L01 radio is not modelled by Wokwi, so the sketch takes its graceful
"RF disabled" path; the button/LED/serial logic and the capture→replay flow all
run. Real serial output from a live wokwi.com run is in
[`wokwi_live_serial.txt`](wokwi_live_serial.txt); `simulation_run.mp4` / `.gif`
are rendered animations of the run. Set `#define DEMO_SELFTEST 1` to run one
capture→replay cycle automatically (the default `0` is button-driven).

## Buttons

- **CAPTURE (D4)** — store the next RF frame (or a labelled demo payload in sim).
- **REPLAY (D5)** — re-transmit the captured bytes.
- **RESET (D6)** — clear the capture buffer.

## Hardware notes

- The nRF24L01 is a **2.4 GHz** transceiver — it links to another nRF24, and is
  **not** a 315/433 MHz receiver, so it cannot receive car-fob signals. For real
  315/433 MHz work you need a different radio (e.g. CC1101 or an SDR).
- Power the nRF24 from **3.3 V** (not 5 V) with a decoupling capacitor across its
  supply for stable operation.
