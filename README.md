# Lab 8: Wireless Image Capture and Transmission

KestrelSAT firmware and course reference material for capturing still images
and transferring files over XBee. Live image preview remains future work.

The current working milestone is **remote JPEG capture, SD-card browsing and verified file uploads/downloads**
using the KestrelSAT GCS Camera and Remote Files tabs.
The project owner confirmed that the GCS and Lab 8 firmware work together on the
KestrelSAT/XBee hardware on **October 20, 2026**.

## Projects

| Project | Purpose |
| --- | --- |
| [Lab 8 firmware](Astro-331-2026-Fall/Astro331_Lab8_Code/README.md) | Current SD browser and verified upload/download service; upload this project for the GCS Remote Files tab. |
| [Lab 4](Astro-331-2026-Fall/Astro331_Lab4_Code) | Original communications and SD-card lab reference. |
| [Lab 5](Astro-331-2026-Fall/Astro331_Lab5_Code) | Original Arducam Mega capture lab reference. |

## Download and run

1. Open this repository's **Actions → Download Lab 8 firmware** and select its
   latest successful run on `main`, or `feature/cs27-baud-benchmark` for the new
   9600/GPIO27 environment before merge. Sign into GitHub and download **KestrelSAT-Lab8-Firmware**
   under **Artifacts**. This small package avoids the full repository's legacy
   files and long Windows paths.
2. Extract the ZIP and open **KestrelSAT-Lab8** in PlatformIO. The folder contains
   `platformio.ini`. For the confirmed camera setup with camera CS on GPIO27,
   build/upload `esp32thing_plus_115200_camera_cs27`. Original lab defaults remain
   available as `esp32thing_plus` (GPIO12/9600); match firmware to physical wiring.
3. Run the updated GCS executable, connect to the ground XBee COM port at
   **115200 baud, 8N1**, ground AP=1/GCS API mode and satellite AP=0, then choose
   **Camera → Refresh images** or **Remote Files → Refresh / Open**.
4. Browse folders, select a file, choose **Download...**, and wait for
   **Verified and saved**.

Keep the working board configuration, wiring, radio pairing, and SD card from the
labs. The file service supports progress, cancellation, retries, and CRC32
verification. Uploads use temporary files and preserve existing names; both directions restart after interruption. The Camera tab adds single-image capture; live streaming remains future work. See the
[Lab 8 README](Astro-331-2026-Fall/Astro331_Lab8_Code/README.md) for wiring, protocol,
limits, build commands, and tests.

## Optional faster UART link — 2026-10-21

**2026-10-22 GPIO27 benchmark:** The firmware source package on
`feature/cs27-baud-benchmark` (and `main` once merged) now includes
`benchmark_9600_cs27` for **9600 baud with camera CS on GPIO27**. Use
`esp32thing_plus_115200_camera_cs27` for the matching 115200 comparison. Set both
radio UARTs and the GCS to the selected rate; USB monitoring remains 115200.
See [the build/upload and graph-recording procedure](Astro-331-2026-Fall/Astro331_Lab8_Code/README.md#gpio27-baud-comparison--2026-10-22).

For the 115200-baud test, select the `esp32thing_plus_115200` firmware environment,
configure both radios' UART baud settings in XCTU, and select 115200 in the GCS.
The normal firmware environment remains at 9600. See the
[setup and rollback instructions](Astro-331-2026-Fall/Astro331_Lab8_Code/README.md#optional-115200-baud-test).
This changes UART speed, not the radio's RF bitrate; verify transfer contents
and compare measured rates on the hardware.

## Remote camera capture — 2026-10-21

**2026-10-22 hardware confirmation:** The owner confirmed camera capture and
GPIO27 operation and reported a working stopping point. Use
`esp32thing_plus_115200_camera_cs27` and unicast destinations on both radios,
with RSSI polling enabled if desired. Two same-image unicast downloads completed
in 22.407 s and 22.411 s, polling off/on respectively, with zero protocol
timeouts/retries. See the [confirmed configuration and measured limits](Astro-331-2026-Fall/Astro331_Lab8_Code/README.md#confirmed-camera-and-radio-setup--2026-10-22).

**2026-10-22 follow-up:** Camera-2 corrects FIFO/JPEG framing and uses the vendor
SD-example read method, retries failed detection through Refresh, and adds byte
count/boundary diagnostics. An optional GPIO27 CS environment supports testing
the GPIO12 boot-strap explanation for camera-connected flash MD5 errors. See
[the upload/wiring and capture-retest procedure](Astro-331-2026-Fall/Astro331_Lab8_Code/README.md#camera-uploaddetection-failures--2026-10-22).

Use matching GCS and firmware downloads from `main` after merge, or
`feature/remote-camera` while testing. Upload `esp32thing_plus_115200_camera_cs27`
for the confirmed GPIO27 setup, keeping the working baud/AP/unicast settings.
The new GCS **Camera** page selects supported resolutions, captures a JPEG to SD,
and then provides View, Download, Rename and Delete. Filenames use the GCS local
request time, UTC offset, resolution and a unique suffix. View downloads a saved
file; no live preview is started. Camera operation has been confirmed by the owner;
the measured transfer pair is not a long-duration reliability study.

See [Lab 8 camera setup](Astro-331-2026-Fall/Astro331_Lab8_Code/README.md#camera-capture--2026-10-21).

## Dated changes

- **2026-10-22 — Hardware result and handoff:** Recorded confirmed camera/GPIO27
  operation, unicast setup and same-image RSSI off/on results. Documented the
  working 115200/API setup and retained the original lab environments. The source
  artifact workflow now checks pull requests as well as pushes to main/feature
  branches and validates all packaged source checksums.

- **2026-10-21 — Camera capture:** Integrated the Lab 5 Arducam Mega driver with
  bounded control waits, cooperative capture/save/read-back, unique local-time
  filenames and retained job status. Packaging includes the standalone camera
  library and license. Both baud variants compile; physical camera testing is pending.

- **2026-10-21 — File management:** Added MANAGE1 capability, file-only DELETE
  and same-folder RENAME, no-overwrite checks and retained results for lost replies.
  Startup marker is `KFS1 manage-1`. All 24 host protocol tests and both baud-rate
  builds passed; delete/rename hardware testing remains pending.
- **2026-10-21 — Upload hardware result:** The project owner confirmed that the
  matching GCS and upload firmware work together on hardware.

- **2026-10-21 — Verified upload feature:** Added UPLOAD1 capability, bounded
  chunk writes, SD read-back verification, no-replace publication, lost-completion
  recovery and reserved partial-file cleanup. The project owner subsequently confirmed uploads work.
- **2026-10-21 — Stability build hardware result:** The project owner confirmed
  the SD-read recovery build worked on hardware; a measured twenty-transfer
  acceptance result and physical failure source were not established.
- **2026-10-21 — Transfer stability diagnostic build:** On
  `fix/file-transfer-stability`, added bounded SD read retries at the same byte
  offset, detailed USB error output and `SDCHECK` local read/CRC passes. Automated
  fault injection reproduces the old failure and verifies recovery; the physical
  failure source and hardware reliability target remain under investigation.
- **2026-10-21 — 115200 transparent link confirmed:** The project owner reported
  successful connection and Remote Files operation with the faster firmware and
  both radios in transparent mode. The ground radio's legacy API receive format
  required a GCS compatibility fix; physical API/RSSI testing remains pending.
- **2026-10-20 — Hardware integration confirmed:** The project owner reported
  successful operation of the Windows GCS and Lab 8 firmware together. Added
  this repository overview and recorded the integration status in the project
  documentation.
- **2026-10-15 — File-transfer milestone implemented:** Added the combined Lab 8
  PlatformIO project, KFS1 SD browser/download protocol, and cross-language
  tests. Added a Windows-verified firmware packaging workflow with a small
  downloadable project ZIP. Labs 4 and 5 remain available as references.
