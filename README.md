# HiJack breathing — project draft

**Team:** Kateřina Hoffmannová & Adrian Jiřík

**Status:** motion dashboard, calibration and CSV recorder are implemented. The current model recognizes all four classes from `Data/`, with ESP32 inference and the BLE mobile monitor. See [four-state training and results](training/FOUR_STATE.md), [mobile monitor](monitor/README.md) and [firmware instructions](firmware/README.md). The earlier two-class model remains available as a reproducible milestone.

**Mobile BLE monitor:** <https://adajirik.github.io/HiJack_breathing/>. Android: Chrome; iPhone: Bluefy. Connect to `HiJack-Detect`, then press Start detection. Blue means not attached, magenta shaking, green calm breathing and red fast breathing. The existing Wi-Fi dashboard remains at <http://192.168.4.1/>. Allow four seconds for the first classification.

**Current evaluation:** 100% held-out window accuracy and 1.0 macro-F1 on 104 overlapping windows from eight complete test recordings. This small test does not establish new-person or remounting performance; see the saved results and limitations. The prior two-class milestone scored 75%.

**Draft date:** 9 November 2026.

**Repository:** <https://github.com/AdaJirik/HiJack_breathing>

**Assignment:** [Real-time AI on a microcontroller — lab](https://tomasvicar.github.io/AUI-public/labs/real-time-ai.html).

This draft translates the assignment into a proposed implementation and a four-hour lab plan. Requirements below come from the assignment checked on the draft date. Sampling rates, model sizes, time limits and performance targets are starting proposals to validate on the actual hardware, not assignment requirements or measured results.

## 1. Goal and required outputs

Build a device using the supplied **ESP32-S3 N16R8** and **MPU-6050 / GY-521 accelerometer and gyroscope**. First demonstrate slow movement versus shaking, then extend the same device to all four states:

| State | LED | Example |
| --- | --- | --- |
| Calm breathing | Green | Sensor held steadily against the chest |
| Fast breathing | Red | Clearly faster breathing with the same placement |
| Not attached | Blue | Held in the hand, moved slowly, or lying on a table |
| Shaking | Magenta | Deliberately shaken device |

The ESP32 must acquire the signal, calculate features and run the trained classifier itself. During the demonstration USB supplies power only: disable USB data logging and do not send measurements to a computer for classification. The PC is used beforehand for recording, training and flashing. Wi-Fi and BLE provide the required displays.

| Deliverable | Points | Evidence to show live |
| --- | ---: | --- |
| Movement versus shaking | 2.5 | Slow movement gives blue; shaking gives magenta; switch back |
| Breathing and all four states | 2.5 | Calm/fast breathing, removal and shaking on one device |
| BLE monitor on GitHub Pages | 1.5 | State, timeline, two duration alarms and connection-loss alarm |
| Page served by the ESP32 | 1.0 | Live signal and probabilities for every class over its Wi-Fi hotspot |
| Own extension | 1.5 | Demonstrate and explain one working addition |
| Documentation | 1.0 | Reproducible wiring, firmware, training and results |

**Compulsory submission conditions:** share the repository with GitHub user `tomasvicar` before the lab ends, and make **at least three intermediate commits during the lab, pushing each as work progresses**. Missing either condition means zero points for the project. A planning commit before the lab does not replace these lab commits. Every scored part must be demonstrated to the teacher before the lab ends.

## 2. Proposed architecture

```text
MPU-6050 (accelerometer + gyroscope, I2C)
                  |
                  v
ESP32: timed sampling -> ring buffer -> features -> trained classifier
                                                    |
                                         probabilities + smoothing
                                                    |
                  +---------------------------------+------------------+
                  |                                 |                  |
              RGB LED                    Wi-Fi AP + HTTP page     BLE service
                                         signal + probabilities        |
                                                               GitHub Pages
                                                               state + timeline
                                                               duration/loss alarms

Development only: USB recordings -> Python training/evaluation -> C++ model export
```

Keep sampling independent of web requests and BLE notifications. Start with a small feature-based model so we can inspect its decisions and export it without a large inference framework.

## 3. Hardware and first bring-up

Available kit: ESP32-S3 N16R8, GY-521, breadboard, jumper wires and USB cable. Move the complete breadboard with both modules together; keep it steady against the chest for breathing recordings.

1. Identify the exact board variant and its pinout. Confirm the onboard RGB LED pin, LED type and colour order; do not assume a generic ESP32 pin mapping.
2. Wire the sensor's power, common ground, SDA and SCL. Confirm the breakout's supply requirements and I2C pull-ups; ESP32 GPIO signals must stay at 3.3 V logic levels. Record the final GPIO mapping here.
3. Run an I2C scan and check the sensor identity. Confirm its address, including the AD0 configuration.
4. Read all six axes at a stable rate. Check units, axis signs, missing samples and stationary noise. Photograph the wiring.
5. Test each required LED colour. Test BLE advertising and a notification with nRF Connect before writing the browser monitor.

| Connection | Planned value |
| --- | --- |
| GY-521 VCC -> board supply | Confirm from the actual module |
| GY-521 GND -> board GND | Common ground |
| GY-521 SDA -> ESP32 GPIO | TBD after board pinout check |
| GY-521 SCL -> ESP32 GPIO | TBD after board pinout check |
| Onboard RGB LED pin/type | TBD after board identification |

**Wiring photo:** pending; add `docs/wiring.jpg` and embed it in this README before submission.

Proposed tools: Arduino framework in PlatformIO, a compatible MPU-6050 driver, Python with NumPy/scikit-learn for training, and plain HTML/CSS/JavaScript for the monitor. Verify board support and pin working dependency versions when setting up; use Arduino IDE if it gets the supplied board running sooner.

## 4. Data collection and honest evaluation

Start with **50 Hz**, logging timestamps and six axes over USB during development. Verify the actual sampling rate rather than assuming the loop timing is exact. Proposed CSV fields:

```text
timestamp_us,ax,ay,az,gx,gy,gz,label,subject_id,session_id
```

Document acceleration and angular-velocity units, sensor ranges, filtering, mounting orientation and sampling configuration. Save one labelled recording per file and note transitions separately.

1. Record slow hand movement and shaking first: several independent takes per class, roughly 30–60 seconds per take.
2. Add calm breathing, fast breathing and detached recordings. Detached data must include both hand-held and lying still cases. Aim for at least three independent takes per final class, ideally across both team members, with remounting between takes.
3. Mark labels as instructed activities, not clinical diagnoses. Keep breathing demonstrations brief and comfortable. Exclude transition segments from initial training and test transitions separately.
4. **Split complete recordings into train/validation/test groups before windowing.** Never randomly split overlapping windows from the same recording across groups. Keep every split represented by all four classes; gather more recordings if necessary.
5. Fit scaling and any feature selection on training data only. Use validation data for tuning and keep the test recordings untouched until final evaluation. If enough data are available, also hold out one person to examine generalization.

Report recording counts and durations per class, the split manifest, confusion matrix, per-class precision/recall/F1 and macro-F1. Save the random seed and settings. Report performance on the device and state transitions separately from offline window accuracy.

## 5. Signal processing and model

**First milestone:** a trained two-class model that separates slow movement from shaking and drives blue/magenta on the board. A threshold rule is useful as a comparison and debugging tool; the final pipeline still includes a trained classifier.

**Four-class starting point:** an 8-second sliding window at 50 Hz, evaluated once per second. This is 400 samples per axis, about 9.6 kB for six float32 channels, excluding all other buffers. Compare 4-, 8- and 12-second windows using validation recordings: shorter windows react sooner but may contain too few slow breaths. Measure startup delay and transition delay explicitly.

Candidate features, kept small enough to reproduce in C++:

- Per-axis mean and standard deviation; dynamic acceleration RMS and peak-to-peak amplitude.
- Gyroscope RMS and acceleration-change energy to identify shaking.
- Low-frequency energy, dominant frequency and periodicity from a chest-sensitive axis or a projection learned using training data only.
- Optional log transforms for strictly positive amplitude features, with a fixed epsilon.

Avoid relying only on acceleration magnitude: small chest tilts can appear mainly as changes in gravity across individual axes. Inspect plots before choosing axes or filters. Keep identical feature order, units, filtering, scaling and numerical constants in Python and firmware.

Train a compact MLP, initially one hidden layer of about 16 units, and compare it with a simpler classifier. Export feature normalization, weights, biases, activation definitions and class order into a versioned C++ header. Use numerically stable softmax for the multiclass output and verify its outputs against Python on saved reference windows. Probabilities are model outputs, not guaranteed calibrated confidence estimates.

Smooth decisions over approximately two seconds and tune on validation data. Display a startup/buffering state until a full window exists. Explicitly report an I2C/sensor fault instead of continuing to display an old confident class. Record inference time, sampling overruns and memory use with Wi-Fi and BLE enabled together.

## 6. Two separate user interfaces

### ESP32 Wi-Fi page

- Start a Wi-Fi access point and serve a local HTTP page; print its SSID and actual page address in the final instructions.
- Show a scrolling measured-signal plot, the predicted state and four probability bars.
- Publish bounded recent signal samples and predictions through a small polling endpoint or WebSocket. Start with modest update rates and reduce display traffic if sampling suffers.
- Expose sample rate, dropped samples and model version for debugging. The displayed classification must come from the ESP32.

### GitHub Pages BLE monitoring station

- Host a static page over HTTPS; add the deployed URL to this README. If the main repository is private and the account cannot use Pages for it, use a separate public monitor repository as the assignment suggests.
- Use a Connect button to request Web Bluetooth access. Test early in a supported Chrome/Edge browser on a BLE-capable computer or Android Chrome; do not assume Safari, Firefox or iPhone Chrome will work.
- Define and document custom service/characteristic UUIDs. Notify the current state every second and on changes, with a sequence number, device uptime and model probabilities. Keep the payload within the negotiated BLE transport size, or define compact encoding/chunking explicitly.
- Show the current state, connection status, last update time and a timestamped state timeline. Use browser receipt times for the timeline and device uptime/sequence for detecting resets or gaps.
- Proposed demo limits: fast breathing continuously for **10 seconds**, detached continuously for **10 seconds**, and a stale-data alarm after **5 seconds** without updates. Also alarm immediately on a BLE disconnect event. These are demonstration settings, not medical thresholds.
- Reset each duration timer when its state ends. On disconnect or stale data mark the state unknown, stop class-duration accumulation and start fresh after reconnection. Acknowledge/mute should not hide the active alarm condition; sound, if added, requires a user gesture.

The board's HTTP page and the HTTPS BLE page have different jobs. The board's plain HTTP page cannot use Web Bluetooth. Test simultaneous Wi-Fi and BLE operation; use a BLE-capable laptop for the monitor if joining the ESP32 hotspot leaves a phone unable to load GitHub Pages.

**Monitoring page URL:** <https://adajirik.github.io/HiJack_breathing/> (two-state movement milestone; breathing alarms remain pending).

## 7. Own extension: live 3D orientation

The selected extension is a **rotating 3D sensor model**, served by the ESP32 over its own Wi-Fi. It includes horizontal stationary calibration, roll/pitch/yaw, gravity-compensated acceleration arrows and signed values, a movement intensity indicator and an acceleration history. The BLE monitor is reserved for the breathing task. The earlier breathing-rate idea is deferred.

The sensor can estimate orientation and acceleration, but cannot reliably track position or travel distance. Heading drifts without a magnetometer. Calibration resets heading and estimates stationary gyro bias. See [firmware/README.md](firmware/README.md) for setup, limitations and the physical validation checklist. The final board-served page must also gain the required breathing signal and class probability display.

## 8. Four-hour lab plan

| Time | Work | Checkpoint |
| --- | --- | --- |
| 00:00–00:25 | Wiring, sensor/LED checks, short BLE test, logger | Stable data and correct colours; **push lab commit 1** |
| 00:25–01:00 | Record movement/shaking, train, export, run on ESP32 | Demonstrate the first 2.5-point part; **push lab commit 2** |
| 01:00–01:55 | Record breathing/detached, evaluate four classes, deploy | Demonstrate four states; **push lab commit 3** |
| 01:55–02:35 | BLE notifications, Pages monitor, timeline and alarms | Show both duration alarms and disconnect; push progress |
| 02:35–03:05 | Board hotspot page, signal plot, probability bars | Verify displays while sampling and BLE run together |
| 03:05–03:30 | Breathing-rate extension if core work is stable | Compare estimate with reference; push progress |
| 03:30–04:00 | Integration checks, README/photo/results, teacher demos | Repository access verified and all working parts shown |

Schedule teacher demonstrations as soon as a part works. If breathing is unreliable, inspect placement, class balance and plots first; protect time for showing working parts and documenting actual limitations. Do not postpone all commits or demonstrations until the end.

## 9. Proposed repository structure

The motion firmware and diagnostic tools now exist. The remaining directories below are planned and should be added incrementally:

```text
firmware/              # ESP32 project, sampler, features, model, LED, BLE, HTTP
training/              # Recording tools, preprocessing, training, export, evaluation
data/README.md         # Dataset format, recording protocol and access instructions
data/splits.json       # Recording-level split manifest
models/                # Small exported model and training configuration
monitor/               # Static Web Bluetooth page for GitHub Pages
docs/                  # Wiring photo, diagrams, results and demo checklist
```

Exclude build output, local environments and credentials from version control. Decide how to store raw recordings before adding large files; keep enough documented sample data and settings for reproducibility.

## 10. Completion checklist

- [ ] Confirm team names, board details, wiring table and photo.
- [ ] Record labelled independent sessions and save the split manifest.
- [ ] Demonstrate movement/shaking and then all four required LED states.
- [ ] Match Python and ESP32 features/probabilities on reference windows.
- [ ] Run acquisition and inference without USB data or PC computation.
- [ ] Demonstrate BLE state/timeline, both duration alarms, stale data and disconnect handling.
- [ ] Deploy the monitor and insert its real GitHub Pages URL above.
- [ ] Demonstrate the board's own live signal/probability page.
- [ ] Validate and demonstrate the chosen extension.
- [ ] Record measured results, failure cases, build/flash commands and pinned dependencies.
- [ ] Make at least three intermediate lab commits and push each during the lab.
- [ ] Share the repository with `tomasvicar` and verify access before the lab ends.
- [ ] Show every completed part to the teacher before the lab ends.
- [ ] Document AI assistance and ensure both members can explain the code.

## 11. AI assistance and current limitations

Codex was used to read the assignment, prepare the initial plan and Git setup, and implement the motion firmware, embedded visualization, calibration and diagnostic tools. See the firmware instructions for validation details. Breathing classification accuracy and BLE behavior have not been validated. This is a coursework prototype, not a clinical monitor.
