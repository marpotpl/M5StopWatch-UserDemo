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
option. After adding this option, flash the new bootloader and application once
over USB; an application-only OTA cannot upgrade the installed bootloader.
Keep the partition table unchanged so NVS and FAT storage retain their offsets.

`ha_ota` logs the running and next OTA partitions. Only an OTA image in
`PENDING_VERIFY` starts its 90-second self-test. The test waits for completed
HAL/app setup, ten seconds of a responsive main UI loop, and five seconds of
connected Wi-Fi. It then marks the image `VALID`. A failed or timed-out test
requests rollback to the prior slot. Home Assistant availability is not a
requirement. Normal boots do not wait for the self-test.

The module exposes `ha_ota::start_update(url, ca_cert_pem)` for later use by a
trusted local trigger. It currently has no UI or automatic trigger. The URL
must use HTTPS and a private IPv4 address (10/8, 172.16/12, or 192.168/16).
The default verifies the server with the ESP-IDF certificate bundle; for a
Mac server using a private CA, the caller supplies that CA certificate and the
server certificate needs the LAN IP in its subject alternative names. The download runs
in a separate task, writes only to the inactive slot, reports status and
progress through `ha_ota::status()`, and restarts only after image validation.
The caller must limit the server and trigger to the trusted local network.
The firmware binary currently includes the compiled HA token: never serve it
over plain HTTP or expose it publicly. Do not put certificates' private keys,
OTA credentials, or other secrets in tracked files.

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
