# Lab 8: remote JPEG capture and verified file transfers

Upload this project to use the GCS **Remote Files** tab. Labs 4 and 5 are left
unchanged. This build supports single JPEG capture to SD and verified file
uploads/downloads. Viewing is an on-demand download; no live preview is started.

## Dated changes

- **2026-10-23 — Code walkthrough:** Added detailed comments to the application
  sources and environment definitions, explaining KFS framing/replay, SD retries,
  upload publication, camera states, JPEG framing, SPI ownership and deadlines.
  Executable code and build settings are unchanged; no speed experiment is
  implemented. The source-artifact workflow builds this branch and retains main.

## Source reading order

1. `src/main.cpp`: board/UART/SPI setup, bounded command collection, SdFat backend,
   USB diagnostics and the cooperative main loop. USB and radio are separate
   channels; radio configuration is still done in XCTU.
2. `include/file_service.h`: KFS1 framing/CRC, field validation, request replay,
   one active transfer, upload/download states and retained mutation results.
   Radio retries and local SD attempts are different layers. File chunks remain
   128 bytes with hex encoding and one request/reply at a time.
3. `include/camera_job.h`: capture, bounded FIFO extraction, SD read-back and
   publication. Job identity avoids duplicate exposures after uncertain replies.
4. `include/jpeg_stream.h`: marker/segment parsing across FIFO blocks, including
   padding and embedded thumbnails. It recognizes framing rather than decoding
   image pixels; the GCS viewer performs decoding after download.
5. `include/camera_hardware.h`: bounded vendor callbacks, SPI transactions and
   temporary-file ownership. The copied Lab 5 vendor library remains unchanged.
6. `platformio.ini`: the existing baud/CS environments. Selecting an environment
   does not program either radio's AP, BD or destination settings.
7. `test/test_protocol.py`: host fault injection and GCS interoperability checks.
   Compilation and simulation do not establish hardware reliability.

## Earlier dated changes

- **2026-10-22 — GPIO27 slow-baud benchmark:** Added `benchmark_9600_cs27`
  for comparing 9600 with the confirmed 115200 setup while retaining camera
  CS=GPIO27, the existing board and SD clock. Source artifacts include this
  environment. Both radio UARTs and the GCS must match the selected baud rate;
  the USB monitor remains at 115200.

- **2026-10-22 — Hardware confirmed:** The owner confirmed camera capture and
  GPIO27 operation and reported no remaining issues at the working stopping
  point. Same-image unicast downloads took 22.407 s with RSSI off and 22.411 s
  with it on, with zero protocol timeouts/retries. The confirmed setup and limits
  are recorded below. Pull requests now run source artifact packaging checks;
  automatic main builds are preserved.

- **2026-10-22 — Camera capture recovery:** Followed the vendor capture2SD
  read/framing approach instead of requiring FIFO bytes to equal JPEG boundaries.
  The streaming framer handles padding, split markers and embedded thumbnail
  markers, while retaining full SD CRC read-back. Refresh images can retry failed
  initialization without reconnecting. Optional CAMDIAG1 metadata reports FIFO/
  read/saved counts and boundary samples. Added an opt-in GPIO27 camera-CS
  environment to investigate GPIO12 boot/flash interference. Startup marker is
  `KFS1 camera-2`. Physical retesting remains required; see the procedure below.

- **2026-10-21 — Remote camera:** Added CAMERA1 capability and capture/status/
  abort jobs, JPEG-boundary and full SD CRC read-back checks, local-time/resolution
  naming through the GCS, and last-job recovery. The copied Lab 5 driver remains
  unchanged; an adapter bounds its waits and shares SPI with SD. Startup marker
  is `KFS1 camera-1`. Hardware camera validation is pending.

- **2026-10-21 — File management:** Added MANAGE1 capability, file-only DELETE
  and same-folder RENAME, no-overwrite checks and retained results for lost replies.
  Startup marker is `KFS1 manage-1`. All 24 host protocol tests and both baud-rate
  builds passed; delete/rename hardware testing remains pending.
- **2026-10-21 — Upload hardware result:** The project owner confirmed that the
  matching GCS and upload firmware work together on hardware.

- **2026-10-21 — Verified uploads:** Added capability negotiation, exclusive
  temporary files, bounded write retries with SD sync, batched read-back CRC
  verification, no-replace publication and status recovery for lost completion
  replies. Cancel/expiry clean temporary files; reserved leftovers can be removed
  from the GCS. Startup marker is `KFS1 upload-1`. The project owner subsequently confirmed uploads work.
- **2026-10-21 — Stability result:** The project owner confirmed the preceding
  read-recovery build worked on hardware. The physical cause and a measured
  twenty-transfer acceptance result remain unconfirmed.
- **2026-10-21 — Transfer stability diagnostic build:** Added up to three reads
  of the same chunk before reporting a permanent failure, with position/FAT-state
  restoration before retry. New USB diagnostics report SD errors, failed offsets,
  timing and resets. `SDCHECK /absolute/path` scans a file locally three times
  without radio traffic. Hardware root-cause/reliability testing remains pending.
- **2026-10-21 — 115200 transparent link confirmed:** The project owner reported
  successful connection and Remote Files operation using this baud-rate variant
  with both radios in AP=0. The ground radio's legacy API output exposed a GCS
  receive-format compatibility issue; use the updated GCS feature build when
  testing ground AP=1. Physical API/RSSI validation remains pending.
- **2026-10-21 — Optional 115200-baud test build:** Added the
  `esp32thing_plus_115200` environment. The original `esp32thing_plus` environment
  and default build remain at 9600 baud. Radio settings must be changed in XCTU
  before using the faster UART configuration; see the procedure below.

- **2026-10-20 — Hardware integration confirmed:** The project owner confirmed
  that this firmware and the Windows KestrelSAT GCS work together on the
  KestrelSAT/XBee hardware for the remote file-transfer milestone, using the
  working lab board configuration and 9600-baud radio link.
- **2026-10-15 — Remote SD file service implemented:** Added read-only directory
  browsing and binary downloads, sequential 128-byte chunks, request retries
  with response replay, per-frame and whole-file CRC32 checks, cancellation
  cleanup, and idle-session expiry. Added C++/Python protocol tests and a small
  firmware download artifact verified by ZIP extraction on Windows. The
  packaging workflow checks out only Lab 8 to avoid legacy Windows path limits.

## Download this project on Windows

In this repository's **Actions** tab, open **Download Lab 8 firmware**, then open
its latest successful run on `main`, or `feature/cs27-baud-benchmark` for the new
9600/GPIO27 environment before it is merged. Download **KestrelSAT-Lab8-Firmware** under Artifacts.
Sign into GitHub to download artifacts. Extract the ZIP and open its
`KestrelSAT-Lab8` folder in PlatformIO; `platformio.ini` is directly inside it.
This small artifact contains only this project. Its source files are verified by
compressing/extracting them and comparing their checksums on a Windows runner.
The workflow runs automatically when this project or its packaging workflow
changes on `main`, `feature/remote-files`, `feature/high-baud-link`, or
`fix/file-transfer-stability`, `feature/remote-upload`, `feature/remote-file-management`,
`feature/remote-camera`, or `feature/cs27-baud-benchmark`, including
merges into `main`.
Manual runs remain available through **Run workflow**.
Pull requests affecting this project or its workflow also validate packaging.

## GPIO27 baud comparison — 2026-10-22

The source ZIP on `feature/cs27-baud-benchmark` (and `main` once merged) includes
both environments below. Use the current GCS executable and the same physical
GPIO27 camera wiring for each test; no GCS update is required.

| Test | PlatformIO environment | Satellite XBee UART / ground XBee UART / GCS baud | Camera CS |
| --- | --- | --- | --- |
| Slow baseline | `benchmark_9600_cs27` | 9600 | GPIO27 |
| Confirmed faster setup | `esp32thing_plus_115200_camera_cs27` | 115200 | GPIO27 |

Stop transfers and disconnect the GCS. In XCTU configure both radios' UART baud
for the selected row, write the settings and close XCTU. Upload the matching
firmware environment and connect the GCS at that baud. Keep satellite AP=0,
ground AP=1/GCS API, and the chosen broadcast/unicast and RSSI test settings.
USB Serial Monitor remains **115200** for both builds; changing its speed does
not change the XBee UART. Keep all power off when changing wiring; no wiring
change is needed between these environments.

```sh
pio run -e benchmark_9600_cs27
pio run -e benchmark_9600_cs27 -t upload
```

Use the same saved JPEG, radio placement and power for comparisons. In Transfer
Stats start a named recording per configuration, download three times with short
idle gaps, stop recording and export rate/RSSI PNGs plus measurement and summary
CSVs. Retain failed runs and diagnostic JSONLs. A low rate or a clean run is a
measurement; no artificial packet loss is introduced. Restore both radio UARTs,
GCS baud and firmware to the confirmed GPIO27/115200 setup after the slow tests.

The original `esp32thing_plus` default still uses GPIO12/9600 and is not the
GPIO27 benchmark environment. Camera/file-service code and the 25 MHz SD clock
are shared across these builds; only the radio UART baud differs between the
two GPIO27 configurations. Physical 9600/GPIO27 testing is required after upload.

## Confirmed camera and radio setup — 2026-10-22

The owner confirmed camera-2 captures work, GPIO27 works well, and the system
reached a working stopping point. Use camera **CS=GPIO27** with
**`esp32thing_plus_115200_camera_cs27`**, both UARTs at **115200**, satellite
**AP=0**, ground **AP=1** with GCS API mode, and **unicast** addressing on both
radios. RSSI polling can remain enabled. The SD clock remains at the configured
25 MHz; no lower-clock firmware change was made during this handoff.

In XCTU, save the working profiles, read each radio's SH/SL and configure:

| Radio | DH | DL |
| --- | --- | --- |
| Ground | Satellite SH | Satellite SL |
| Satellite | Ground SH | Ground SL |

Copy the peer's hexadecimal values exactly, write both radios and reconnect the
GCS so it reads the destination. Keep network settings and MAC acknowledgments
enabled (MM=0 in the lab firmware). The supplied lab profile uses broadcast
DH=0/DL=FFFF; it is not the confirmed transfer setup. Firmware does not write
radio settings. Existing GPIO12/9600 and GPIO12/115200 environments remain available
for the original lab wiring; do not select them with the camera CS on GPIO27.

Two supplied logs download the same saved 640×480 image with unicast addressing:

| Log | RSSI polling | Operation duration | Protocol timeouts/retries | Result |
| --- | --- | --- | --- | --- |
| `kfs-20261023T021016Z-o38jaxio.jsonl` | Off | 22.407 s | 0 / 0 | Downloaded, checksum verified |
| `kfs-20261023T021158Z-r41bqc_5.jsonl` | On | 22.411 s | 0 / 0 | Downloaded, checksum verified |

Both complete 260 READ chunks with steady progress. Polling returned a final
-21 dBm reading. One rejected protocol line during OPEN in the polling-on run
caused no retry or measurable slowdown. UTC log filenames fall on October 23;
the local test date was October 22. Earlier broadcast examples had missing-reply
timeouts; this pair supports broadcast delivery contributing to pauses and shows
no meaningful polling overhead in these two runs.

The prior 83,996-byte image's SD READ_FAILED at offset 34,816 was not physically
diagnosed; local SDCHECK results were not supplied. The owner subsequently reported
all seemed fixed, but two successful downloads do not establish a twenty-transfer
reliability result or eliminate every possible SD fault. Keep USB/JSONL diagnostics
and use SDCHECK if an SD error recurs. No speculative SD-recovery change is included.

## Wiring and build

The configuration deliberately retains the working lab board identifier
`sparkfun_esp32s2_thing_plus_c` and environment `esp32thing_plus`.

| Connection | ESP32 GPIO |
| --- | --- |
| XBee DOUT → ESP32 RX | 16 |
| ESP32 TX → XBee DIN | 17 |
| SD chip select | 5 |
| Camera chip select | 27 confirmed; 12 in original lab environments |
| SPI SCK / MISO / MOSI | 18 / 19 / 23 |

For the confirmed camera setup, use the GPIO27/115200/unicast configuration above.
The original lab defaults remain **9600 baud, 8N1**, with GPIO12 camera CS.
USB debug output is 115200 baud. Use the same power/ground wiring as the labs.
The camera and SD card use separate chip selects and SPI transactions; camera
CS is released before each SD operation.

### Camera upload/detection failures — 2026-10-22

The first camera hardware test reported flash MD5 mismatches when attached,
intermittent detection, then `INVALID_JPEG` after 1,840 bytes at 96×96. The radio
log showed no retries, timeouts or malformed replies. The old capture code assumed
FIFO size exactly matched the JPEG; the vendor
[capture2SD example](https://github.com/ArduCAM/Arducam_Mega/blob/main/examples/ArduinoUNO/capture2SD/capture2SD.ino)
instead seeks the JPEG start marker and stops at its end marker. Camera-2 uses
that example's single-byte read primitive and a streaming marker/segment-length
framer, then verifies the extracted JPEG on SD before publication. Invalid frames
still fail with cleanup. The old log lacks image bytes, so the exact physical
padding/readout fault remains unconfirmed. Single-byte reads can save more slowly
at large resolutions; the 120-second whole-job deadline remains bounded.

**GPIO12 is also the ESP32 MTDI boot strap.** A connected camera holding it high
at reset can select 1.8 V for a board with 3.3 V flash, preventing flashing/boot.
See [Espressif's boot-pin documentation](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/boot-mode-selection.html).
This is a plausible cause of the reported MD5 mismatch, not proven by that error;
power instability can also cause it. Firmware cannot control boot straps during
ROM flashing. The lab board identifier and flash verification are preserved.

An optional environment avoids this pin without changing the working lab defaults:

1. Disconnect **all power**. Confirm GPIO27 is otherwise unused on your hardware.
2. Move only the camera **CS** wire from GPIO12 to **GPIO27**; retain SD, SPI,
   supply and ground wiring.
3. Upload **`esp32thing_plus_115200_camera_cs27`** and confirm `KFS1 camera-2`
   and `camera_cs=27` in the USB monitor. Do not select this environment with
   the camera still wired to GPIO12. Existing environments retain GPIO12.
4. Keep working radio/AP settings. In the matching GCS Camera tab, Refresh
   images, select **320×240**, Capture, then View. Repeat at 96×96 and 640×480.
5. If camera detection fails, wait three seconds and Refresh images to retry
   initialization, without resetting the working radio link. Ready cameras are
   not reset by Refresh. Retries are skipped during active file/camera jobs.

```sh
pio run -e esp32thing_plus_115200_camera_cs27
pio run -e esp32thing_plus_115200_camera_cs27 -t upload
```

If flashing still fails, disconnect the camera with all power off, upload, then
power down before reconnecting it. Do not hot-plug wiring, disable MD5 checks or
burn flash-voltage eFuses. Keep its rated supply connection; verify the camera
model and actual power wiring before changing voltage. A stated 5 V connection
does not establish supply stability. If the alternate CS pin does not resolve
boot/upload failures, collect reset/startup output and check supply/ground/cables.

Camera-2 advertises optional `CAMDIAG1`. `CAMDIAG|job_hex` returns
`CAMDETAIL|job_hex|fifo_bytes|read_bytes|saved_bytes|prefix_bytes|first_8_bytes_hex|last_8_bytes_hex|error`.
The GCS saves this bounded metadata as `camera_detail` in its JSONL diagnostic,
including failed captures. Counts/CRC in a successful CAMJOB describe only the
extracted JPEG. Marker/segment checks do not replace full GCS JPEG decoding.
Automated regression includes real padded JPEGs, marker splits, embedded thumbnail
markers, malformed/truncated frames and a fake-bus test of the production adapter
with the unchanged vendor driver. These checks do not establish hardware timing.

Open **this folder** in PlatformIO, then Build and Upload:

The following commands select the original GPIO12/9600 lab defaults. For the
confirmed camera hardware, select `esp32thing_plus_115200_camera_cs27` instead.

```sh
pio run -e esp32thing_plus
pio run -e esp32thing_plus -t upload
```

No serial monitor is required for startup. Insert the SD card before boot. If SD
initialization fails, the radio still starts; the GCS reports unavailable paths.
Check/reinsert the card and reset to retry initialization. Commands `1` (help)
and `3` (LED toggle) remain available in the serial monitor. Lab 4's blocking RSSI
command is not included in this file-service firmware.

## Optional 115200-baud test

The existing GCS already offers **115200** in its Baud Rate selector, including
in the transfer-statistics feature build. No new GCS executable is required for
this baud-rate option. Both transparent and AP=1 connections can use it.

1. Close the GCS and stop any transfer. In XCTU, save a backup of **each radio's**
   working configuration before changing it. Access the satellite radio through
   its appropriate USB adapter/programming connection; the GCS does not change
   radio settings remotely.
2. On the **ground XBee**, set **BD — UART Baud Rate** to **115200**, write it,
   then reconnect XCTU at 115200 if prompted. Retain its chosen AP mode: AP=1
   for the new GCS API/RSSI feature or AP=0 for transparent mode.
3. On the **satellite XBee**, set **BD** to **115200** and write it. Keep **AP=0**
   (transparent mode). Preserve destination addresses, network/security settings,
   and all other working radio parameters on both radios.
4. In PlatformIO, select **Project Tasks → esp32thing_plus_115200 → Upload**,
   or run the commands below. This uses the same board and pins as the working
   lab firmware. The USB debug monitor remains at 115200 and prints the selected
   XBee UART rate at startup; USB monitor speed alone does not change radio baud.
5. Close XCTU, open the GCS, select **115200 baud, 8N1** and the correct ground
   radio mode, then Connect. Start with a small file and compare it with its SD
   original, then test a larger file and compare rate/CSV plots with a 9600 run.

```sh
pio run -e esp32thing_plus_115200
pio run -e esp32thing_plus_115200 -t upload
```

The matching pairs are **GCS ↔ ground XBee UART** and **ESP32 firmware ↔ satellite
XBee UART**. UART rates need not match across the RF link, but using 115200 on
both ends removes the old 9600-baud serial bottlenecks. It does not change the RF
bitrate or guarantee twelve times the file-transfer throughput: packet overhead,
encoding, radio latency, retries and RSSI polling still contribute.

To roll back, restore both XBees' BD settings to 9600 in XCTU, upload
`esp32thing_plus`, and select 9600 in the GCS. Keep each end's AP mode matched to
its software. Both build variants are compiled during development; over-the-air
115200 connection and Remote Files operation in transparent mode were confirmed
by the project owner on 2026-10-21. API/RSSI operation with the corrected GCS
receive handling still requires a hardware test. The supplied ground radio uses
legacy 802.15.4 API indicators (AO=2); keep that setting and use the updated
[GCS feature build](https://github.com/chasedustevichusafa/KestrelSAT-GCS/tree/feature/transfer-statistics).

## Stability diagnostics — 2026-10-21

Use the `fix/file-transfer-stability` source artifact and the matching GCS
stability build. At startup the satellite USB monitor prints `KFS1 camera-2` in the current camera build,
reset reason, SD SPI clock and the XBee UART baud. SD reads retry at the exact
same byte offset up to three times, with a 10 ms pause before retries; partial
attempts do not advance the protocol offset or checksum. Permanent failures
close the session and report `READ_FAILED|offset|wanted|got|attempts` to the GCS.
SD failures/recoveries and periodic read checkpoints go to USB only. Raw
`file_latched` error flags may persist after a successful retry.

For a local SD baseline, disconnect the GCS and open the satellite USB monitor
at 115200. Enable newline endings and send `SDCHECK /path/to/file.jpg`. With no
active transfer, it makes three local read-only passes without recovery retries
or transmitting file data. Each completed pass prints bytes, CRC32 and elapsed
milliseconds; a failure prints the offset, requested/returned counts and SD
errors. All passes should complete with matching CRCs. Wait for an old session
to expire or reset if it reports busy. The scan occupies the firmware loop, so
keep the GCS disconnected while running it.

The default SD SPI clock remains 25 MHz. For an evidence-driven lower-clock
comparison, add `-D SD_SPI_MHZ=10` to the selected environment's build_flags,
retaining `-D XBEE_UART_BAUD=115200` for the faster UART environment. Rebuild,
upload and verify the printed clock. Preserve the working board/wiring and
change one variable at a time; the lower clock is not an established fix.

Follow the [GCS stability test procedure](https://github.com/chasedustevichusafa/KestrelSAT-GCS/blob/fix/file-transfer-stability/docs/file-transfer-stability.md)
for comparisons of local SD, transparent radio, API without polling and API with
RSSI polling. The GCS saves bounded metadata captures under `transfer_diagnostics/`.
The hardware goal is at least 20 consecutive verified downloads per supported
mode including the largest failing file. Simulated tests do not meet that goal.

## Upload a file — 2026-10-21

Use the matching GCS executable and source artifact on `feature/remote-file-management`.
Select `esp32thing_plus_115200` when uploading firmware for the existing 115200
radios. Keep the working wiring, SD clock and radio configuration. Confirm the
satellite USB startup marker `KFS1 manage-1` in the current build.

In GCS **Remote Files**, browse to an existing destination folder and Refresh.
The new firmware advertises `CAPS|UPLOAD1|MANAGE1|128` in the current build, enabling **Upload...** and
**Remove partial...**. Select a local file and choose its destination filename.
Existing names are rejected, including case-insensitive aliases. The final name
appears only after complete size/CRC verification of data read back from the SD.
The GCS refreshes the folder and records acknowledged upload rates/RSSI in the
existing Transfer Stats graphs, CSV logs and PNG exports.

Temporary files are named `.kfs-xxxxxxxx.part` beside the destination. Upload
names cannot use that prefix (case-insensitive), FAT-reserved characters or
trailing dots/spaces. Paths are limited to 192 UTF-8 bytes and must leave room
for that temporary name; files are limited to 4 GiB minus one byte. Creation
requires an existing folder and available space. Every 128-byte write is synced
before ACK, with up to three attempts. Retries reopen only the session's own
temporary file and write at the original offset. Verification processes up to
1024 bytes per request so large files retain progress/cancellation.

COMMIT checks received and stored CRCs before renaming with a no-existing-target
requirement. Lost COMMIT replies can be recovered through retained STATUS or a
repeated COMMIT; the last completion result survives idle expiry but is held in
RAM and is lost on reset. An unconfirmed outcome is explicitly reported by the
GCS. Verify the destination before retrying in that case. Filesystem rename is
not a power-loss transaction.

Cancel and idle expiry attempt to remove the temporary file. After a reset or
failed cleanup, Refresh, select the reserved `.part` entry and use **Remove
partial...**, confirming its path. Cleanup rejects ordinary filenames,
directories and active sessions. Incomplete uploads restart rather than resume.
USB diagnostics include SD_WRITE attempts, publication and cleanup results;
the GCS records bounded metadata-only diagnostics without file payloads.

New KFS1 operations are CAPS, PUT, WRITE, VERIFY, COMMIT, STATUS, ABORT and CLEAN.
HELLO/download commands remain compatible. See the [complete upload protocol and
hardware checklist](https://github.com/chasedustevichusafa/KestrelSAT-GCS/blob/feature/remote-upload/docs/remote-upload.md).
For acceptance, upload/download-compare empty, small, 14 KB JPEG and larger binary
files, then test Cancel/disconnect and repeated runs with API RSSI polling on/off.
Automated simulation and compilation do not establish physical upload reliability.

## Delete and rename files — 2026-10-21

Use matching `feature/remote-file-management` firmware and GCS builds. Upload
`esp32thing_plus_115200` for the current 115200 radios and keep their working
AP settings. Confirm startup marker `KFS1 manage-1`. In GCS Remote Files,
**Refresh / Open**, select a file, and choose **Rename...** or **Delete...**.
Deletion asks for confirmation and permanently removes the selected file. Rename
keeps the current folder and rejects existing destinations and case-only changes.
Folders and reserved `.kfs-` upload names are excluded; use Remove partial for
unfinished uploads. Old firmware leaves the new management buttons disabled.

The service advertises `CAPS|UPLOAD1|MANAGE1|128`. New commands are DELETE with a
hex path, RENAME with hex source/target paths, and RESULT with the original
request ID. Replies are DELETED, RENAMED or ERR; RESULT returns the retained
operation/outcome or UNKNOWN. The last management request/reply survives metadata
queries and idle expiry in RAM, allowing duplicate replay and lost-reply recovery.
Reset or a newer management operation loses/replaces this record. On unknown
completion, Refresh and inspect before trying again. These SD operations are not
power-loss transactions. Active file sessions block management; dispatched
operations cannot be cancelled. Frames allow 1024 bytes for two 192-byte paths.

All 24 host protocol tests passed, including loss/retry, idle expiry, long paths,
backend errors, directory rejection and overwrite protection. Both firmware
baud-rate builds passed. Test on disposable files first: rename, download/compare,
delete and Refresh. Management hardware validation remains pending. Full setup:
[GCS file management](https://github.com/chasedustevichusafa/KestrelSAT-GCS/blob/feature/remote-file-management/docs/remote-file-management.md).

## Camera capture — 2026-10-21

Extract the **matching camera** GCS executable and this source ZIP from
`feature/remote-camera`. All required Arducam sources and the MIT license are
under `lib/Arducam_Mega-3.0.0`; no Lab 5 checkout is needed to build this package.
Labs 4/5 remain unmodified references. Use PlatformIO **Project Tasks →
esp32thing_plus_115200_camera_cs27 → Upload** for the confirmed GPIO27/115200 setup;
`esp32thing_plus_115200` retains GPIO12/115200, and the default
`esp32thing_plus` remains 9600. Confirm USB startup marker `KFS1 camera-2` and the
camera sensor/model/status line. Retain existing radio AP/baud/network settings.

In GCS **Camera → Refresh images**, select a supported resolution (640×480 by
default when available), then **Capture image**. The satellite creates `/images`
if needed and saves a JPEG to an exclusive temporary file. Its status advances
through Capturing, Saving and Verifying. The final `.jpg` appears only after
complete size/JPEG-boundary checks, SD sync and CRC32 read-back verification.
Capture success does not download the image automatically. Select **View** to
perform a checked download and open a static GCS viewer with Fit/zoom/original
size controls. Download saves to a chosen destination; Rename/Delete operate on
the satellite image, with overwrite protection and delete confirmation.

Example filename: `/images/20261021T153045-0600_640x480_a14f9082.jpg`. Time is the
GCS computer's local capture-request time, including UTC offset; it is not a
satellite shutter/RTC measurement. The job suffix prevents same-second name
collisions and existing names are rejected. Renaming can change the generated name.

**Stop waiting does not cancel the onboard capture.** Disconnecting or closing
the GCS also lets the satellite finish saving. The GCS remembers pending intent;
reconnect and use **Check capture** before another request. Last-job status is
held in RAM until another job or reset. UNKNOWN after reset requires Refresh and
inspection of `/images`, not automatic recapture. Files remain on SD. Incomplete
`.kfs-xxxxxxxx.part` files can be inspected/removed using the existing Remote
Files partial-cleanup control. A cleanup failure is explicit. Publication is not
a power-loss transaction; inspect after interruption.

Camera initialization waits have a 4-second budget and capture control waits a
2-second budget; capture completion is polled with a
15-second limit and a whole capture/save/verify job has a 120-second limit. Data
moves in 256-byte buffers; no whole-image allocation is used. Camera SPI uses
1 MHz transactions, while the existing SD clock is preserved. SD read/write
retries retain the same offset and write retries reuse buffered camera data.
File operations are blocked while a job is active; camera status/abort remain
available. Missing cameras do not stop the file service. Check power/wiring and
Refresh images after fixing an initialization fault. USB prints camera and SD
error details.

CAPS becomes `CAPS|UPLOAD1|MANAGE1|CAMERA1|CAMDIAG1|128`; HELLO/file chunks remain compatible.
`CAMINFO` returns model, supported-resolution mask, camera error and SD state.
`CAPTURE|job_hex|mode_index|path_hex`, `CAMSTATUS|job_hex`, and `CAMABORT|job_hex`
return `CAMJOB|job_hex|state|done|total|crc32|path_hex|error`. Capture paths are
restricted to one `.jpg` file within `/images`. Repeated job IDs retain the same
outcome rather than taking another photo; changed parameters for that ID are
rejected. The GCS exposes Stop waiting and Check capture, not automatic CAMABORT.

Image downloads participate in existing speed/RSSI measurements. Multi-operation
recordings include capture events; onboard capture/save is zero useful file
throughput until a download begins. All 33 portable protocol tests pass, including
loss/retry, duplicate jobs, camera/job timeouts, SD failure/corruption, collision
protection, partial cleanup, padded real JPEGs and the production camera adapter
with a fake bus. Both baud variants and the optional GPIO27 build compile. These
checks do not establish physical camera operation.

On hardware, first capture 320×240 or 640×480, View and check decoded dimensions,
Download/compare copies, then rename/delete a disposable image. Test higher
resolutions, repeated captures, Stop waiting/Check capture and reconnect. Recheck
existing uploads/downloads/statistics. Keep USB output and GCS diagnostics on any
failure. Full instructions and limits:
[GCS camera guide](https://github.com/chasedustevichusafa/KestrelSAT-GCS/blob/feature/remote-camera/docs/camera.md).

## Hardware test

Start the updated GCS executable, connect to the ground XBee COM port at 9600,
then **Remote Files → Refresh / Open**. Browse `/`, double-click folders, select a
file and **Download…**. Start with a small known text file, then a JPEG and a larger
binary file. Compare downloaded contents against the SD originals. Test Cancel
and reconnect as well. Downloads preserve SD contents; uploads require a new
filename. Both directions restart rather than resume.
The original Lab 4/5 firmware does not implement these commands.

See the GCS repository's `docs/remote-files.md` for the Windows build/download
steps, protocol limits, and detailed user instructions.

## Protocol

`include/file_service.h` is a portable C++11 request/response engine; `src/main.cpp`
adapts it to SdFat and the Lab 4 XBee UART. There are no heap-sized file buffers.
Only one file is held open. Normal heartbeat text is suppressed for 30 seconds
after protocol activity; idle sessions close after that interval.

Wire form (ASCII, newline terminated):

```text
@KFS1|<8-digit hex request ID>|<operation>|<fields...>|<8-digit CRC32>\n
```

CRC32 is IEEE/zlib, over all bytes before the last `|`. Paths, names, and data are
hex-encoded UTF-8/bytes. This keeps `+++` out of protocol traffic. The GCS sends a
leading newline before each request to recover a truncated preceding command.

| Request fields | Response fields |
| --- | --- |
| `HELLO` | `HELLO\|1\|128` |
| `LIST\|path_hex\|decimal_index` | `ENTRY\|next_index\|D_or_F\|size\|name_hex`, or `END` |
| `OPEN\|path_hex` | `FILE\|session_token\|size` |
| `READ\|session_token\|decimal_offset` | `DATA\|offset\|hex_bytes` (at most 128 bytes) |
| `END\|session_token` | `DONE\|size\|whole_file_crc32` |
| `CLOSE\|session_token` | `CLOSED` |

Errors are `ERR|CODE`. The token is the OPEN request ID. READ must advance
sequentially; only the last identical request is replayed on retries. END verifies
that all bytes were read, returns the whole-file checksum, and closes the file.
Upload and management commands extend this base protocol (see above).
Maximum path: 192 bytes; maximum frame buffer: 1024 bytes; maximum file: 4 GiB minus one byte. Do not change card contents during
an operation. Each LIST reopens/scans the directory to avoid persistent directory
handles across canceled listings. The GCS caps listings at 4096 entries.

## Automated compatibility tests

On Linux with GCC/G++ (C and C++17) and the pinned GCS Python dependencies
(including Pillow) installed:

```sh
python test/test_protocol.py --gcs /path/to/KestrelSAT-GCS -v
```

The tests compile the actual service header with a host filesystem adapter and
exercise it through a PTY with the real Python client. They inject dropped,
corrupted, duplicated and truncated replies and verify cancellation, partial-file
cleanup, checksum failure, empty files, and binary downloads. These checks do not
replace an over-the-air hardware test.
