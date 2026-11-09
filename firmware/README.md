# Motion extension: ESP32-hosted 3D orientation

This firmware implements the motion extension, CSV recording and a trained two-state movement detector with BLE control. Breathing classification and the full BLE breathing-alarm monitor remain pending. The embedded motion page needs no internet, CDN, external JavaScript library or separate filesystem upload.

## Movement detection over BLE

Open <https://adajirik.github.io/HiJack_breathing/> in Android Chrome or iPhone Bluefy, connect to **HiJack-Detect**, then press **Start detection**. The first result follows a one-second sampling window. Blue means `not_attached`; magenta means `shaking`. Stop turns the RGB LED off. The phone displays the same RGB state, plus connection status and recent movement history. All features/classification execute on the ESP32; USB is only needed for power during the demonstration. The motion dashboard and CSV recorder remain available independently over Wi-Fi.

See [training details and honest evaluation](../training/README.md) and [BLE protocol/mobile setup](../monitor/README.md). The current held-out window accuracy is 75%; one shaking recording is missed. This model deliberately does not classify either breathing class.

## Use

1. Power the board. Connect a phone or laptop to Wi-Fi **HiJack-Motion**, password **hijack32**. Stay connected if the operating system warns that this network has no internet.
2. Open **http://192.168.4.1/** in the browser, explicitly using HTTP.
3. Put the MPU-6050 flat with its component side up. Press **Calibrate level** and leave it still for about 3 seconds (0.6 seconds settling, then 2.5 seconds measuring). Normal sensor noise is allowed. Calibration is rejected for motion above the tolerance or if the sensor is not approximately horizontal.
4. Tilt and rotate the complete assembly. The 3D sensor model and roll/pitch/yaw follow the sensor. The model's axes correspond to the markings on the sensor module; it is a schematic model, not a scan of the breadboard.
5. The **Movement trace** shows live raw accelerometer X/Y/Z immediately, even before calibration. Each axis has its own graph, numeric reading and adaptive vertical scale, so small movements remain visible against the gravity baseline. Switch to **Gravity removed** after calibration for linear acceleration in the calibrated reference frame. The gold acceleration arrow, signed side-panel values and intensity glow also use linear acceleration. These are not estimates of displacement or velocity. Recalibrate to reset heading and gyro bias.

Calibration lasts until reset/reboot and is shared by all connected viewers. It uses stable online variance rather than absolute deviation from gravity to detect motion. Combined acceleration noise may be up to 0.35 m/s² RMS and gyro noise up to 1.5 degrees/s RMS; stationary gyro bias up to 15 degrees/s is allowed and removed. The mean acceleration magnitude must lie within 6.5–13 m/s², and the sensor must be face up within 25 degrees of level. A single uniform acceleration gain is estimated from the stationary gravity magnitude for fusion and linear acceleration; raw readings remain unchanged. This does not correct separate per-axis offsets or gains, which need a multi-position calibration.

Gravity provides roll/pitch correction; without a magnetometer yaw necessarily drifts. Sustained acceleration can also affect tilt estimates. The default ±2 g accelerometer and ±250 degrees/s gyroscope ranges can clip during strong shaking or quick rotations.

## Recording labelled CSV data

The separate **Record a measurement** panel leaves the motion dashboard in place:

1. Choose **Not attached**, **Shaking**, **Calm breathing** or **Fast breathing**.
2. Enter a custom base filename, prepare the sensor/activity and press **Record 10 seconds**. Capture starts immediately, with a countdown and sample count. Calibration is optional because the stored acceleration and gyro are raw, not orientation-dependent or bias-corrected.
3. The ESP32 captures all six axes locally at the 100 Hz target rate for a 10-second window (normally about 1,000 samples), independently of browser update speed. The dashboard continues to update. Calibration is temporarily unavailable while recording.
4. When the window ends, **Keep this measurement?** appears automatically. Choose **Confirm & save CSV** to download, or **Discard** to retry. An interrupted sensor read produces an error and cannot be confirmed. Low sample counts are flagged for review.
5. Confirmed files can be downloaded again until another measurement replaces them. Pending data remain available after refreshing/reconnecting the page, but live in board RAM and are lost on reset or power loss. Only one recording can exist on the device at a time, shared by all viewers.

The browser saves the file through its normal download behavior; it may ask where to save, or use its Downloads folder. Filenames contain only the provided base name and activity label, for example:

```text
adrian_session01_calm_breathing.csv
```

The type appears only in the filename as `not_attached`, `shaking`, `calm_breathing` or `fast_breathing`; it is not repeated in the data rows. No extra identifier is appended to the filename. Spaces and normal filename characters are preserved; characters invalid in Windows filenames are replaced with underscores. A provided `.csv` extension is removed before adding the label and final extension. No separate annotation file is needed. CSV columns:

```text
timestamp_us,ax_m_s2,ay_m_s2,az_m_s2,gx_deg_s,gy_deg_s,gz_deg_s,session_id,sample_index,device_sample_seq,calibrated,accel_scale
```

- `timestamp_us`: actual sample time relative to the recording start, in microseconds; rows are inside `[0, 10000000)`.
- Acceleration: sensor-frame m/s² including gravity, using the nominal sensor scale, without the orientation calibration's uniform gain.
- Gyroscope: sensor-frame degrees/s using the nominal sensor scale, without subtracting the stationary gyro bias. This deliberately differs from the bias-corrected Sensor stream display.
- `session_id`, zero-based `sample_index`, and `device_sample_seq`: preserve recording identity and help inspect timing/sample gaps. Split training/test data by whole recording rather than random rows/windows.
- `calibrated` and `accel_scale`: orientation-calibration state at capture start, stored for context; the CSV sensor values do not have that gain applied.

Record several independent takes for every class. Include both lying still and hand-held examples for Not attached. Ten seconds can contain few calm breaths; keep complete recordings together when training and consider longer recordings in a later iteration if needed.

## Wiring

| MPU-6050 breakout | ESP32-S3 |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO8 |
| SCL | GPIO9 |
| AD0 | GND; firmware also checks address 0x69 |
| XDA, XCL, INT | Unconnected |

The firmware verifies WHO_AM_I = 0x68, wakes the sensor, selects a 44 Hz accelerometer filter / 42 Hz gyro filter and a 100 Hz output rate. A FreeRTOS task reads six axes at a 100 Hz target rate using I2C at 400 kHz. Quaternion fusion runs on the ESP32. The browser requests snapshots up to 25 times/s and interpolates orientation for display. Gravity is removed in the world frame after applying the estimated uniform gain, and linear acceleration is expressed in the calibrated reference frame with light smoothing.

The main loop serves HTTP independently of sampling. Sensor faults freeze the model with an explicit unavailable indicator and trigger reconnection attempts. Network timeouts also mark the display as disconnected. The UI uses Canvas projection of 3D geometry with depth sorting and has no network dependencies.

## Build and upload (PowerShell, from the project root)

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install platformio==6.2.0 pyserial==3.5
.\.venv\Scripts\python.exe -m platformio run -d firmware
.\.venv\Scripts\python.exe -m platformio run -d firmware -t upload --upload-port COM3
```

Check the current port with `.\.venv\Scripts\python.exe -m serial.tools.list_ports -v`. COM3 was the detected CH343 USB-UART port during initial setup; Windows may assign a different port later. If automatic upload cannot enter bootloader mode, hold BOOT, tap RESET, release BOOT and retry.

Run `node tools/check_dashboard.cjs` to check the actual embedded script's raw/linear histories, scale selection, calibration controls and stale-data behavior. With the device flat and stationary, `.\.venv\Scripts\python.exe tools/inspect_device.py COM3 --calibrate` starts the same calibration used by the page and reports the measured noise and result.

The pinned PlatformIO platform provides Arduino-ESP32 2.0.17. The configuration targets ESP32-S3 with 16 MB QIO flash, without using PSRAM. No third-party sensor library is needed. USB serial at 115200 baud prints startup information, responds to `?` with one diagnostic snapshot and accepts `c` to start calibration for development checks. It does not continuously stream data. All acquisition, calibration and fusion run on the board.

## API

- `GET /api/state`: sensor health, quaternion `[w,x,y,z]`, raw acceleration in m/s², bias-corrected gyro in degrees/s, filtered linear acceleration in m/s², calibration progress, sample counter and sample age.
- `POST /api/calibrate`: start stationary level calibration; returns 202, or 503 if the sensor is unavailable. Repeat while calibrating returns 409.
- `POST /api/recording/start`: form fields `label` (one of the four label tokens) and `filename` (custom base). Returns 202, or an error if calibration/capture/review is already in progress.
- `GET /api/recording/status`: phase (0 idle, 1 active, 2 awaiting confirmation, 3 confirmed, 4 failed), sample count, elapsed milliseconds, label, filename and session ID. Also included in `/api/state`.
- `POST /api/recording/confirm`: form field `id`; only a completed, nonempty measurement can be confirmed.
- `GET /api/recording/download?id=...`: streams CSV only for that confirmed recording.
- `POST /api/recording/discard`: form field `id`; discard/cancel the current measurement. A new start cannot overwrite unconfirmed data.

## Verification checklist

Initial verification on 9 November 2026: firmware compiled successfully, was flashed to the connected ESP32-S3 on COM3, and the uploader verified the written data. A diagnostic request returned `ok: true` with a fresh six-axis sample (8 ms old). Browser layout and automated dashboard checks passed. The build used about 45 kB RAM and 769 kB flash. Physical level calibration, motion accuracy and opening the page over the hotspot still need hands-on checks. Windows Wi-Fi scanning was unavailable because location permission was disabled; no system settings were changed.

Calibration/trace fix verified later on 9 November 2026: the original sensor's stationary acceleration magnitude was about 9.13 m/s², which incorrectly failed the old ±0.6 m/s² gravity check despite low noise. After the update and verified upload, actual stationary calibration succeeded on the connected device. Measured combined noise was 0.0284 m/s² acceleration and 0.0552 degrees/s gyro, with a uniform acceleration gain of 1.07452. A fresh calibrated sample had linear acceleration `[0.0046, -0.0006, 0.0028]` m/s² and zero I2C read errors. Automated checks verified raw graphs before calibration, the linear-view toggle, independent axis scales, calibration messages and stale-history expiry. Motion rejection under deliberate shaking and dynamic orientation accuracy still need physical validation.

CSV recorder verification on 9 November 2026: the updated firmware compiled, uploaded and passed flash verification. The running device reported fresh measurements, idle recording state and zero sensor read errors. Native C++ tests exercised the actual recorder/calibration headers: 1,000 simulated samples inside a 10-second window, microsecond-clock rollover, immutable review data, sensor failure and buffer overflow. The actual embedded browser script passed confirmation-before-download, custom filename/type, countdown, discard and error-state tests in addition to the existing dashboard checks. The build used about 81 kB static RAM and 787 kB flash. The computer was not connected to the ESP32 hotspot, so a real browser CSV download over Wi-Fi remains a hands-on check.

- [ ] Boot reports the Wi-Fi access point and sensor snapshot reports `ok: true`.
- [ ] Dashboard loads over the board's Wi-Fi with internet access disabled.
- [ ] Level calibration succeeds; moving the sensor during calibration is rejected.
- [ ] Gentle roll/pitch/yaw rotations match the displayed axes and return near zero when restored.
- [ ] At rest after calibration, estimated linear acceleration is near zero.
- [ ] A gentle push shows an acceleration impulse; stopping shows the opposite impulse.
- [ ] Unplugging the sensor or losing Wi-Fi gives an explicit fault/stale indication.

Hardware checks must be recorded as observed; a successful compile/flash alone does not validate motion accuracy. The UI does not diagnose breathing or any medical condition.

## Sources and AI assistance

- [Espressif I2C API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/i2c.html)
- [Espressif Wi-Fi API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html)
- [PlatformIO ESP32-S3 board configuration](https://docs.platformio.org/en/latest/boards/espressif32/esp32-s3-devkitc-1.html)

Codex prepared this firmware, the embedded visualization, calibration, documentation and build/upload workflow. Both team members should inspect and understand the sensor scaling, quaternion convention, gravity removal and HTTP handlers.
