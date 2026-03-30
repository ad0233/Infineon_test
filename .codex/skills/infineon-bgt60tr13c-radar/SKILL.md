---
name: infineon-bgt60tr13c-radar
description: Use when working on this repo's ESP32-P4 + Infineon BGT60TR13C radar path, especially for sensor bring-up, SPI/FIFO debugging, presence detection, or the ported distance_measure-style nearest-target logging.
---

# Infineon BGT60TR13C Radar

Use this skill when the task touches the Infineon radar path in this repo rather than the older `my_lidar` protocol stack.

## Scope

This implementation currently provides:

- Board bring-up for the Infineon wingboard on ESP32-P4.
- SPI mode 0 communication with LP GPIO hold release before bus init.
- Radar power enable through `LDO_EN`.
- TR13C register bring-up through `sensor-xensiv-bgt60trxx`.
- Runtime profile override inspired by Infineon's Arduino `distance_measure`.
- FIFO polling, retry, and frame-generator restart after repeated errors.
- Presence detection with `MACRO`, `MICRO`, and `ABSENCE` callbacks.
- Nearest-target distance logging in centimeters using range FFT on `RX0 + chirp0`.

## Primary Files

- `components/my_lidar_inf/my_lidar_inf.c`
  Main task, runtime profile constants, SPI/FIFO handling, presence pipeline, and nearest-target logging.
- `components/my_lidar_inf/xensiv_radar_presence_impl.c`
  Presence helper math, including range bin length.
- `dependencies/sensor-xensiv-bgt60trxx/xensiv_bgt60trxx.c`
  Register-level core driver, chip ID compatibility handling, and GSR0 diagnostics.
- `dependencies/sensor-xensiv-bgt60trxx/xensiv_bgt60trxx_esp.c`
  ESP-IDF transport layer, including FIFO burst read handling.
- `components/my_audoi_board/CMakeLists.txt`
  Public driver dependencies needed for board headers to compile cleanly.

## Current Runtime Profile

The current runtime truth lives in `components/my_lidar_inf/my_lidar_inf.c`, not in the static `radar_settings_tr13c.h` metadata:

- `start_freq_hz = 58 GHz`
- `bandwidth_hz = 4.5 GHz`
- `samples_per_chirp = 64`
- `num_chirps_per_frame = 64`
- `num_rx_antennas = 3`
- `adc_div = 60`
- `vga_gain_rx1 = 3`
- `distance_threshold = 2.1 dB`
- `distance_first_valid_bin = 4`

With this profile the expected range-bin length is about `0.033 m`, and the nearest-target detector intentionally ignores the first `~13.2 cm` to suppress near-field coupling.

## Startup Flow

Follow this order when modifying initialization:

1. Release GPIO hold on radar LP GPIOs before `spi_bus_initialize`.
2. Initialize SPI bus and attach the device in mode 0.
3. Configure `LDO_EN` as output and drive it high.
4. Call `xensiv_bgt60trxx_esp_init(...)` with the base register list.
5. Apply the runtime distance profile with register read-modify-write.
6. Configure IRQ as input and set FIFO limit to the full frame size.
7. Start frame generation from the radar task.

If you reorder these steps, expect false chip IDs, FIFO errors, or a dead sensor.

## Data Path

The current live path is:

1. Poll IRQ high.
2. Read one full FIFO frame.
3. Retry once on failure.
4. Read FIFO status and restart frame generation after repeated failures.
5. Extract `RX0` samples for `chirp0`.
6. Copy the chirp once for nearest-target distance detection.
7. Feed the original chirp into the presence library.

This means the current distance and presence outputs are both single-RX, single-chirp derived even though the frame contains 3 RX channels and 64 chirps.

## Expected Logs

Healthy startup usually includes:

- `Applied distance profile: ...`
- `Profile registers: ...`
- `Range bin length=0.033m ...`
- `Sensor initialized OK`
- `Radar presence detection started. bin_length=0.033m`

Healthy runtime usually includes:

- `Radar frames processed: ...`
- `Nearest target: xx.x cm (bin=n, level=y dB)`
- `[MACRO PRESENCE] ...`
- `[MICRO PRESENCE] ...`
- `[ABSENCE] ...`

Occasional `FOU_ERR`, `SPI_BURST_ERR`, or `CLK_NUM_ERR` during startup have been observed. Treat them as secondary unless they correlate with repeated `get_fifo_data failed` logs or loss of runtime detection.

## Safe Tuning Knobs

When distance logs are missing or unstable, adjust these in `components/my_lidar_inf/my_lidar_inf.c` first:

- `RADAR_DISTANCE_THRESHOLD_DB`
  Lower it if no target is detected at valid ranges.
- `RADAR_DISTANCE_FIRST_VALID_BIN`
  Reduce it only if you need closer detection and can tolerate more near-field coupling.
- `RADAR_PRESENCE_MAX_RANGE_M`
  Shrink or expand presence range without rewriting FFT logic.
- `macro_threshold` / `micro_threshold`
  Tune presence sensitivity after the data path is confirmed healthy.

Do not start by changing SPI mode, frame size, or the base register list unless runtime evidence points there.

## Constraints

- Do not assume unknown chip IDs are fatal on this board; compatibility fallback is already in place.
- Do not re-enable GPIO ISR-based IRQ handling unless you have a strong reason; polling avoided WDT issues on ESP32-P4.
- Do not mix this skill with the legacy `my_lidar` feature set. Heart rate, respiration, and richer sleep metrics still belong to the older stack, not this Infineon path.
