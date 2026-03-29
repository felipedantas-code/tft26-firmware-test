# Tractian Fast Track 2026 — Firmware Challenge

Welcome! This repository contains everything you need to complete the firmware
challenge. Read this file carefully before you start.

> **Before anything else, read the challenge document:**
> [`doc/Firmware Tech Challenge.pdf`](doc/Firmware%20Tech%20Challenge.pdf)
>
> It contains the full problem statement, grading rubric, and all context
> needed to approach the exercises correctly.

## Repository structure

```
tft26-firmware-test/
│
├── hal/
│   ├── trac_fw_io.hpp          ← HAL header (read this first)
│   └── lib/<platform>/         ← pre-compiled libhal.a / hal.lib
│
├── example/main.cpp            ← reference example — start here
│
├── exercises/
│   ├── 01_parts_counter/       ← Exercise 01 (README + template)
│   ├── 02_frequency_estimator/ ← Exercise 02 (README + template)
│   └── 03_i2c_bitbang/         ← Exercise 03 (README + template)
│
├── simulator/                  ← Virtual board simulator (native GUI)
│   ├── simulator-macos
│   ├── simulator-linux
│   └── simulator-windows.exe
│
└── CMakeLists.txt
```

---

## How the simulator fits into your workflow

> **Keep the simulator open the entire time you are running your firmware.**

![Simulator demo](doc/simulator.gif)

The simulator is the virtual board your firmware runs against. It exposes
buttons, LEDs, an ADC channel, a display, and every other peripheral defined in
the HAL — all rendered as a live web UI at `http://localhost:8080`.

**You do not need to write any networking or UI code.** The pre-compiled HAL
library (`libhal.a`) handles all communication with the simulator transparently.
As long as you call the HAL functions correctly (e.g. `io.gpio_write()`,
`io.adc_read()`, `io.display_print()`, …), the simulator front-end will
automatically reflect the hardware state in real time — LEDs lighting up,
display updating, button presses being captured, and so on.

The expected workflow for every exercise is:

1. Start the simulator in one terminal and **leave it open**.
2. Build and run your firmware binary in a second terminal.
3. Watch the simulator front-end to verify that your firmware behaves correctly.

If the simulator is not running when your binary starts, the HAL calls will
fail silently or time-out. Always start the simulator first.

---

## Prerequisites

### macOS

1. Install **Xcode Command Line Tools** (provides `clang++`, `codesign`, etc.):

   ```bash
   xcode-select --install
   ```

2. Install **CMake** (e.g. via [Homebrew](https://brew.sh)):

   ```bash
   brew install cmake
   ```

3. Confirm both are available:

   ```bash
   cmake --version
   clang++ --version
   ```

CMake automatically selects `macos-arm64` or `macos-x86_64` based on your
processor. The pre-compiled HAL library must exist at
`hal/lib/<platform>/libhal.a` — it is included in this repository.

### Linux

```bash
sudo apt install build-essential cmake
```

The simulator also requires the WebKit2GTK runtime library:

```bash
sudo apt-get install libwebkit2gtk-4.1-0
```

> **No display server?** If you run the simulator over SSH or in a headless
> environment (no `$DISPLAY` / `$WAYLAND_DISPLAY`), the GUI will not open.
> The simulator will print the URL of its built-in HTTP server instead —
> open that URL in any browser on the same machine.

### Windows

1. Install **CMake** (>= 3.10) from cmake.org. Add to PATH during installation.

2. Install **Build Tools for Visual Studio** from visualstudio.microsoft.com/downloads.
   During installation, select **Desktop development with C++**.

> **Important:** always build from the **x64 Native Tools Command Prompt for VS 2022**
> (search for it in the Start menu). A regular PowerShell or CMD will not find
> the compiler and cmake will fail.

---

## Building an exercise

```bash
# Configure (run once from the repository root)
cmake -S . -B build

# Build a specific exercise
cmake --build build --target ex01   # or ex02

# Run (with the simulator already running in another terminal)
./build/ex01
```

> **Windows:** use the **x64 Native Tools Command Prompt** and run:
> ```
> cmake -B build
> cmake --build build --config Release
> ```
> Executables are output to `build\Release\` (e.g. `build\Release\ex01.exe`).
> If you see a *generator mismatch* error, delete the `build` folder and reconfigure.

---

## Running the simulator

Start the simulator **before** running your firmware binary. It provides the
virtual board: buttons, ADC, display, LEDs, and everything the HAL talks to.

A native window opens and serves the virtual board at `http://localhost:8080`.

### macOS

Downloaded or copied binaries may be quarantined by macOS. Run these steps
**once** from the repository root before launching for the first time:

**Step 1 — Remove quarantine attribute:**

```bash
xattr -c ./simulator/simulator-macos
```

**Step 2 — Re-sign locally** (required if the binary shows `killed` immediately
after launch, which means the code signature was invalidated during download):

```bash
codesign --force --sign - ./simulator/simulator-macos
```

Verify the signature is accepted:

```bash
codesign --verify --verbose=2 ./simulator/simulator-macos
```

> This modifies the binary on disk relative to Git. To restore the original:
> `git checkout -- simulator/simulator-macos` and re-apply the steps above if
> needed.

**Step 3 — Launch:**

```bash
# Option A — directly from the terminal
./simulator/simulator-macos

# Option B — ask macOS to open it as a GUI application
open ./simulator/simulator-macos
```

Use `Terminal.app` (Applications → Utilities → Terminal) rather than an
editor's integrated terminal if the window does not appear.

**Gatekeeper — "developer cannot be verified"**

If macOS blocks the binary on first open:

- **Finder:** right-click `simulator/simulator-macos` → **Open** → confirm, **or**
- **System Settings → Privacy & Security → Open Anyway** when macOS prompts.

### Linux

```bash
./simulator/simulator-linux
```

### Windows

```powershell
.\simulator\simulator-windows.exe
```

Or double-click the file in Explorer.

---

## Getting started

**1 — Fork the repository** on GitHub:
> `https://github.com/urielcontardi/tft26-firmware-test`

**2 — Clone your fork locally:**

```bash
git clone https://github.com/<your-username>/tft26-firmware-test.git
cd tft26-firmware-test
```

---

## Exercises

| # | Folder |
|---|---|
| 01 | `exercises/01_parts_counter/` |
| 02 | `exercises/02_frequency_estimator/` |
| 03 | `exercises/03_i2c_bitbang/` |

Each exercise folder contains a `README.md` with the full specification and
acceptance criteria, and a `main.cpp` template where you write your solution.

---

## Submitting

Once your solutions are complete, push your final commits:

```bash
git add exercises/01_parts_counter/main.cpp
git add exercises/02_frequency_estimator/main.cpp
git add exercises/03_i2c_bitbang/main.cpp
git commit -m "feat: firmware challenge solutions"
git push origin main
```

Then obtain the SHA of your last commit:

```bash
git rev-parse HEAD
```

Send **both** of the following to the recruiting team via the channel indicated
in your invitation email:

1. **Repository URL** — e.g. `https://github.com/<your-username>/tft26-firmware-test`
2. **Commit SHA** — the full 40-character hash output of `git rev-parse HEAD`

> Make sure the repository is **public** (or that the reviewing team has been
> granted access) at the time of submission.
>
> The commit SHA is used to identify your submission exactly. Any commits pushed
> after you send the SHA will not be considered.

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| `Operation not permitted` / binary does not open | `xattr -c ./simulator/simulator-macos`, then open via Finder right-click → **Open** (see macOS Step 1 above). |
| `killed` immediately on launch | `codesign --force --sign - ./simulator/simulator-macos` (macOS step 2). |
| No window appears | Use `open ./simulator/simulator-macos`; check **Cmd+Tab** and **Activity Monitor** to confirm the process is running. |
| Terminal closes immediately | Avoid terminal profiles that append `; exit`. Open a plain session and run commands manually. |
| `libwebkit2gtk-4.1.so.0: cannot open shared object file` (Linux) | `sudo apt-get install libwebkit2gtk-4.1-0` |
| HAL library not found (CMake error) | Verify `hal/lib/<platform>/libhal.a` exists. Run `git status` — the file may not have been checked out. |

---

## Tips

- Run the example first to get familiar with the HAL API:
  ```bash
  cmake --build build --target example && ./build/example
  ```
- Keep the simulator open while your firmware runs — see [How the simulator fits into your workflow](#how-the-simulator-fits-into-your-workflow) for details.
- All timing must be non-blocking: use `io.millis()` for delays; never call `io.delay()` inside a loop that must remain responsive.
