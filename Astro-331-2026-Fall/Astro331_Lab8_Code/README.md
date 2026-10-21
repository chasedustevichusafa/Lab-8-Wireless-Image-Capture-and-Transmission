# Lab 8: remote SD browser and verified file transfers

Upload this project to use the GCS **Remote Files** tab. Labs 4 and 5 are left
unchanged. This build supports verified uploads and downloads; it does not yet command
camera capture or provide live preview.

## Dated changes

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
its latest successful run on `feature/remote-file-management`. Download **KestrelSAT-Lab8-Firmware** under Artifacts.
Sign into GitHub to download artifacts. Extract the ZIP and open its
`KestrelSAT-Lab8` folder in PlatformIO; `platformio.ini` is directly inside it.
This small artifact contains only this project. Its source files are verified by
compressing/extracting them and comparing their checksums on a Windows runner.
The workflow runs automatically when this project or its packaging workflow
changes on `main`, `feature/remote-files`, `feature/high-baud-link`, or
`fix/file-transfer-stability`, `feature/remote-upload`, or `feature/remote-file-management`, including
merges into `main`.
Manual runs remain available through **Run workflow**.

## Wiring and build

The configuration deliberately retains the working lab board identifier
`sparkfun_esp32s2_thing_plus_c` and environment `esp32thing_plus`.

| Connection | ESP32 GPIO |
| --- | --- |
| XBee DOUT → ESP32 RX | 16 |
| ESP32 TX → XBee DIN | 17 |
| SD chip select | 5 |
| Camera chip select (held high) | 12 |
| SPI SCK / MISO / MOSI | 18 / 19 / 23 |

Keep the working radio pairing and transparent mode at **9600 baud, 8N1**.
USB debug output is 115200 baud. Use the same power/ground wiring as the labs.
The camera stays deselected while the SD card uses the shared SPI bus.

Open **this folder** in PlatformIO, then Build and Upload:

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
stability build. At startup the satellite USB monitor prints `KFS1 manage-1` in the current management build,
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

On Linux with a C++17 host compiler and the GCS Python dependencies installed:

```sh
python test/test_protocol.py --gcs /path/to/KestrelSAT-GCS -v
```

The tests compile the actual service header with a host filesystem adapter and
exercise it through a PTY with the real Python client. They inject dropped,
corrupted, duplicated and truncated replies and verify cancellation, partial-file
cleanup, checksum failure, empty files, and binary downloads. These checks do not
replace an over-the-air hardware test.
