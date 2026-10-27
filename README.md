# own_pyocd — pyOCD GUI Front-End for STM32 / HK32 / GD32

A lightweight Windows GUI wrapper around the bundled [`pyocd`](https://github.com/pyocd/pyOCD)
command-line tool. It lets you **connect to an MCU, erase by sector or whole-chip,
flash firmware (BIN/HEX/ELF), and reset/run** — without needing a Python/conda
environment. The `pyocd.exe` and its `_internal/` runtime are shipped next to the
tool, so the only external dependency is the device `.pack` files.

## Features

- **Connect / Disconnect** to a target via a DAP probe (SWD/JTAG).
- **Target list** populated automatically from the bundled Keil/GigaDevice/HKMicroChip
  DFP `.pack` files (`pyocd list --targets --pack ...`).
- **Probe auto-detection** with multi-probe selection when more than one is connected.
- **Erase**:
  - Whole-chip erase (`pyocd erase --chip`).
  - **Sector erase** with a graphical sector selector built from the MCU flash
    layout parsed out of the device pack (PDSC).
  - Auto-select sectors covered by the loaded firmware image.
- **Download (flash)**:
  - Format: BIN (needs a base address), HEX, ELF (addresses taken from the file).
  - Four erase strategies before flashing:
    - `None` — write directly (target region must already be erased).
    - `Auto by firmware address` — erase only the sectors the image overlaps.
    - `Selected sectors` — erase the sectors chosen in the sector dialog.
    - `Whole chip` — `pyocd load -e chip`.
  - Optional reset-and-run after flashing.
- **Sector selector dialog**: lists every flash sector (address range + size),
  supports select-all / none / invert, and auto-checks the sectors the firmware
  occupies (HEX/ELF from file addresses, BIN from base address + file size).
- **Command queue**: when a download needs a pre-erase, the erase step(s) run
  first and the load runs automatically afterwards; the batch stops on any error.
- **Log panel** with copy/save, and live progress / status.

## Project Layout

```
own_pyocd/
├── README.md                 # this file (English)
├── README_zh.md              # Chinese version
├── pyocd_cli.bat             # optional: plain CLI entry point to pyocd.exe
├── pyocd-windows-0.46.0/     # bundled pyocd distribution (exe + _internal/)
│   └── pyocd.exe
├── packs/                    # device DFP packs (*.pack), auto-scanned at runtime
│   ├── Keil.STM32F4xx_DFP.3.1.1.pack
│   ├── GigaDevice.GD32F4xx_DFP.3.5.0.pack
│   └── HKMicroChip.HK32C030xx_DFP.1.0.5.pack
└── pyocd_ui/                 # Qt6 GUI application (this repo's source)
    ├── main.cpp
    ├── mainwindow.{h,cpp}
    ├── PyOcdRunner.{h,cpp}    # QProcess wrapper around pyocd.exe
    ├── PyocdLocator.{h,cpp}   # locate the bundled pyocd.exe
    ├── PackParser.{h,cpp}     # parse .pack PDSC -> flash layout / sectors
    ├── FirmwareParser.{h,cpp} # parse BIN/HEX/ELF -> address range
    ├── SectorSelectorDialog.{h,cpp}
    ├── build.bat             # one-click Windows build
    ├── CMakeLists.txt
    └── packaging/            # deploy.ps1 + WiX installer scripts
```

## Dependencies

| Component            | Version / Note                                   |
|----------------------|-------------------------------------------------|
| Qt                   | Qt 6 (Widgets)                                   |
| Compiler             | MinGW-w64 (GCC)                                  |
| Build system         | CMake ≥ 3.16 + Ninja                             |
| pyocd (bundled)      | `pyocd-windows-0.46.0` (already included)        |
| Device packs         | Any Keil/GigaDevice/HKMicroChip DFP `.pack`      |

> The `pyocd.exe` and its Python runtime are **third-party** and are **not** part
> of the source tree by design. See *Bundling pyocd* below.

## Getting the bundled pyocd

The GUI expects a directory named `pyocd-windows-<version>` next to `pyocd_ui/`,
containing `pyocd.exe` and its `_internal/` folder. Download the official
"standalone windows" release from the
[pyOCD releases page](https://github.com/pyocd/pyOCD/releases) and extract it so
the layout is:

```
own_pyocd/pyocd-windows-0.46.0/pyocd.exe
own_pyocd/pyocd-windows-0.46.0/_internal/...
```

(Or set `PYOCD_DIR` / adjust `PyocdLocator` if you keep it elsewhere.)

## Building (Windows)

Environment variables can override the default toolchain locations
(`build.bat` defaults to `D:\Software_Installed\...`, change them to your own):

```bat
set MINGW_DIR=D:\path\to\mingw64\bin
set QT_DIR=D:\path\to\qt_install
build.bat            rem incremental build
build.bat clean      rem clean + rebuild
build.bat nobuild    rem configure only
```

This runs `qt-cmake -B build -G Ninja`, builds with `ninja`, then runs
`windeployqt` so the `build/` folder is self-contained. The resulting executable
is `build/pyocd_ui.exe`.

To produce an installer, use `pyocd_ui/packaging/deploy.ps1` (WiX-based),
which also copies the third-party runtime DLLs from `packaging/runtime-deps/`
(e.g. `zstd.dll` required by `Qt6Core.dll`).

## Usage

1. **Select MCU target** from the drop-down (populated from the packs). Click
   *Refresh* if the list is empty.
2. **Select probe** (or leave "Auto" to auto-detect). Click *Connect*. A
   `commander status` + vector-table read confirms the link and reports whether
   flash is blank or already programmed.
3. **Load firmware**: pick a `.bin`/`.hex`/`.elf` file. For BIN, also enter the
   base address (e.g. `0x08000000`).
4. **Erase** (optional, before download):
   - *Whole chip* — confirm and erase everything.
   - *Sector* — click **Select sectors…** to open the sector dialog. Sectors
     overlapped by the loaded firmware are pre-checked automatically.
5. **Download**: choose the *Download erase* strategy, then click **Download
   firmware**. If a pre-erase is required, the tool erases the selected sectors
   first and then flashes — in one queued batch.
6. **Reset & Run** (if enabled) to start the application.

### Sector erase under the hood

`pyocd erase --sector 0x08000000-0x08003FFF` accepts **one** address range per
invocation, so multiple selected sectors are issued as **separate erase
commands** (queued), not a single command with many `--sector` arguments.

### Download erase strategies

| Mode                      | pyocd behavior                                            |
|---------------------------|-----------------------------------------------------------|
| None                      | `load` with no erase (region must be pre-erased)           |
| Auto by firmware address  | erase only overlapped sectors, then `load -e sector`       |
| Selected sectors          | erase chosen sectors, then `load -e sector`                |
| Whole chip                | `load -e chip` (pyocd erases internally)                  |

## Supported devices

Any MCU present in a bundled `.pack` whose PDSC exposes a flash region. Verified
families include STM32F4, GD32F4, and HK32C030. The flash layout / sector table
is derived from the pack; for unknown parts a default 2 KB sector size is used.

## Notes / Troubleshooting

- **`zstd.dll` missing on target machine** — it is a transitive dependency of
  `Qt6Core.dll`. `deploy.ps1` bundles it from `packaging/runtime-deps/`; make sure
  it ends up next to the executable in the installed package.
- **"unrecognized arguments" / `-e none` error** — `pyocd load -e` only accepts
  `auto|chip|sector`; this tool never passes `none`.
- **Multiple probes** — the GUI prompts you to pick one instead of guessing.

## License

See `LICENSE`. (Third-party components — pyocd, Qt, device packs, MinGW — keep
their own licenses.)
