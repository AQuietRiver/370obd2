# 370obd2

A C++17 OBD-II scanner for the 2009/2010 Nissan 370Z (Z34), with a CLI and a Qt6 desktop GUI.

## Features

- **Standard PID discovery** — queries which Mode 01 PIDs the vehicle supports, then reads and decodes them (engine load, RPM, coolant/intake temp, MAF, throttle position, fuel trims, etc.).
- **Enhanced PIDs** — manufacturer-specific PIDs loaded from a CSV profile (`data/`), so Z34-specific sensors can be added without recompiling.
- **Diagnostic trouble codes (DTCs)**:
  - Mode 03 — stored/confirmed codes (MIL-on)
  - Mode 07 — pending codes (detected, not yet confirmed)
  - Mode 0A — permanent codes (not clearable by a scan tool)
  - Mode 04 — clear stored/pending codes
- **Two transports** — a built-in simulator for development without hardware, and a POSIX serial transport for a real ELM327 USB adapter.
- **Qt6 GUI**:
  - Sensor and DTC tables, with buttons to scan or clear DTCs on either the simulator or a connected vehicle (clearing on a real vehicle asks for confirmation first, since it resets the MIL and monitor status).
  - Built-in PDF viewer for factory service manuals.
  - An OpenRouter-powered diagnostic assistant: ask questions about a scan/DTC snapshot, and it can search the loaded service manuals and your technician logbook for relevant context. Requires your own OpenRouter API key, entered at runtime (never written to disk in plain text — optionally saved to the OS keyring).
  - A local technician logbook (`logbook/`) for saving scan snapshots, notes, and AI analysis for future reference.

## Build

Requires CMake 3.20+ and a C++17 compiler. The GUI additionally requires Qt6 (Widgets, Network, SerialPort, Pdf, PdfWidgets) and Qt6Keychain.

```bash
# Ubuntu/Debian
sudo apt install cmake qt6-base-dev qt6-serialport-dev qt6keychain-dev
```

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

If Qt6 isn't found, CMake skips the GUI target and still builds the CLI and tests. Pass `-DBUILD_GUI=OFF` to skip it explicitly.

## Usage

CLI, against the built-in simulator:

```bash
./build/370obd2_cli
```

Against a real ELM327 adapter:

```bash
./build/370obd2_cli --serial /dev/ttyUSB0
```

Clear stored/pending DTCs first (opt-in — never happens implicitly):

```bash
./build/370obd2_cli --clear-dtcs --serial /dev/ttyUSB0
```

An enhanced-PID CSV profile can be passed as the last argument (defaults to `data/sample_nissan_z34_enhanced_pids.csv`).

GUI:

```bash
./build/370obd2_gui
```

## Layout

```
src/core/   OBD-II protocol logic: types, parser, PID registry, scan session, transports
src/gui/    Qt6 desktop app
src/main.cpp  CLI entry point
data/       Enhanced PID CSV profile(s)
tests/      Unit tests for src/core
docs/       Setup notes
logbook/    Local technician logbook (created/appended by the GUI)
```

## Notes

- The sample CSV in `data/` documents the enhanced-PID import format — it is not verified Nissan factory data.
- Factory service manual PDFs are not included in this repository (copyrighted); the GUI's manual viewer and assistant look for PDFs in a local `manuals/` directory that isn't tracked here.
