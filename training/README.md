# Two-class movement model

Train and reproduce from the repository root:

```powershell
.\.venv\Scripts\python.exe -m pip install -r training/requirements.txt
.\.venv\Scripts\python.exe training/train_movement.py
```

Only `Data/*_not_attached.csv` and `Data/*_shaking.csv` are used: 10 independent ten-second recordings per class, about 20,000 samples total. Calm/fast breathing recordings are excluded from this milestone. Labels come from filenames. Raw acceleration is in m/s² and raw angular velocity in degrees/s, without the dashboard's orientation calibration or gyro-bias removal.

Each model window has 100 samples at the 100 Hz target rate (one second), sliding by 25 samples (250 ms). Features are `log1p` of combined XYZ acceleration standard deviation, combined XYZ gyro standard deviation, acceleration-magnitude standard deviation, acceleration step RMS, gyro step RMS and gyro RMS. These remove dependence on a particular mounting axis while retaining amplitude and speed information. The same six features and training-only normalization are exported to dependency-free C++.

## Selection and results

The deterministic split in `models/movement_split.json` is made by complete recording before windowing: 12 train, 4 validation and 4 test files. Identical session IDs or duplicate file hashes are rejected. Initial validation results are saved. Model selection uses four-fold stratified recording-group cross-validation on the 16 development recordings, with the scaler fitted separately within every fold. Test recordings are excluded from model-selection fits. The script compares logistic regression, a small 16-unit MLP and a learned decision stump. Logistic regression wins the grouped development comparison and is refitted on those 16 files; the four test files remain excluded from deployed weights.

Held-out results: **75% accuracy**, **0.733 macro-F1**, on 148 overlapping windows from four held-out recordings. The confusion matrix has true classes in rows and predicted classes in columns, ordered `not_attached`, `shaking`:

| Actual / predicted | Not attached | Shaking |
| --- | ---: | ---: |
| Not attached | 74 | 0 |
| Shaking | 37 | 37 |

One held-out shaking recording, `data_15_shaking.csv`, is missed completely. The test set is small, its windows overlap, and person identities are not documented, so this is not evidence of cross-person or general-purpose activity recognition. The initial MLP was better on the single validation split but less reliable across grouped development folds. The full reports and per-file results are saved in `models/` rather than selecting the model on test accuracy.

The quality report also finds 2,565 samples at or near the ±2 g / ±250 degrees/s sensor limits, predominantly in shaking files. The firmware keeps these ranges to match training. Increasing ranges would require recording a matching new dataset. Improve the model with additional independent shaking styles, especially examples resembling data_15, and a fresh held-out set; avoid merely adding overlapping windows from the same recording. Breathing is not recognized by this two-class model.

## ESP32 execution

`firmware/include/movement_model.h` contains the exported normalization and model coefficients. No Python or laptop runs during detection. Features/inference execute in the sensor task, using raw samples. The first result arrives after one second; subsequent results arrive every 250 ms. An exponential average (weight 0.5) and 0.4/0.6 switching hysteresis reduce flicker, adding transition latency. Sensor faults clear the window and report an error until one second of fresh samples is available.

`tools/check_movement.cpp` compares C++ inference with the Python model on actual held-out reference windows to within 3e-5 probability, including feature calculation. It also checks start/stop/fault state and LED colour mapping. Build it with a C++17 compiler and the `firmware/include` include directory, run it from the repository root, then run the existing recorder/dashboard checks.

The real ESP32 BLE test measured 256 microseconds for feature extraction plus inference on one observed live packet. This is an observed result, not a worst-case execution bound; assess timing under the final combined workload before the full four-state demonstration.

The onboard RGB pin is `MOTION_RGB_PIN=48`, matching the supplied board's apparent variant. A newer board with RGB on GPIO38 needs `-D MOTION_RGB_PIN=38` in PlatformIO build flags. Pin 38 is never driven speculatively alongside 48.

Codex prepared and ran the training, generated the C++ model and added BLE control/monitoring. Both group members should understand the recording split, features, inference and measured limitations.
