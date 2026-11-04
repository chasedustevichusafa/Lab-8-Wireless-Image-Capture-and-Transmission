# Lab 8: remote JPEG capture and verified file transfers

**Final operating documentation — November 4, 2026 (America/Denver).**
Upload this PlatformIO project for the completed KestrelSAT GCS **Remote Files**,
**Camera** and **Live Preview** workflows. Labs 4/5 remain reference projects.
The owner confirmed operation of the merged features; use current `main` sources
in both repositories. Preview is repeated still capture/download orchestrated by
the GCS, not an additional onboard streaming firmware. Dedicated Link Tracker
likewise uses the existing service, with RSSI collected by the ground radio/GCS.

## Download this project on Windows

1. Sign into GitHub and open [Download Lab 8 firmware](https://github.com/chasedustevichusafa/Lab-8-Wireless-Image-Capture-and-Transmission/actions/workflows/lab8-firmware.yml).
   Select a successful **main** run for the desired source commit, then download
   **KestrelSAT-Lab8-Firmware** from Artifacts.
2. Extract the ZIP, and open **KestrelSAT-Lab8** in PlatformIO. `platformio.ini`
   must be directly inside the opened folder. The package contains the complete
   application, tests, required Arducam sources and license. Lab 5 is not needed.
3. Select **Project Tasks → esp32thing_plus_115200_camera_cs27 → Upload** for
   the confirmed camera wire on GPIO27. Confirm USB startup **KFS1 speed-1**,
   `camera_cs=27` and **XBee UART: 115200 baud**.
4. Configure both radios separately in XCTU as below, close XCTU, then launch the
   matching [main GCS executable](https://github.com/chasedustevichusafa/KestrelSAT-GCS/actions/workflows/build-windows.yml).

This is a **source ZIP**, not a precompiled flash binary. The source workflow
checks required files and a Windows ZIP extraction/checksum round-trip; it does
not compile or upload firmware. Artifacts expire after **30 days**. Old unexpired
runs remain downloadable; Git retains committed source after artifact expiry.
**Run workflow** can package another available ref. Relevant main pushes and
project/workflow pull requests automatically validate packaging, including main
merges. The small package avoids legacy whole-repository Windows path limits.

## Confirmed camera and radio setup — 2026-10-22

| Setting | Confirmed operating value |
| --- | --- |
| PlatformIO environment | `esp32thing_plus_115200_camera_cs27` |
| Board / framework | `sparkfun_esp32s2_thing_plus_c` / Arduino; retain working lab configuration |
| Satellite XBee UART | 115200, 8N1, **AP=0** |
| Ground XBee / GCS | 115200, 8N1, **AP=1** / **XBee API (AP=1)** |
| Camera CS | GPIO27 |
| SD SPI / USB monitor | 25 MHz / 115200 baud |
| Addressing | Unicast to the peer's SH/SL, with working MAC acknowledgments |

Read each radio's hexadecimal SH/SL in XCTU. Set ground **DH=Satellite SH,
DL=Satellite SL**, and satellite **DH=Ground SH, DL=Ground SL**, write both, then
reconnect the GCS. Retain the working network/security settings and MAC ACKs
(MM=0 in the supplied lab profile). The lab's **DH=0/DL=FFFF** is broadcast and
is separate from the confirmed unicast configuration.

AP=0 passes serial bytes; AP=1 frames the local ground-radio/computer UART for
addressing and diagnostics. Ground API envelopes are removed/added locally, so
the satellite remains AP=0. The current GCS supports legacy `0x80`/`0x81` AO
output and modern `0x90`/`0x91` receive frames. Neither API mode nor a faster UART
changes the radio's RF bitrate. Firmware never rewrites radio settings.

The confirmed camera/GPIO27 and unicast setup resolved the reported working
issues. Two same-image 640×480 unicast downloads took **22.407 s RSSI off** and
**22.411 s RSSI on**, each with zero protocol timeouts/retries and verified CRCs
(`kfs-20261023T021016Z-o38jaxio.jsonl` / `kfs-20261023T021158Z-r41bqc_5.jsonl`).
These were October 22 local-time tests; log filenames use UTC. They show no
meaningful polling overhead in those two runs, not a guaranteed reliability rate.
An earlier SD READ_FAILED at offset 34816 was not physically diagnosed; retain
SD/USB diagnostics and use SDCHECK if it recurs.

## Firmware environments and build commands

| Environment | XBee UART | Camera CS | Purpose |
| --- | ---: | --- | --- |
| `esp32thing_plus_115200_camera_cs27` | 115200 | GPIO27 | Completed working setup |
| `benchmark_9600_cs27` | 9600 | GPIO27 | Slow comparison without moving camera wiring |
| `esp32thing_plus_115200` | 115200 | GPIO12 | Original camera-CS wiring, faster UART |
| `esp32thing_plus` (default) | 9600 | GPIO12 | Original lab defaults |

Run from the folder containing this `platformio.ini`:

```sh
pio run -e esp32thing_plus_115200_camera_cs27 -j 4
pio run -e benchmark_9600_cs27 -j 4
pio run -e esp32thing_plus_115200 -j 4
pio run -e esp32thing_plus -j 4
```

Upload only the environment matching the physical wiring and radio UART baud:

```sh
pio run -e esp32thing_plus_115200_camera_cs27 -t upload
```

PlatformIO's name `esp32thing_plus` does not override the actual board setting.
The selected build changes firmware UART/CS definitions, not either XBee's
configuration. USB monitoring stays 115200 in all environments.

## GPIO27 baud comparison — 2026-10-22

Stop operations and disconnect the GCS. Save both XCTU profiles. For the slow
comparison write **9600 UART baud on both radios**, upload
`benchmark_9600_cs27`, and select 9600 in the GCS. Retain satellite AP=0,
ground AP=1/GCS API and the same unicast, RSSI, profile and antenna settings.
USB monitoring stays 115200. The default `esp32thing_plus` is GPIO12, not the
GPIO27 slow environment. Restore both radio UARTs/GCS to 115200 and upload
`esp32thing_plus_115200_camera_cs27` afterwards. No wiring change is required.

## Optional 115200-baud test

For original GPIO12 wiring, `esp32thing_plus_115200` is the faster UART build;
for GPIO27 use the confirmed environment above. Set each radio's UART baud in
XCTU, close XCTU and use matching GCS baud/mode. The local pairs that must agree
are firmware ↔ satellite XBee and GCS ↔ ground XBee. The RF link can bridge
different UART rates, but matching 115200 on both removes both old serial
bottlenecks. USB monitor speed does not configure either XBee.

## Speed-test installation and comparison (2026-10-27)

Use matching main GCS/firmware artifacts. Camera and Remote Files share a
**Transfer profile** preference; each operation snapshots its profile before
starting, so changing a control cannot alter an in-flight transfer.

| Profile | Useful bytes/block | Codec | Requests per batch |
| --- | ---: | --- | ---: |
| Baseline | 128 | Hex | 1 |
| Larger blocks | 256 | Hex | 1 |
| Compact | 256 | Base64 | 1 |
| Fast | 512 | Base64 | 1 |
| Experimental | 512 | Base64 | 2 |
| Auto | Fast with FAST1; legacy Baseline otherwise | Negotiated | 1 |

Owner-reported useful download speeds were about **1500 B/s Baseline**,
**3850 B/s Auto/Fast**, and **4700 B/s Experimental**. These are observed results,
not guarantees or upload benchmarks. Batch 2 sends at most two data requests
and confirms both before another batch; it is not a two-byte block, simultaneous
file transfers or an unbounded sliding window. Retry only unconfirmed commands
with their original IDs. Two fixed request/reply entries retain successful data
replies across metadata queries; a transient future-offset error is not cached.

FAST1 adds `OPENX|pathHex|chunk|codec` and
`PUTX|pathHex|size|crcHex|chunk|codec`. FILE/READY echo the agreed profile.
`READX`/`WRITEX` use the negotiated HEX or B64 encoding. Supported choices are
128/256-byte HEX or 128/256/512-byte B64. Base64 uses four characters per three
bytes, compared with six for hex; it remains CRC-framed text, not raw binary UART
traffic. All records fit the existing 1024-byte parser. Upload sync/read-back,
whole-file checksums, no-overwrite publication and camera safeguards remain.
Legacy HELLO/commands retain 128-byte hex. Auto falls back before opening a file;
explicit faster profiles require FAST1. Profiles never switch silently after errors.

Compare the same saved JPEG at least five times per profile, retaining the same
radio placement, baud, addressing and RSSI setting. Export CSV and PNG in Transfer
Stats; keep complete durations, failures, retries and actual `transfer_profile`.
Repeat uploads with fresh destination names and include read-back completion time.
A multi-operation recording keeps idle intervals between runs. If a profile
fails, retain diagnostics, compare Fast without batching, then Baseline.
See the [GCS comparison guide](https://github.com/chasedustevichusafa/KestrelSAT-GCS/blob/main/docs/transfer-speed.md).

## Live Preview, measurement and Link Tracker

The GCS **Live Preview** tab schedules repeated still captures with supported
resolution selection and **5/10-second start-to-start targets**. It verifies the
SD image and download, decodes/displays it, then deletes **only that preview job's
image**. Ordinary still photos are preserved. Save frame writes a permanent PC
copy. The pop-out keeps native image pixels while its window resizes. Pending
capture/delete intent can be recovered without another exposure or automatic
restart. Larger JPEGs and camera/SD work can exceed a target; no frame backlog
is accumulated. The guide uses reported speed tiers and a two-second overhead
allowance; it is not a guaranteed resolution/frame-rate specification.

Transfer Stats supports useful upload/download rates, optional ground RSSI,
continuous multi-operation recordings and CSV/PNG exports. White/black graph
formatting is independent of UI theme; RSSI can overlay rate on a separate dBm
axis without changing raw data or replacing missing samples. Dedicated **Link
Tracker** records received RSSI, round-trip success/latency and distance markers
without a file transfer. It uses legacy packet RSSI or a rate-limited DB fallback.
For 16-bit receive frames, verify the satellite **MY** source (possibly `0000`),
not a 64-bit destination. **Open CSV bundle... / Open session folder...** rebuilds
its saved graphs offline with explicit optional source rechecking. These are GCS
features; no extra firmware environment or radio-programming command is needed.

Guides: [preview](https://github.com/chasedustevichusafa/KestrelSAT-GCS/blob/main/docs/live-preview.md),
[statistics](https://github.com/chasedustevichusafa/KestrelSAT-GCS/blob/main/docs/transfer-statistics.md),
[recordings](https://github.com/chasedustevichusafa/KestrelSAT-GCS/blob/main/docs/multi-operation-recordings.md),
[Link Tracker](https://github.com/chasedustevichusafa/KestrelSAT-GCS/blob/main/docs/link-tracker.md).

## Source reading order

1. `src/main.cpp`: board/UART/SPI setup, bounded command collection, SdFat backend,
   USB diagnostics and the cooperative main loop. USB and radio are separate
   channels; radio configuration is still done in XCTU.
2. `include/file_service.h`: KFS1 framing/CRC, field validation, request replay,
   one active transfer, upload/download states and retained mutation results.
   Radio retries and local SD attempts are different layers. Legacy transfers
   use 128-byte hex; FAST1 negotiates larger blocks and optional two-request batches.
   `include/transfer_codec.h` implements bounded, strict Base64 encode/decode.
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

**The confirmed camera setup uses GPIO27.** GPIO12/MTDI flash-voltage strapping
is documented for classic ESP32 chips, but must not be assumed for another SoC
from a pin name or environment label. The project retains its actual
`sparkfun_esp32s2_thing_plus_c` configuration. The MD5 report alone did not
establish a strapping or supply root cause. Firmware cannot correct hardware
conditions during ROM flashing. Retain flash verification and the working board
configuration; see [Espressif's chip-specific boot guidance](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/boot-mode-selection.html).

An optional environment avoids this pin without changing the working lab defaults:

1. Disconnect **all power**. Confirm GPIO27 is otherwise unused on your hardware.
2. Move only the camera **CS** wire from GPIO12 to **GPIO27**; retain SD, SPI,
   supply and ground wiring.
3. Upload **`esp32thing_plus_115200_camera_cs27`** and confirm `KFS1 speed-1`
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

## Stability diagnostics — 2026-10-21

Use current main firmware and the matching GCS build. At startup the satellite USB monitor prints `KFS1 speed-1` in the current camera build,
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

Follow the [GCS stability test procedure](https://github.com/chasedustevichusafa/KestrelSAT-GCS/blob/main/docs/file-transfer-stability.md)
for comparisons of local SD, transparent radio, API without polling and API with
RSSI polling. The GCS saves bounded metadata captures under `transfer_diagnostics/`.
The hardware goal is at least 20 consecutive verified downloads per supported
mode including the largest failing file. Simulated tests do not meet that goal.

## Upload a file — 2026-10-21

Use matching current main GCS and firmware artifacts.
Select `esp32thing_plus_115200_camera_cs27` for the confirmed GPIO27/115200
setup; use the GPIO12 variant only with original camera-CS wiring. Keep the working wiring, SD clock and radio configuration. Confirm the
satellite USB startup marker `KFS1 speed-1` in the current build.

In GCS **Remote Files**, browse to an existing destination folder and Refresh.
The new firmware advertises `CAPS|UPLOAD1|MANAGE1|CAMERA1|CAMDIAG1|FAST1|128` in the current build, enabling **Upload...** and
**Remove partial...**. Select a local file and choose its destination filename.
Existing names are rejected, including case-insensitive aliases. The final name
appears only after complete size/CRC verification of data read back from the SD.
The GCS refreshes the folder and records acknowledged upload rates/RSSI in the
existing Transfer Stats graphs, CSV logs and PNG exports.

Temporary files are named `.kfs-xxxxxxxx.part` beside the destination. Upload
names cannot use that prefix (case-insensitive), FAT-reserved characters or
trailing dots/spaces. Paths are limited to 192 UTF-8 bytes and must leave room
for that temporary name; files are limited to 4 GiB minus one byte. Creation
requires an existing folder and available space. Every data-block write (128 bytes in Baseline; negotiated in FAST1) is synced
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
hardware checklist](https://github.com/chasedustevichusafa/KestrelSAT-GCS/blob/main/docs/remote-upload.md).
For acceptance, upload/download-compare empty, small, 14 KB JPEG and larger binary
files, then test Cancel/disconnect and repeated runs with API RSSI polling on/off.
Automated simulation and compilation do not establish physical upload reliability.

## Delete and rename files — 2026-10-21

Use matching `main` firmware and GCS builds. Upload
`esp32thing_plus_115200_camera_cs27` for GPIO27/115200 and keep the working
AP settings. Confirm startup marker `KFS1 speed-1`. In GCS Remote Files,
**Refresh / Open**, select a file, and choose **Rename...** or **Delete...**.
Deletion asks for confirmation and permanently removes the selected file. Rename
keeps the current folder and rejects existing destinations and case-only changes.
Folders and reserved `.kfs-` upload names are excluded; use Remove partial for
unfinished uploads. Old firmware leaves the new management buttons disabled.

The service advertises `CAPS|UPLOAD1|MANAGE1|CAMERA1|CAMDIAG1|FAST1|128`. New commands are DELETE with a
hex path, RENAME with hex source/target paths, and RESULT with the original
request ID. Replies are DELETED, RENAMED or ERR; RESULT returns the retained
operation/outcome or UNKNOWN. The last management request/reply survives metadata
queries and idle expiry in RAM, allowing duplicate replay and lost-reply recovery.
Reset or a newer management operation loses/replaces this record. On unknown
completion, Refresh and inspect before trying again. These SD operations are not
power-loss transactions. Active file sessions block management; dispatched
operations cannot be cancelled. Frames allow 1024 bytes for two 192-byte paths.

Host protocol coverage includes loss/retry, idle expiry, long paths, backend
errors, directory rejection and overwrite protection. The owner confirmed the
completed file-management workflow. Test disposable files after changing a setup:
rename, download/compare, delete and Refresh. Full setup:
[GCS file management](https://github.com/chasedustevichusafa/KestrelSAT-GCS/blob/main/docs/remote-file-management.md).

## Camera capture — 2026-10-21

Extract the **matching camera** GCS executable and this source ZIP from
`main`. All required Arducam sources and the MIT license are
under `lib/Arducam_Mega-3.0.0`; no Lab 5 checkout is needed to build this package.
Labs 4/5 application and vendor sources remain reference material. Use PlatformIO **Project Tasks →
esp32thing_plus_115200_camera_cs27 → Upload** for the confirmed GPIO27/115200 setup;
`esp32thing_plus_115200` retains GPIO12/115200, and the default
`esp32thing_plus` remains 9600. Confirm USB startup marker `KFS1 speed-1` and the
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

CAPS becomes `CAPS|UPLOAD1|MANAGE1|CAMERA1|CAMDIAG1|FAST1|128`; HELLO/file chunks remain compatible.
`CAMINFO` returns model, supported-resolution mask, camera error and SD state.
`CAPTURE|job_hex|mode_index|path_hex`, `CAMSTATUS|job_hex`, and `CAMABORT|job_hex`
return `CAMJOB|job_hex|state|done|total|crc32|path_hex|error`. Capture paths are
restricted to one `.jpg` file within `/images`. Repeated job IDs retain the same
outcome rather than taking another photo; changed parameters for that ID are
rejected. The GCS exposes Stop waiting and Check capture, not automatic CAMABORT.

Image downloads participate in existing speed/RSSI measurements. Multi-operation
recordings include capture events; onboard capture/save is zero useful file
throughput until a download begins. Portable protocol coverage includes
loss/retry, duplicate jobs, camera/job timeouts, SD failure/corruption, collision
protection, partial cleanup, padded real JPEGs and the production camera adapter
with a fake bus. The owner confirmed camera capture and GPIO27 operation; automated checks
remain distinct from hardware reliability measurements.

On hardware, first capture 320×240 or 640×480, View and check decoded dimensions,
Download/compare copies, then rename/delete a disposable image. Test higher
resolutions, repeated captures, Stop waiting/Check capture and reconnect. Recheck
existing uploads/downloads/statistics. Keep USB output and GCS diagnostics on any
failure. Full instructions and limits:
[GCS camera guide](https://github.com/chasedustevichusafa/KestrelSAT-GCS/blob/main/docs/camera.md).

## Protocol

`include/file_service.h` is a portable C++11 request/response engine; `src/main.cpp`
adapts it to SdFat and the Lab 4 XBee UART. There are no heap-sized file buffers.
Only one file is held open. Normal heartbeat text is suppressed for 30 seconds
after protocol activity; idle sessions close after that interval.

The table below describes the **legacy 128-byte hex commands**, retained for
Baseline/old-client compatibility. FAST1 extends them as described above.

Wire form (ASCII, newline terminated):

```text
@KFS1|<8-digit hex request ID>|<operation>|<fields...>|<8-digit CRC32>\n
```

CRC32 is IEEE/zlib, over all bytes before the last `|`. Paths/names and legacy
data are hex-encoded UTF-8/bytes; FAST1 data uses its negotiated encoding.
Legacy hex encoding keeps `+++` out of the data payload. The GCS sends a
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
sequentially; legacy requests can be replayed on retries. FAST1 data operations retain two
request/reply entries for the bounded batch mode. END verifies
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

## Completion and validation record — 2026-11-04

The completed main firmware includes UPLOAD1, MANAGE1, CAMERA1, CAMDIAG1 and
FAST1. The current USB startup marker is **KFS1 speed-1**; camera-1/camera-2,
manage-1 and upload-1 describe earlier milestones, not the current required build.
The owner confirmed camera/GPIO27, file operations, speed profiles and preview;
Link Tracker/graph replay are implemented in the GCS. All four environments are
retained for reproducible wiring/baud comparisons. Vendored camera sources and
licenses remain unchanged.

Compile all selected environments and run the host protocol command above after
firmware changes. For a hardware check, Refresh files, download/compare a known
file, upload under a new name, download/compare it, then rename/delete a disposable
copy. Capture and View 320×240 or 640×480 before larger images; exercise preview,
Stop/Check capture, reconnect and pending-frame recovery. Save CSVs/JSONLs and
USB output on errors. Repeat enough operations to characterize your actual
setup. Build/simulation results and owner-reported working tests do not establish
a universal radio range, guaranteed refresh rate or measured failure probability.
