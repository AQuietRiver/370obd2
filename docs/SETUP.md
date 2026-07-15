# 370 OBD-II Scanner Setup

This project is a C++17 desktop scanner for a 2010 Nissan 370Z.

## Build tools

Install CMake and Qt 6 development packages before building the GUI. The GUI
also uses Qt Keychain so API keys can be stored in the operating-system keyring,
and `pdftotext` from Poppler to search service manuals.

On Ubuntu-like systems, the package names are usually:

```bash
sudo apt install cmake qt6-base-dev qt6-serialport-dev qt6keychain-dev poppler-utils
```

Build the core and tests:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

If Qt or Qt6Keychain is not installed, CMake skips the GUI target and still
builds the CLI and tests. To request a headless build explicitly:

```bash
cmake -S . -B build -DBUILD_GUI=OFF
```

Tests can be disabled with `-DBUILD_TESTS=OFF`. For a local AddressSanitizer and
UndefinedBehaviorSanitizer build on GCC or Clang, use
`-DENABLE_SANITIZERS=ON`.

## Service manuals and AI assistant

Place locally owned PDF service manuals in `manuals/`. These files are ignored
by Git. Opening a PDF is local; asking the diagnostic assistant can send
selected manual excerpts, the current sensor/DTC snapshot, and relevant Tech
Logbook entries to OpenRouter.

The AI assistant is optional and disabled until you provide an OpenRouter API
key. The key is held in memory and is only saved after explicit confirmation,
using the OS keyring with plaintext fallback disabled. OpenRouter model pricing
and data policies apply. Do not send customer information unless you have
permission to do so.

The local Tech Logbook is stored as plaintext in `logbook/TECH_LOGBOOK.md` and
may contain VINs, notes, scan snapshots, and AI responses. Protect or delete it
as appropriate for the vehicle and owner.

## Sensor coverage goal

The app must not rely on a short hard-coded sensor list. It should:

- ask the car which standard OBD-II PIDs are supported
- query every supported standard PID
- load Nissan/Z34 enhanced PID definitions from CSV
- show decoded values when formulas are known
- keep raw hex values when decoding is not known yet
- report unavailable sensors separately

The sample CSV in `data/` is not real Nissan service data. It only documents the import format.
