# Lab 8: Wireless Image Capture and Transmission

Completed KestrelSAT firmware and course references for verified SD-card file
transfers, Arducam Mega JPEG capture and periodic preview over XBee, paired with
[KestrelSAT GCS](https://github.com/chasedustevichusafa/KestrelSAT-GCS).

**Project completion documentation: November 4, 2026 (America/Denver).**
The owner confirmed the working GCS/firmware integration, file management, camera,
speed improvements, preview and RSSI tracking. Use the merged `main` versions of
both repositories. Live Preview and Link Tracker are GCS features using this
firmware's existing camera/file/HELLO operations; they do not require a separate
preview firmware or a satellite RSSI-streaming mode.

## Choose the correct project

| Project | Role |
| --- | --- |
| [Lab 8](Astro-331-2026-Fall/Astro331_Lab8_Code/README.md) | Deployed PlatformIO firmware: KFS1 SD browsing, verified upload/download, rename/delete, capture/recovery and negotiated speed profiles. |
| [Lab 4](Astro-331-2026-Fall/Astro331_Lab4_Code) | Original telemetry/XBee/SD reference; not the deployed GCS file service. |
| [Lab 5](Astro-331-2026-Fall/Astro331_Lab5_Code) | Original Arducam Mega reference; not the deployed GCS file service. |

Labs 4/5 source and vendor libraries are retained as references. The Lab 8 source
package includes its own required Arducam library and license; a Lab 5 checkout
is not needed to build it. Detailed application comments and the
[source reading order](Astro-331-2026-Fall/Astro331_Lab8_Code/README.md#source-reading-order)
explain how the final firmware works.

## Download, configure and upload

1. Sign into GitHub and open [Actions → Download Lab 8 firmware](https://github.com/chasedustevichusafa/Lab-8-Wireless-Image-Capture-and-Transmission/actions/workflows/lab8-firmware.yml).
   Choose a successful **main** run containing the desired commit and download
   **KestrelSAT-Lab8-Firmware** from **Artifacts**.
2. Extract the ZIP and open its **KestrelSAT-Lab8** folder in PlatformIO; that
   folder directly contains `platformio.ini`. The small package avoids legacy
   whole-repository Windows path issues. It contains **source**, not a prebuilt
   flash image or an automatic radio programmer.
3. For the confirmed camera wiring choose **Project Tasks →
   esp32thing_plus_115200_camera_cs27 → Upload**. Do not use the default environment
   with a GPIO27 camera: the default is still GPIO12/9600.
4. Write each radio's UART/AP/address settings in XCTU as shown below; retain
   the working network/security settings. Close XCTU and any Serial Monitor that
   owns the port before opening the GCS.
5. Download/extract the GCS executable from a successful main
   [Windows test build](https://github.com/chasedustevichusafa/KestrelSAT-GCS/actions/workflows/build-windows.yml).
   Connect to the ground radio at **115200, 8N1, XBee API (AP=1)**. Use
   **Remote Files → Refresh / Open**, then **Camera → Refresh images**.

| Firmware environment | XBee UART baud | Camera CS | Use |
| --- | ---: | --- | --- |
| `esp32thing_plus_115200_camera_cs27` | 115200 | GPIO27 | Confirmed operating configuration |
| `benchmark_9600_cs27` | 9600 | GPIO27 | Slow-baud comparison with the same camera wire |
| `esp32thing_plus_115200` | 115200 | GPIO12 | Original camera-CS wiring at faster UART baud |
| `esp32thing_plus` (default) | 9600 | GPIO12 | Original lab defaults |

All environments retain board `sparkfun_esp32s2_thing_plus_c`, Arduino framework,
25 MHz SD SPI and a **115200-baud USB monitor**. The USB baud is independent of
the selected radio UART baud. Firmware selection does not reprogram an XBee.

## Confirmed radio and camera setup

- Satellite XBee: **115200**, **AP=0** (transparent).
- Ground XBee: **115200**, **AP=1**; select the same baud/API mode in the GCS.
- Camera CS: **GPIO27**, with the matching firmware environment.
- Unicast: **ground DH/DL = satellite SH/SL**, **satellite DH/DL = ground SH/SL**.
  Copy the peer's hexadecimal serial address exactly; retain MAC acknowledgments.
- Retain the lab's working supply, common ground, network and security settings.

AP=0 passes serial bytes. AP=1 adds structured frames on the local computer/radio
UART for addressing, receive metadata, transmit status and AT queries; the ground
radio removes/adds that framing, so the satellite can remain transparent.
AP mode is separate from destination addressing and does not change RF bitrate.
The supplied lab profile's **DH=0/DL=FFFF is broadcast**, not the confirmed unicast
transfer setup. Legacy AO receive indicators are supported by the current GCS.

See [wiring, flashing recovery and protocol details](Astro-331-2026-Fall/Astro331_Lab8_Code/README.md).
Disconnect power before changing wiring. Earlier camera-attached flashing and
JPEG framing issues were addressed with the confirmed GPIO27 setup and the
bounded JPEG extraction implementation; follow the detailed guide if they recur.

## Completed GCS workflows

- **Remote Files:** SD browser; verified uploads/downloads; cancellation; protected
  rename/delete; reserved partial-upload cleanup. Interrupted transfers restart.
- **Camera:** supported-resolution selection, JPEG capture to SD, View/Download,
  Rename/Delete and pending-job recovery. Names contain **GCS local request time**,
  UTC offset, resolution and a unique job suffix, rather than satellite RTC time.
- **Live Preview:** on-demand 5/10-second capture/download targets, automatic
  removal of successfully displayed preview-only SD frames, Save frame and a
  resizable native-pixel pop-out. Refresh targets depend on JPEG size and speed.
- **Transfer Stats:** live rate/RSSI, multi-operation recordings, CSV/summary/event
  exports, white/black graphs and an optional right-axis RSSI overlay.
- **Link Tracker:** dedicated RSSI, round-trip success/latency and distance
  checkpoints; CSV bundles reopen offline to rebuild/export graphs. For 16-bit
  receive frames, the expected source is the satellite's **MY**, possibly `0000`,
  not its 64-bit SH/SL. This is a GCS receive-source check, not a firmware change.

Full operation and analysis instructions are in the
[GCS README](https://github.com/chasedustevichusafa/KestrelSAT-GCS/blob/main/README.md).

## Speed profiles and results

Baseline uses 128-byte hex blocks; Larger blocks uses 256-byte hex; Compact uses
256-byte Base64; Fast uses 512-byte Base64; Experimental uses the same 512-byte
Base64 with **two requests per batch**. Auto negotiates Fast with FAST1 firmware
and falls back to Baseline with older firmware. CRC32 checks, SD sync/read-back,
request replay and overwrite protection remain in place.

The owner reported approximately **1500 B/s Baseline**, **3850 B/s Auto/Fast**,
and **4700 B/s Experimental** on the working setup. These are observed useful
download rates, not guaranteed rates, upload measurements or an RF bitrate.
Compare the same saved file and placement; retain failures and retries as well
as average rate. Use `benchmark_9600_cs27` to compare the old UART rate without
moving the camera CS wire; match both radio UARTs and GCS baud, then restore the
115200 environment/settings afterwards.

## Artifacts, older versions and validation

The source workflow validates required files and Windows ZIP extraction/checksums
and retains firmware artifacts for **30 days**. It does **not** compile or flash
firmware. Main/project pull requests and relevant main pushes run packaging;
**Run workflow** can rebuild a selected available ref. GCS test artifacts have a
separate **14-day** retention. Old unexpired runs remain downloadable; after
artifact expiry, use Git source history and a compatible build environment.
Record both repositories' commit IDs and radio profiles for reproducibility.

The [Lab 8 README](Astro-331-2026-Fall/Astro331_Lab8_Code/README.md#automated-compatibility-tests)
provides all four compilation commands and the C++/Python protocol test command.
Compilation, host fault injection and owner-reported operation are distinct
forms of evidence. The completed project does not establish a guaranteed radio
range or failure-free behavior for every SD card, supply, scene or antenna setup.

## Completion record — 2026-11-04

Documented all merged features, the confirmed GPIO27/115200/API/unicast setup,
reported speed tiers, preview and source-verified RSSI/offline graph workflow.
Removed obsolete branch-only installation and superseded pending-feature text.
Updated the source package's own README so downloaded firmware remains usable
without this repository overview. Historical Lab 4/5 instructions now identify
their reference role and point to the final service.
