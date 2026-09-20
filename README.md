# M5StopWatch-UserDemo
M5Stack StopWatch user demo for hardware evaluation.

## Build

### Fetch Dependencies

```bash
python3 ./fetch_repos.py
```

The project uses a local copy of `78/esp-wifi-connect` in
`vendor/78__esp-wifi-connect/`, based on upstream version 3.1.3. Its
`wifi_station.cc` runs `HandleScanResult()` in a separate `wifi_scan_result`
FreeRTOS task with an 8192-byte stack. Running it in the ESP-IDF `sys_evt`
callback caused a stack overflow. `main/idf_component.yml` selects this local
copy through `override_path`; do not edit the downloaded copy in
`managed_components/`.

The ESP-IDF Component Manager bundled with this toolchain records an absolute
path for the local component in `dependencies.lock`. After a fresh clone in a
different directory, run `idf.py update-dependencies` before the first build.
This lets Component Manager regenerate the lockfile for that checkout; do not
edit the lockfile by hand.

To update the component, compare the vendored copy with a newer upstream
release, carry forward the scan-task fix if upstream still needs it, update
the version in the vendored `idf_component.yml` and `main/idf_component.yml`,
then run `idf.py reconfigure` to regenerate `dependencies.lock` and
`idf.py build`. Verify Wi-Fi scanning on the device before adopting the update.

Create the local Wi-Fi configuration before building:

```bash
cp main/hal/ha_secrets.example.h main/hal/ha_secrets.h
```

Replace the example placeholders in `ha_secrets.h` with your own network
credentials. This file is ignored by Git; do not commit credentials.
Also define `HA_ACCESS_TOKEN` there with a Home Assistant Long-Lived Access
Token. The example header contains only a placeholder; never commit the real
token.

The first Home Assistant client stage connects to
`ws://192.168.0.73:8123/api/websocket` after Wi-Fi obtains an IP address,
authenticates with the token, and reconnects after transport failures. It does
not fetch entities, call services, or change the UI. `auth_invalid` stops
retries until the device restarts. This `ws://` endpoint sends the token without
TLS; use it only on the intended local network.

`components/tcp_transport/` overrides the ESP-IDF 5.5.4 component to fix
WebSocket frames buffered during the HTTP Upgrade. Its `transport_ws.c` is
copied from ESP-IDF 5.5.4 with only the buffered-read checks changed; the
other transport sources and headers still come from the installed ESP-IDF.
Without this fix, Home Assistant's `auth_required` frame can remain buffered
until another packet arrives, after its authentication timeout. When updating
ESP-IDF, compare this local file with the new upstream `transport_ws.c` and
remove the override if upstream includes the fix.

### OTA foundation

The existing `partitions.csv` already provides `otadata` and two 5056 KiB
application slots. `sdkconfig.defaults` enables the ESP-IDF bootloader rollback
option. The rollback bootloader and OTA client were installed over USB and the
first `ota_0` to `ota_1` update passed on hardware. Future application updates
use OTA; USB remains the recovery method. Keep the partition table unchanged
so NVS and FAT storage retain their offsets.

`ha_ota` logs the running and next OTA partitions. Only an OTA image in
`PENDING_VERIFY` starts its 90-second self-test. The test waits for completed
HAL/app setup, ten seconds of a responsive main UI loop, and five seconds of
connected Wi-Fi. It then marks the image `VALID`. A failed or timed-out test
requests rollback to the prior slot. Home Assistant availability is not a
requirement. Normal boots do not wait for the self-test.

### OTA trigger through Home Assistant

After `auth_ok`, `ha_client` subscribes to the
`m5stopwatch_ota_request` event and checks Home Assistant's subscription
result. It restores the subscription after each reconnect. An intentional
request can be sent with Home Assistant's `fire_event` WebSocket command:

```json
{
  "id": 42,
  "type": "fire_event",
  "event_type": "m5stopwatch_ota_request",
  "event_data": {
    "device_id": "m5stopwatch",
    "request_id": "unique-request-001",
    "url": "https://192.168.1.10:8443/StopWatch-UserDemo.bin",
    "sha256": "0000000000000000000000000000000000000000000000000000000000000000",
    "size": 123456
  }
}
```

Use the actual binary's SHA-256 and byte size. `request_id` must be 1–64
ASCII letters, digits, `-`, `_`, or `.`; accepted IDs are remembered in RAM
until reboot. After 32 accepted requests, further requests fail closed until
reboot. The URL must be HTTPS with a private IPv4 literal (10/8, 172.16/12,
or 192.168/16), without credentials, query, or fragment. No OTA HTTP endpoint
or automatic startup update is exposed by the device.

The WebSocket callback only queues frame data. The `ha_client` task validates
the event, then `ha_ota::start_update(url, sha256, size)` starts a separate
download task. It writes the inactive slot. The downloader checks a supplied
Content-Length, the final byte count, and SHA-256 of the written flash before
`esp_https_ota_finish()` validates the ESP image and selects the boot slot.
The existing PENDING_VERIFY self-test decides whether the new image becomes
VALID or rolls back. OTA status and progress are available through
`ha_ota::status()`.

### Local OTA HTTPS CA

The firmware trusts only the dedicated CA in the local, Git-ignored
`main/hal/ha_ota_ca.h`. The event cannot supply or change this CA. Without
the header, builds still work but OTA requests are refused. Before building
firmware intended to accept OTA requests, generate a dedicated development
CA and copy its **public** certificate into this header. The private key
stays on the Mac in the Git-ignored `local_ota/` directory. The header
format is shown in `main/hal/ha_ota_ca.example.h`.

Example development setup (replace `MAC_LAN_IP` with the Mac's current
private LAN IPv4):

```bash
mkdir -p local_ota
openssl req -x509 -newkey rsa:3072 -sha256 -nodes -days 730 \
  -keyout local_ota/ota-ca.key -out local_ota/ota-ca.crt \
  -subj "/CN=M5StopWatch Local OTA CA" \
  -addext "basicConstraints=critical,CA:TRUE" \
  -addext "keyUsage=critical,keyCertSign,cRLSign"
openssl req -newkey rsa:3072 -sha256 -nodes \
  -keyout local_ota/ota-server.key -out local_ota/ota-server.csr \
  -subj "/CN=M5StopWatch OTA Server"
printf 'subjectAltName=IP:MAC_LAN_IP\nextendedKeyUsage=serverAuth\n' > local_ota/server.ext
openssl x509 -req -in local_ota/ota-server.csr \
  -CA local_ota/ota-ca.crt -CAkey local_ota/ota-ca.key \
  -CAcreateserial -out local_ota/ota-server.crt -days 365 -sha256 \
  -extfile local_ota/server.ext
```

Put the contents of `local_ota/ota-ca.crt` in a C++ raw string assigned to
`HA_OTA_CA_PEM` in `main/hal/ha_ota_ca.h`; include only the public CA
certificate. On a fresh device, install the rollback bootloader and an
application containing this CA once over USB. If the CA changes later, the
running firmware must first receive the new CA through a trusted update path.
If only the Mac's IP changes, sign a new server certificate containing its new
IP with the same CA; the device does not need a new flash.

Serve only `build/StopWatch-UserDemo.bin` on the Mac's LAN interface over
HTTPS using the server certificate and key. Compute metadata with
`shasum -a 256 build/StopWatch-UserDemo.bin` and
`stat -f %z build/StopWatch-UserDemo.bin` before sending the event.
The binary contains the compiled HA token: never expose it publicly.
The current HA control WebSocket is still `ws://` and the event has no HMAC;
use this only on a trusted local network. A future HMAC/WSS layer can be
added before `start_update()` without changing the download API.

### OTA workflow

1. Build with `idf.py build`. Confirm that the image fits the inactive OTA
   slot and that the intended public CA is compiled into the firmware.
   Calculate the exact metadata:

   ```bash
   shasum -a 256 build/StopWatch-UserDemo.bin
   stat -f %z build/StopWatch-UserDemo.bin
   ```

2. Start the local, Git-ignored `local_ota/M5StopWatch OTA Server.app`.
   It binds only to the Mac's LAN IPv4 on port 8443 and serves only
   `/StopWatch-UserDemo.bin` from the current build; other paths return 404.
   Check `local_ota/server.log` for its READY line. In ESET Cyber Security,
   allow **incoming connections** for `M5StopWatch OTA Server.app` on the
   trusted local network. Keep the private CA and server keys in
   `local_ota/`, never in Git.

3. Verify HTTPS before firing the event: use `curl --cacert local_ota/ota-ca.crt`
   to check the server's `Content-Length`, download
   the image, and compare its SHA-256 with the local build. The server
   certificate must have the Mac's current LAN IPv4 in its SAN. Do not use
   `--insecure` for this check.

4. After the StopWatch logs `OTA event subscription active`, fire a single
   `m5stopwatch_ota_request` event in Home Assistant with a new
   `request_id`, the HTTPS URL, the exact size, and SHA-256. Use an
   authenticated Home Assistant WebSocket `fire_event` command (shown
   above) or `POST /api/events/m5stopwatch_ota_request` with the same
   `event_data`. Keep the HA token in the existing local secrets file.

5. Monitor the device: event accepted → download to the inactive slot →
   Content-Length, final byte count, SHA-256 and ESP image validation →
   boot partition change → restart into `PENDING_VERIFY` → bounded HAL,
   UI-loop and Wi-Fi self-test → `VALID`. Then check Watch Face, Wi-Fi,
   NTP/RTC, HA authentication and restored OTA subscription, and observe
   stability for at least 60 seconds. Only a failed self-test requests
   rollback; HA being unavailable alone does not.

The first hardware OTA followed this sequence and a later controlled restart
still booted the new slot as `VALID`. A deliberate rollback failure test has
not been performed. Do not erase the entire flash during USB recovery;
preserve NVS and storage.

### Tool Chains

[ESP-IDF v5.5.4](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/index.html)

### Build

```bash
idf.py build
```

### Flash

```bash
idf.py flash
```
