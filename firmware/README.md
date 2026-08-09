# ESP32 enclosure firmware

Board: **ESP32 DevKit V1** (ESP32-WROOM-32). Pin map below is for that board.

## What it does

| Job | Detail |
|---|---|
| SoftAP `SNTC-Enclosure` | Serves a captive portal showing a 6-char proximity code |
| STA on campus WiFi | Uplink for MQTT — the AP itself has **no** internet route |
| Publishes the code | `device/{uuid}/access/proximity_code`, rotated every 60 s |
| Obeys commands | `unlock_door`, `dispense`, `unlock` — with nonce replay protection |
| Reports | door events, tamper, telemetry, heartbeat every 30 s |

The AP is deliberately a dead end. Members read the code, leave the AP, and
submit it from a real network. Full rationale in `docs/USER_GUIDE.md` §2.

## Flash it

1. **Arduino IDE** → Preferences → Additional Board URLs:
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
   Then Boards Manager → install **esp32** by Espressif.

2. Library Manager → install:
   - `PubSubClient` (Nick O'Leary)
   - `ArduinoJson` (Benoit Blanchon, v7+)

3. Edit `config.h`:
   ```c
   #define STA_SSID      "your-campus-ssid"
   #define STA_PASSWORD  "your-campus-password"
   #define MQTT_HOST     "192.168.1.50"   // machine running docker-compose
   ```
   `DEVICE_UUID` already matches the seeded demo device — leave it alone unless
   you created your own device in Admin → Devices.

4. Board: **DOIT ESP32 DEVKIT V1**. Select the port, Upload.

Or skip the IDE entirely — `arduino-cli` is scriptable:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 kms_enclosure
arduino-cli upload  --fqbn esp32:esp32:esp32 -p /dev/ttyUSB0 kms_enclosure
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

Serial access needs your user in `dialout`: `sudo usermod -aG dialout $USER`,
then log out and back in.

5. Serial Monitor at **115200**. Expected:
   ```
   [ap] SNTC-Enclosure at 4.3.2.1
   [sta] up: 192.168.1.77
   [code] initial 4F2A91
   [mqtt] connected
   [code] 4F2A91 -> device/1111.../access/proximity_code
   ```

## Wiring

**Never use GPIO 6–11 on a WROOM-32 module** — they are wired to the internal
SPI flash, and driving them crashes the chip into a boot loop.

| Signal | GPIO | Notes |
|---|---|---|
| Door lock driver | 4 | HIGH = unlocked. Use a MOSFET/relay, not the pin directly |
| Tamper switch | 5 | NC to GND; opens (reads HIGH) when the enclosure is prised |
| Slots 1–8 | 13,14,16,17,18,19,21,22 | `PIN_SLOTS[]` in the `.ino`, HIGH for 1.2 s to actuate |

Change the `PIN_*` constants at the top of the `.ino` if your board differs.

## Testing without hardware

The whole proximity path can be exercised from a laptop — publish a code by
hand and it lands in Redis exactly as the ESP32's would:

```bash
docker compose up -d mosquitto redis postgres

mosquitto_pub -h localhost -t 'device/11111111-2222-3333-4444-555555555555/access/proximity_code' \
              -m '{"code":"TEST42"}'
```

Then enter `TEST42` at `/connect` in the portal. Watch commands go the other
way with:

```bash
mosquitto_sub -h localhost -t 'device/#' -v
```

## MQTT broker choice

- **Local dev** — `docker compose up mosquitto`, set `MQTT_HOST` to your
  machine's LAN IP (not `localhost`; the ESP32 resolves that to itself).
- **Production** — HiveMQ Cloud. Set `MQTT_USERNAME`/`MQTT_PASSWORD`, and note
  that TLS on port 8883 needs `WiFiClientSecure` instead of `WiFiClient`.

## Known limits

- `openDoor()` blocks for the unlock TTL. Fine for one door; add a timer if you
  ever actuate concurrently.
- Battery telemetry is hardcoded to 100%. Wire a fuel gauge and replace it.
- No OTA. Re-flash over USB.
