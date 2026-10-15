# Lab 8: remote SD browser and file download

Upload this project to use the GCS **Remote Files** tab. Labs 4 and 5 are left
unchanged. This first milestone downloads existing files; it does not yet command
camera capture or provide live preview.

## Download this project on Windows

In this repository's **Actions** tab, open **Download Lab 8 firmware**, then open
its latest successful run. Download **KestrelSAT-Lab8-Firmware** under Artifacts.
Sign into GitHub to download artifacts. Extract the ZIP and open its
`KestrelSAT-Lab8` folder in PlatformIO; `platformio.ini` is directly inside it.
This small artifact contains only this project. Its source files are verified by
compressing/extracting them and comparing their checksums on a Windows runner.

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
