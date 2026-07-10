# 370 OBD-II Scanner Setup

This project is a C++17 desktop scanner for a 2010 Nissan 370Z.

## Build tools

Install CMake and Qt 6 development packages before building the GUI.

On Ubuntu-like systems, the package names are usually:

```bash
sudo apt install cmake qt6-base-dev qt6-serialport-dev
```

Build the core and tests:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

If Qt is not installed, CMake skips the GUI target and still builds the CLI and tests.

## Sensor coverage goal

The app must not rely on a short hard-coded sensor list. It should:

- ask the car which standard OBD-II PIDs are supported
- query every supported standard PID
- load Nissan/Z34 enhanced PID definitions from CSV
- show decoded values when formulas are known
- keep raw hex values when decoding is not known yet
- report unavailable sensors separately

The sample CSV in `data/` is not real Nissan service data. It only documents the import format.
