# Four-state breathing and movement classifier

The current firmware uses this model. The preceding two-class model and training script remain in the repository as a reproducible milestone.

## Reproduce

From the repository root, with the training requirements installed:

```powershell
.\.venv\Scripts\python.exe training/train_four_state.py
.\.venv\Scripts\python.exe -m platformio run -d firmware -t upload --upload-port COM3
```

The script reads all 40 recordings in `Data/` (ten per class), derives labels from filenames, validates timestamps and sampling rate, and rejects duplicate recording IDs/content. Sensor inputs remain raw acceleration in m/s² and raw gyro in degrees/s. The calibration, capture duration, CSV schema and filenames are unchanged.

## Features and model

The full classifier uses a 400-sample (4-second) window at 100 Hz, advancing every 50 samples (500 ms). Features include the existing six motion features, the standard deviation of each of the six axes, and low-frequency acceleration/gyro powers at 0.15, 0.25, 0.4, 0.6, 0.9 and 1.2 Hz. Signals are centered and Hann-windowed for these spectral features. Python uses Fourier projections; C++ uses mathematically equivalent Goertzel accumulation in double precision. Features have 24 values, most using log transforms.

The script compares normalized multinomial logistic regression and 16/24-unit MLPs using four-fold stratified recording-group cross-validation on development recordings. The 16-unit MLP is selected; the larger model ties it and adds no measured benefit. All normalization is fitted within training folds, and the final scaler/model use only the 32 development recordings.

`firmware/include/four_state_model.h` exports normalization, learned weights, biases, ReLU hidden activation and numerically stable four-class softmax. Class order is fixed as `not_attached`, `shaking`, `calm_breathing`, `fast_breathing`. No training library runs on the ESP32.

Outputs use a 0.5 exponential probability average and require two consecutive different-class winners before switching, reducing LED flicker. The first result needs four seconds; transitions can take several seconds because the window contains previous activity. Sensor faults clear the window and require fresh samples. STOP clears the detection state and turns off the RGB LED. Calibration and CSV collection remain independent.

The sensor task feeds a bounded ring buffer on core 1. A separate classifier task on core 0 takes a short, locked chronological snapshot and performs feature extraction/inference outside the lock. This prevents the roughly 41 ms spectral feature computation observed during development from blocking the 100 Hz sensor task or CSV capture. In-flight results are discarded when a stop, sensor fault or restart changes the buffer generation.

## Evaluation

Complete recordings are assigned to train/validation/test before overlapping windows are created. The original deterministic seed is preserved. There are 24 initial train, 8 validation and **8 held-out test recordings**, two per class. Cross-validation for model selection uses train+validation only. The final fitted model does not include test recordings.

Held-out window accuracy and macro-F1 are both **1.0** on **104 windows**. The confusion matrix is:

| Actual / predicted | Not attached | Shaking | Calm breathing | Fast breathing |
| --- | ---: | ---: | ---: | ---: |
| Not attached | 26 | 0 | 0 | 0 |
| Shaking | 0 | 26 | 0 | 0 |
| Calm breathing | 0 | 0 | 26 | 0 |
| Fast breathing | 0 | 0 | 0 | 26 |

This is a small, single-collection dataset. Overlapping windows are not 104 independent experiments. Development CV scores vary substantially by fold (about 0.81–1.0 macro-F1), and no new-person or remounting test has been conducted. A four-second window may contain only part of a calm breath. These results do not establish clinical validity or generalization beyond the recordings. Live breathing/transition accuracy must still be checked with the actual mounting and both team members. Sensor saturation found in the earlier movement quality report remains a limitation; ranges are unchanged to match training.

Reports, parameters, split manifest, source hashes and reference samples are in `models/four_state_*.json` and `models/four_state_reference.csv`. No test-driven threshold tuning is performed.

## Displays and protocol

The existing BLE service/name, START/STOP commands, 20-byte packet and state IDs 0–4 are preserved. State 5 adds calm breathing and state 6 adds fast breathing. Protocol byte remains 1; the page must be refreshed to accept the extended states. Existing IDs 2/3 remain blue/magenta. Green `[0,255,0]` means calm breathing; red `[255,0,0]` means fast breathing. The same function generates board LED and phone colours.

The Wi-Fi dashboard adds a separate four-class probability panel without replacing the 3D view, graphs, calibration or recording controls. `/api/state` includes all four smoothed probabilities, the model identifier, window length and measured inference duration.

## Verification

`tools/check_four_state.cpp` checks the actual C++ features, softmax and online detector against Python probabilities from one held-out window per class, within 1e-4 probability. It also checks all four LED colours, fault recovery, startup and stop. Existing two-class, recorder/calibration, dashboard and BLE-monitor tests remain applicable. Phone browser and physical breathing accuracy need hands-on verification.

On 9 November 2026 the firmware was uploaded and verified on the connected ESP32. A real BLE connection accepted START, emitted valid live class/RGB notifications and accepted STOP. The stationary board was classified as not attached. A separate development check measured **99.9 Hz** sensor sampling during active classification, approximately **41.8 ms** feature extraction/inference on its separate task, and no I2C read errors. Reference replay verifies the green/red code paths, but actual chest-mounted calm/fast breathing and physical LED colours still need a hands-on demonstration. BLE discovery uses the service UUID, and the name has its own scan-response payload to avoid advertisement-size truncation.

Codex implemented and ran the training/export workflow, expanded firmware/display support and prepared the regression tests. Both group members should understand the features, split, model, latency and limitations.
