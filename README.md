# Lab 8: Wireless Image Capture and Transmission

KestrelSAT firmware and course reference material for transferring files and,
in a future milestone, capturing and previewing images over XBee.

The current working milestone is **remote SD-card browsing and file downloads**
using the [KestrelSAT GCS Remote Files tab](https://github.com/chasedustevichusafa/KestrelSAT-GCS/tree/feature/remote-files).
The project owner confirmed that the GCS and Lab 8 firmware work together on the
KestrelSAT/XBee hardware on **October 20, 2026**.

## Projects

| Project | Purpose |
| --- | --- |
| [Lab 8 firmware](Astro-331-2026-Fall/Astro331_Lab8_Code/README.md) | Current SD browser/download service; upload this project for the GCS Remote Files tab. |
| [Lab 4](Astro-331-2026-Fall/Astro331_Lab4_Code) | Original communications and SD-card lab reference. |
| [Lab 5](Astro-331-2026-Fall/Astro331_Lab5_Code) | Original Arducam Mega capture lab reference. |

## Download and run

1. Open this repository's **Actions → Download Lab 8 firmware** and select its
   latest successful run. Sign into GitHub and download **KestrelSAT-Lab8-Firmware**
   under **Artifacts**. This small package avoids the full repository's legacy
   files and long Windows paths.
2. Extract the ZIP and open **KestrelSAT-Lab8** in PlatformIO. The folder contains
   `platformio.ini`. Build and upload the `esp32thing_plus` environment.
3. Run the updated GCS executable, connect to the ground XBee COM port at
   **9600 baud, 8N1**, then choose **Remote Files → Refresh / Open**.
4. Browse folders, select a file, choose **Download...**, and wait for
   **Verified and saved**.

Keep the working board configuration, wiring, radio pairing, and SD card from the
labs. The file service supports progress, cancellation, retries, and CRC32
verification. Transfers are read-only and restart after interruption. Camera
capture and live image preview are future work. See the
[Lab 8 README](Astro-331-2026-Fall/Astro331_Lab8_Code/README.md) for wiring, protocol,
limits, build commands, and tests.

## Dated changes

- **2026-10-20 — Hardware integration confirmed:** The project owner reported
  successful operation of the Windows GCS and Lab 8 firmware together. Added
  this repository overview and recorded the integration status in the project
  documentation.
- **2026-10-15 — File-transfer milestone implemented:** Added the combined Lab 8
  PlatformIO project, KFS1 SD browser/download protocol, and cross-language
  tests. Added a Windows-verified firmware packaging workflow with a small
  downloadable project ZIP. Labs 4 and 5 remain available as references.
