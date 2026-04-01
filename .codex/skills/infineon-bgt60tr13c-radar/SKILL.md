---
name: infineon-bgt60tr13c-radar
description: Use when working on this repo's ESP32-P4 + Infineon BGT60TR13C radar path, especially for sensor bring-up, SPI/FIFO debugging, distance tracking, or the ported presence pipeline.
---

# Infineon BGT60TR13C Radar

Reference implementations:

- Official Arduino example: `https://github.com/Infineon/arduino-xensiv-radar-sensor-bgt60tr13`
- Local reference script: `E:/Infineon_test/vital_signs_detector.py`

Use this skill when the task touches the Infineon radar path in this repo rather than the legacy `my_lidar` stack.

## Scope

This implementation currently provides:

- ESP32-P4 board bring-up for the Infineon wingboard.
- SPI mode 0 communication with LP GPIO hold release before bus init.
- Radar power enable through `LDO_EN`.
- TR13C register bring-up through `sensor-xensiv-bgt60trxx`.
- A distance-measure style loop that rearms each measurement instead of relying on free-running IRQ forever.
- FIFO slice reads (`4096` samples per slice) to stay within the hardware FIFO limit.
- Coherent range processing across `64` chirps on `RX1`.
- Tracked nearest-target distance export in centimeters.
- Presence export ported from `vital_signs_detector.py`, including phase, amplitude, and bin-stability features.

## Primary Files

- `components/my_lidar_inf/my_lidar_inf.c`
  Main task, runtime profile constants, SPI/FIFO handling, distance tracking, presence pipeline, and live logging.
- `components/my_lidar_inf/my_lidar_inf.h`
  Public export structure and getter API.
- `components/my_lidar_inf/radar_settings_tr13c.h`
  Baseline TR13C register list aligned to the current 59-63 GHz profile.
- `components/my_lidar_inf/xensiv_radar_presence_impl.c`
  Older presence helper path and CLI defaults that still matter for some tuning commands.
- `dependencies/sensor-xensiv-bgt60trxx/xensiv_bgt60trxx.c`
  Register-level core driver, chip ID compatibility handling, and GSR0 diagnostics.
- `dependencies/sensor-xensiv-bgt60trxx/xensiv_bgt60trxx_esp.c`
  ESP-IDF transport layer, including FIFO burst reads.

## Current Runtime Truth

The live configuration is defined in `components/my_lidar_inf/my_lidar_inf.c`, not by stale comments or the old single-chirp path.

- `spi_frequency = 20 MHz`
- `start_freq_hz = 59 GHz`
- `bandwidth_hz = 4 GHz`
- `samples_per_chirp = 128`
- `num_chirps_per_frame = 64`
- `num_rx_antennas = 3`
- `adc_div = 80`
- `vga_gain_rx1 = 5`
- `distance_threshold_db = -5.0`
- `distance_first_valid_bin = 8`
- `main_rx_idx = 1`
- `fifo_slice_samples = 4096`
- `measurement_rearm_delay_ms = 100`

Expected range-bin length is about `3.75 cm`, so:

- `bin 8 ~= 30.0 cm`
- `bin 13 ~= 48.7 cm`
- `bin 15 ~= 56.2 cm`

## Live Export API

The public export is `radar_data_t` in `components/my_lidar_inf/my_lidar_inf.h`.

Distance-related fields:

- `target_detected`
- `distance_cm`
- `signal_db`
- `range_bin`
- `movement_energy`
- `frame_counter`

Presence-related fields:

- `presence_detected`
- `presence_confidence`
- `presence_distance_cm`
- `phase_excursion_mm`
- `amplitude_cv`
- `bin_span`
- `breath_present`
- `phase_present`
- `amplitude_present`
- `bin_present`

Getter:

```c
radar_data_t data = {0};
my_lidar_inf_get_data(&data);
```

## Measurement Loop

The current live path is:

1. Wait for IRQ high for each FIFO slice.
2. Read `4096` samples per slice until one frame is assembled.
3. If a slice read fails, inspect FIFO status and recover by restarting frame generation.
4. Run range FFT for every chirp on `RX1`.
5. Coherently average the complex FFT bins across chirps.
6. Use tracked-bin logic inspired by `vital_signs_detector.py` to choose the target range bin.
7. Export distance metrics from the tracked bin.
8. Update presence features from the tracked bin history.
9. Delay `100 ms`, reset FIFO, and rearm the next measurement.

This means the current distance and presence outputs are frame-level and chirp-aggregated, not the old `RX0 + chirp0` shortcut.

## Distance Logic

Distance tracking is now ported from the Python reference:

- Search only in the valid bin window starting at `RADAR_DISTANCE_FIRST_VALID_BIN`.
- Prefer staying near the previous tracked bin unless a global candidate is meaningfully stronger.
- `RADAR_RANGE_TRACK_LOCAL_RADIUS_BINS = 3`
- `RADAR_RANGE_TRACK_SWITCH_RATIO = 1.35`
- `RADAR_RANGE_TRACK_MAX_STEP_BINS = 1`
- Keep a short tracked-bin history and export the median bin.

Detection rule:

- `target_detected = (signal_db > RADAR_DISTANCE_THRESHOLD_DB)`
- With the current threshold, `-0.91 dB` and `4.27 dB` both count as detected.

## Presence Logic

Presence logic is ported from `E:/Infineon_test/vital_signs_detector.py` and uses a rolling history instead of the older macro/micro callback state machine.

Windows and thresholds:

- `phase_window_seconds = 6.0`
- `amp_window_seconds = 5.0`
- `bin_window_seconds = 5.0`
- `breath_window_seconds = 10.0`
- `phase_excursion_mm_th = 0.08`
- `amplitude_cv_th = 0.35`
- `bin_span_th = 2.0`
- `presence_confidence_th = 0.35`
- `presence_miss_limit = 3`

Confidence weights:

- `breath_present -> +0.55`
- `phase_present -> +0.25`
- `amplitude_present -> +0.10`
- `bin_present -> +0.10`

Raw presence is considered true when:

- `confidence >= 0.35`, or
- `breath_present`, or
- `phase_present && bin_present`

The exported `presence_detected` keeps a short miss latch so it does not drop immediately on one weak frame.

## Expected Logs

Healthy startup usually includes:

- `Applied distance profile: ...`
- `Profile registers: ...`
- `Frame control: ...`
- `Sensor initialized OK`
- `Radar distance/presence measurement started. ...`
- `Presence reference: main_rx=1 ...`

Healthy runtime usually includes:

- `Distance export: detected=yes distance=48.7cm bin=13 level=-0.91dB movement=0.256 frame=393`
- `Presence export: detected=yes confidence=0.90 distance=48.7cm phase_exc=1.313mm amp_cv=0.191 bin_span=5.0 flags(breath=1 phase=1 amp=1 bin=0)`

`CLK_NUM_ERR` may still appear in raw SPI register logs. Treat it as secondary unless it correlates with repeated FIFO failures or stalled frame counters.

## Build Requirements

This repo currently needs `ESP_IDF_VERSION=5.5` in the environment before `idf.py reconfigure` and `idf.py build`. Without it, `esp_wifi_remote` does not load the correct Kconfig file and the build fails on missing `CONFIG_WIFI_RMT_*` macros.

Recommended Windows environment:

```powershell
$env:IDF_PATH='C:\Users\18049\esp\v5.5\esp-idf'
$env:ADF_PATH='C:\Users\18049\esp\v5.5\esp-adf'
$env:IDF_PYTHON_ENV_PATH='C:\Users\18049\.espressif\python_env\idf5.5_py3.11_env'
$env:ESP_IDF_VERSION='5.5'
idf.py reconfigure
idf.py build
```

## Safe Tuning Knobs

When behavior needs tuning, adjust these in `components/my_lidar_inf/my_lidar_inf.c` first:

- `RADAR_DISTANCE_THRESHOLD_DB`
  Raise it to reduce false positives, lower it to improve weak-target detection.
- `RADAR_DISTANCE_FIRST_VALID_BIN`
  Raise it to suppress near-field coupling, lower it only when you truly need closer range.
- `RADAR_MAIN_RX_IDX`
  Switch the main receive channel only after validating actual board wiring and antenna behavior.
- `RADAR_RANGE_TRACK_SWITCH_RATIO`
  Raise it to reduce range-bin hopping, lower it to let the tracker switch targets faster.
- `RADAR_PRESENCE_PHASE_EXC_MM_TH`
  Raise it if phase-based presence is too sensitive.
- `RADAR_PRESENCE_AMP_CV_TH`
  Lower it if noisy amplitude traces are causing false presence.
- `RADAR_PRESENCE_BIN_SPAN_TH`
  Raise it if a moving subject should still count as stable presence.

Do not start by changing SPI mode, register-map addresses, or the base driver unless runtime evidence points there.

## Constraints

- Do not mix this skill with the legacy `my_lidar` heart-rate or sleep pipeline.
- Do not assume static `radar_settings_tr13c.h` comments are enough; verify against `my_lidar_inf.c`.
- Do not commit `dependencies.lock` path rewrites blindly if they only reflect local absolute paths.
- Do not treat one field alone as presence truth. The intended signal is the combined export, especially `presence_confidence`, `phase_excursion_mm`, `amplitude_cv`, and `bin_span`.
