# HiJack BLE movement and breathing monitor

Live page: **https://adajirik.github.io/HiJack_breathing/**

1. Power the ESP32 with the movement/BLE firmware. Keep its sensor connected. No computer data connection is needed for inference.
2. Open the HTTPS page in **Chrome on Android**. On **iPhone**, install [Bluefy](https://apps.apple.com/app/id1492822055) and open the same URL inside Bluefy. Safari and iPhone Chrome do not expose Web Bluetooth. See [Bluefy's official site](https://bluefy.app/).
3. Enable Bluetooth, press **Connect ESP32**, and choose **HiJack-Detect** in the browser's device chooser. No prior pairing in phone settings is required.
4. Press **Start detection** and allow four seconds for the first window. Blue means `not_attached`, magenta `shaking`, green `calm_breathing` and red `fast_breathing`. For breathing, keep the sensor steadily against the chest as during recording. Classifications update every 500 ms; smoothing and the four-second window add transition delay. The screen uses the RGB state sent by the same firmware function that sets the onboard LED.
5. **Stop** turns off classification and the RGB LED. **Disconnect** closes the phone connection. Detection continues autonomously on the board if the connection drops; reconnect to see it and press Stop if desired.

Keep the page in the foreground. The connection badge and history show interruptions; after 1.5 seconds without fresh notifications, the screen becomes neutral instead of showing an old classification. History is bounded to the latest 20 changes in memory and clears when the page reloads. No analytics, server upload or external JavaScript library is used.

The monitor is independent of the existing Wi-Fi dashboard at http://192.168.4.1/. You do not need to join the board's Wi-Fi to use BLE. The plain HTTP board page cannot use Web Bluetooth; the monitor must be loaded from HTTPS (or localhost for development). A Wi-Fi dashboard CSV download may briefly delay LED/notification updates while the HTTP transfer completes.

## Deployment

The workflow `.github/workflows/monitor-pages.yml` publishes only this directory through GitHub Pages. It runs when monitor files change on main or when manually dispatched. Repository visibility was changed to public with the user's explicit authorization because the account's plan rejected Pages for the private repository. The repository, including already committed recordings, is consequently public.

## BLE protocol, version 1

- Advertised name: `HiJack-Detect`.
- Service: `e40d1000-7c60-4c1a-8bbc-42a4a91131c0`.
- State (read + notify): `e40d1001-7c60-4c1a-8bbc-42a4a91131c0`.
- Control (write with response): `e40d1002-7c60-4c1a-8bbc-42a4a91131c0`.
- Control commands: UTF-8 `START` and `STOP`.

Notifications are exactly 20 bytes, fitting the default ATT payload, sent when the state changes and as a 200 ms heartbeat. Multibyte integers are little endian:

| Offset | Meaning |
| --- | --- |
| 0 | Protocol version = 1 |
| 1 | State: 0 stopped, 1 warming, 2 not_attached, 3 shaking, 4 sensor fault, 5 calm breathing, 6 fast breathing |
| 2–4 | RGB colour, each 0–255; blue `[0,0,255]`, magenta `[255,0,255]`, green `[0,255,0]`, red `[255,0,0]` |
| 5 | Smoothed shaking model output × 255; not calibrated clinical confidence |
| 6 | Detection enabled (0/1) |
| 7 | No current classification sensor fault (0/1) |
| 8–11 | Notification sequence number |
| 12–15 | Device uptime in milliseconds |
| 16–19 | Most recent feature extraction + inference duration in microseconds |

Before building the page, BLE can also be inspected with nRF Connect for Mobile as recommended in the assignment: connect, subscribe to state and write `START` to control. Decode notifications as bytes, not UTF-8 text.

## Checks

`node tools/check_ble_monitor.cjs` verifies the page's real script against protocol fixtures: exact colours, control writes, history, duplicate/reordered packets, stale data and disconnect handling. `tools/inspect_ble.py` can test the actual board over BLE using the optional Python `bleak==2.0.0` package. Android/iPhone browser behavior and physical LED colour require hands-on validation on those devices.

Initial two-class hardware verification on 9 November 2026: the uploaded board advertised `HiJack-Detect`, accepted a real BLE connection and START command, sent 21 notifications during the test with valid blue `[0,0,255]` classification, and accepted STOP with an off `[0,0,0]` state. That earlier model measured 256 microseconds in the observed packet. The current four-state model's timing and hardware checks are documented in [the four-state report](../training/FOUR_STATE.md). The existing sensor stream remained fresh with zero I2C errors. The sampler task has an 8 kB stack; this resolved a stack-overflow reset found during initial combined Wi-Fi/BLE testing. Physical breathing transitions and phone browsers still need hands-on checks.

The four-state model is now implemented; see [training/evaluation](../training/FOUR_STATE.md). The earlier two-state milestone and its measured results are preserved in the repository. Breathing duration alarms and the full bedside-monitor grading requirements remain pending. Refresh the monitor page after updating firmware so it recognizes the added breathing state IDs.
