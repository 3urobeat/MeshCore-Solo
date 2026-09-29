# Releasing Solo firmware

1. Add the notes for the new version at the top of `release-notes.md`.
2. Push a `v*` tag (e.g. `v1.29`). [Build Solo Firmwares](./.github/workflows/build-solo-firmwares.yml)
   builds every `*_solo_dual` and `*_solo_lvgl` environment in `solo/` with
   `FIRMWARE_VERSION` set to the tag and opens a **draft** release with:
   - `.uf2` + `-ota.zip` (DFU package) for nRF52 boards;
   - `-merged.bin` (flash at `0x0`) for ESP32 boards;
   - `-ota.bin` (app image) for the `*_solo_lvgl` boards — what their
     Settings › System › Firmware update downloads.
3. Paste the notes into the draft and publish it. Only a published,
   non-prerelease release is what devices and the website see as the latest:
   the on-device update reads `releases/latest`, and the website's simulator
   is refreshed from its `solo-sim-wasm.zip` ([Build Solo Sim](./.github/workflows/build-solo-sim.yml)).

The upstream `companion-v*`, `repeater-v*` and `room-server-v*` tags still
build the stock firmwares through their own workflows.
