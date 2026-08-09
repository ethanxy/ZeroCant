# AGENTS.md

## Project overview

ESP32-S3 firmware for a flight-attitude / level / laser-rangefinder display device.

- Framework: ESP-IDF v5.4.2 + LVGL 8.3.11
- Target: `esp32s3` (with 8 MB Octal PSRAM, 16 MB flash)
- Display: SH8601 280x456 AMOLED (QSPI); IMU: QMI8658C (I2C); plus touch, ADC, laser.
- Entry point: `main/main.c` (`app_main`). Business logic lives in `components/` (e.g. `user_app`, `ui_state_manager`, `angle_display`, `level_display`, `laser`, `settings`).

This is embedded firmware: the normal dev loop is **build → flash → monitor** on real hardware. There is no local "server" to run.

## Cursor Cloud specific instructions

ESP-IDF v5.4.2 is preinstalled at `~/esp/esp-idf` (toolchain + tools in `~/.espressif`). It is NOT on `PATH` by default — source it once per shell before any `idf.py` command:

```bash
. $HOME/esp/esp-idf/export.sh
```

### Building

Always build with the orphan `ballistic_calc` component excluded:

```bash
idf.py -DEXCLUDE_COMPONENTS="ballistic_calc" build
```

- `components/ballistic_calc/CMakeLists.txt` declares `REQUIRES "esp_common" "m"`. `m` (libm) is not an ESP-IDF component, so leaving it in fails requirement expansion. `ballistic_calc` is not referenced by `main` or any other component (it is orphan/unused), so excluding it produces the real product firmware. Do NOT "fix" it by editing code unless the component is actually wired into the app.
- A successful build produces `build/FactoryProgram.bin` (~700 KB) and reports it fits the 15 MB `factory` app partition.

### Do NOT run `idf.py set-target`

The committed `sdkconfig` is the source of truth and already targets `esp32s3` with `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`. `idf.py set-target` regenerates `sdkconfig` from `sdkconfig.defaults`, which still has a stale `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y`; that shrinks flash to 8 MB and then the 15 MB `factory` partition in `partitions.csv` no longer fits, breaking the build. If `sdkconfig` ever gets clobbered, restore it with `git checkout -- sdkconfig`. Just run `idf.py build` (target comes from the committed `sdkconfig`).

### `dependencies.lock` / local LVGL

LVGL is a local component in `components/lvgl`. `dependencies.lock` pins it via an absolute `path:`. It must point at this checkout (`/workspace/components/lvgl` on the cloud VM). If it points elsewhere (e.g. a committed macOS path), the build fails with "the 'path' field ... does not point to a directory". The startup update script rewrites this path idempotently. Do NOT simply delete `dependencies.lock` to fix it — a full delete makes the component manager re-download `managed_components/espressif__esp_lcd_sh8601`, overwriting the committed (locally-adjusted) copy.

### Running / testing

- No hardware is attached in the cloud VM, so `idf.py flash`/`monitor` cannot reach a device.
- QEMU (`qemu-xtensa`) is installed. `idf.py qemu monitor` boots the image, but this board's Octal PSRAM and AMOLED/IMU peripherals are not emulated, so boot aborts at `octal_psram: PSRAM ID read error` after the bootloader loads the app. That is expected and confirms the image is a valid, bootable ESP32-S3 firmware; it is not a code defect.
- There is no automated test suite wired into the app build.
