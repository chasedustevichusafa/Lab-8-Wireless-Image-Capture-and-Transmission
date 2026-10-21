# Lab 8: remote SD browser and file download

Upload this project to use the GCS **Remote Files** tab. Labs 4 and 5 are left
unchanged. This first milestone downloads existing files; it does not yet command
camera capture or provide live preview.

## Dated changes

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
its latest successful run. Download **KestrelSAT-Lab8-Firmware** under Artifacts.
Sign into GitHub to download artifacts. Extract the ZIP and open its
`KestrelSAT-Lab8` folder in PlatformIO; `platformio.ini` is directly inside it.
This small artifact contains only this project. Its source files are verified by
compressing/extracting them and comparing their checksums on a Windows runner.
The workflow runs automatically when this project or its packaging workflow
changes on `main`, `feature/remote-files`, `feature/high-baud-link`, or
`fix/file-transfer-stability`, including
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
stability build. At startup the satellite USB monitor prints `KFS1 stability-1`,
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

## Hardware test

Start the updated GCS executable, connect to the ground XBee COM port at 9600,
then **Remote Files → Refresh / Open**. Browse `/`, double-click folders, select a
file and **Download…**. Start with a small known text file, then a JPEG and a larger
binary file. Compare downloaded contents against the SD originals. Test Cancel
and reconnect as well. Transfers are read-only and restart rather than resume.
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
There is no write/delete command. Maximum path: 192 bytes; maximum frame buffer:
768 bytes; maximum file: 4 GiB minus one byte. Do not change card contents during
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
