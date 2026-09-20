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
