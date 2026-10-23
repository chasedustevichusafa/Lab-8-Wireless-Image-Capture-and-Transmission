// Lab 8: SD browser and verified file uploads/downloads over the Lab 4 XBee.
// Remote single JPEG captures use the Lab 5 camera on the shared SPI bus.
#include <Arduino.h>
#include <SPI.h>
#include <SdFat.h>
#include <esp_system.h>
#include "file_service.h"

// START HERE: the satellite has four cooperating layers.
// 1. setup()/loop() configure peripherals and collect newline-delimited UART
//    commands without waiting for a ground station to be connected.
// 2. kfs::Service validates CRC/fields and manages one upload/download session.
// 3. SdBackend below translates those abstract operations to actual SdFat calls.
// 4. CameraHardware + kcam::Job capture, frame, save and reread a JPEG in small
//    steps. The loop keeps servicing status commands between camera steps.
//
// The satellite XBee is TRANSPARENT (AP=0): this UART carries ordinary KFS text.
// XBee API framing is handled by the ground radio/GCS, not by this firmware.
// UART baud is the MCU-to-radio speed, not the radio's over-the-air bitrate.
// Radio destination, AP and BD settings are configured separately in XCTU.
// USB Serial is a separate diagnostic channel and stays at 115200 in all builds.

#ifndef XBEE_UART_BAUD
#define XBEE_UART_BAUD 9600
#endif
#ifndef SD_SPI_MHZ
#define SD_SPI_MHZ 25
#endif
static_assert(SD_SPI_MHZ >= 1 && SD_SPI_MHZ <= 25, "SD SPI test clock must be 1..25 MHz");
static_assert(XBEE_UART_BAUD == 9600 || XBEE_UART_BAUD == 115200,
              "Select a documented XBee UART baud rate");

#ifndef CAMERA_CS_PIN
#define CAMERA_CS_PIN 12
#endif
// Keep the working lab board identifier even though its historical name contains
// "esp32s2": PlatformIO's actual board definition targets this ESP32 hardware.
// The environments select GPIO12 (original lab) or GPIO27 (confirmed camera
// wiring). GPIO12 is also a boot strap and can affect flashing before setup()
// runs; an application-level digitalWrite cannot change the ROM's reset sample.
static const uint8_t SD_CS = 5, CAMERA_CS = CAMERA_CS_PIN;
static const int SPI_SCK_PIN = 18, SPI_MISO_PIN = 19, SPI_MOSI_PIN = 23;
static const int XBEE_RX = 16, XBEE_TX = 17;
HardwareSerial Xbee(2);
SdFat sd;
bool sdReady = false;
#include "camera_hardware.h"
CameraHardware cameraHardware;
kcam::Job<CameraHardware> cameraJob(cameraHardware);

class SdBackend {
public:
    bool cameraBusy() const { return cameraJob.busy(); }
    void cameraInfo(char* body, size_t n) {
        // Refresh can recover late camera power without resetting a working link.
        if (!serviceBusyForCamera() && !cameraHardware.ready()) cameraHardware.retry();
        snprintf(body, n, "CAMERA|%s|%lu|%s|%s", cameraHardware.model(),
                 (unsigned long)cameraHardware.mask(), cameraHardware.fault(), sdReady ? "SD_READY" : "SD_UNAVAILABLE");
    }
    const char* cameraStart(uint32_t id, unsigned mode, const char* path) { return cameraJob.start(id, mode, path, millis()); }
    void cameraStatus(uint32_t id, char* body, size_t n) { cameraJob.status(id, body, n); }
    void cameraDiagnostics(uint32_t id, char* body, size_t n) { cameraJob.diagnostics(id, body, n); }
    void cameraAbort(uint32_t id) { cameraJob.abort(id); }
    int list(const char* path, uint32_t index, kfs::Entry& entry) {
        // Reopen/scan each LIST request instead of keeping a directory iterator
        // alive across a disconnect or cancelled browsing operation. The wire
        // index is a protocol cursor, not a raw FAT directory offset. This is
        // deliberately simple; changing card contents during a listing can
        // change the enumeration and should be avoided.
        if (!sdReady) return -1;
        FsFile dir = sd.open(path, O_RDONLY);
        if (!dir || !dir.isDirectory()) { dir.close(); return -1; }
        FsFile item;
        uint32_t current = 0;
        while (item.openNext(&dir, O_RDONLY)) {
            if (current++ == index) {
                size_t n = item.getName(entry.name, sizeof(entry.name));
                uint64_t size = item.size();
                if (!n || n >= sizeof(entry.name)-1 || size > UINT32_MAX) {
                    item.close(); dir.close(); return -1;
                }
                entry.directory = item.isDirectory();
                entry.size = entry.directory ? 0 : (uint32_t)size;
                item.close(); dir.close(); return 1;
            }
            item.close();
        }
        bool failed = dir.getError();
        dir.close();
        return failed ? -1 : 0;
    }
    bool open(const char* path, uint32_t& size) {
        // Service owns session identity; this adapter owns the actual file.
        // Reject directories and sizes outside the 32-bit wire representation.
        // File transfer state is not resumed automatically after a reset.
        close();
        if (!sdReady) return false;
        file_ = sd.open(path, O_RDONLY);
        if (!file_ || file_.isDirectory() || file_.size() > UINT32_MAX) { close(); return false; }
        size = (uint32_t)file_.size();
        return true;
    }
    int readAt(uint32_t offset, uint8_t* bytes, size_t n, uint8_t attempt) {
        // SD read/seek failure and RF reply loss are different layers. The
        // service calls this again for a local SD attempt, whereas an identical
        // radio request is replayed by the service without another file read.
        // Restoration here rebuilds file-position/FAT traversal state; it is
        // not a complete card power-cycle or guaranteed electrical recovery.
        uint32_t started = micros();
        // A failed read can partially advance both the position and FAT chain
        // state. Rebuild that state before retrying the entire same chunk.
        bool positioned = true;
        if (attempt > 1) {
            delay(10);
            positioned = file_.seekSet(1) && file_.seekSet(0);
        }
        positioned = positioned && file_.seekSet(offset);
        int got = positioned ? file_.read(bytes, n) : -1;
        // Preserve every failure/retry but rate-limit healthy checkpoints.
        // These USB messages do not contaminate the radio's KFS response stream.
        // getError() may remain latched after a later successful read, so inspect
        // got/attempt/timing together with card/data error codes.
        if (got != (int)n || attempt > 1 || millis()-lastReport_ >= 1000) {
            Serial.printf("[SD_READ] ms=%lu offset=%lu wanted=%u got=%d attempt=%u us=%lu card=0x%02x data=0x%02x file_latched=0x%02x\n",
                          (unsigned long)millis(), (unsigned long)offset, (unsigned)n, got,
                          (unsigned)attempt, (unsigned long)(micros()-started),
                          sd.sdErrorCode(), sd.sdErrorData(), file_.getError());
            lastReport_ = millis();
        }
        return got;
    }
    const char* beginUpload(const char* target, const char* temporary, uint32_t size) {
        // Validate the parent, available allocation units and both names before
        // creating an exclusive temporary. Free-space arithmetic is widened to
        // avoid overflow. An existing final file is never opened for writing.
        if (!sdReady) return "SD_UNAVAILABLE";
        if (sd.exists(target)) return "TARGET_EXISTS";
        char parent[kfs::MAX_PATH+1]; strcpy(parent, target);
        char* slash = strrchr(parent, '/');
        if (slash == parent) slash[1] = 0; else *slash = 0;
        FsFile dir = sd.open(parent, O_RDONLY);
        bool valid = dir && dir.isDirectory(); dir.close();
        if (!valid) return "DIRECTORY_UNAVAILABLE";
        int32_t clusters = sd.vol()->freeClusterCount();
        if (clusters < 0) return "SD_UNAVAILABLE";
        if ((uint64_t)clusters * sd.vol()->bytesPerCluster() < size) return "OUT_OF_SPACE";
        if (sd.exists(temporary)) return "TEMP_EXISTS";
        file_ = sd.open(temporary, O_RDWR | O_CREAT | O_EXCL);
        if (!file_) return "CREATE_FAILED";
        strcpy(target_, target); strcpy(temporary_, temporary);
        Serial.printf("[UPLOAD] temporary=%s target=%s bytes=%lu\n", temporary_, target_, (unsigned long)size);
        return nullptr;
    }
    int writeAt(uint32_t offset, const uint8_t* bytes, size_t n, uint8_t attempt) {
        // Upload ACK means the whole requested range was written AND synced.
        // Retry uses the original offset and rebuilds state by reopening only
        // this session's .part, rather than appending a duplicate partial block.
        uint32_t started = micros();
        bool positioned = true;
        if (attempt > 1) {
            delay(10);
            // Reopen only this session's exclusive temporary file to rebuild
            // FAT state after a failed allocation/write, including at offset 0.
            if (file_) {
                bool synced = file_.sync();
                bool closed = file_.close();
                positioned = synced && closed;
            }
            if (positioned) file_ = sd.open(temporary_, O_RDWR);
            positioned = positioned && file_;
        }
        positioned = positioned && file_.seekSet(offset);
        int got = positioned ? (int)file_.write(bytes, n) : -1;
        if (got == (int)n && !file_.sync()) got = -1;
        if (got != (int)n || attempt > 1 || millis()-lastReport_ >= 1000) {
            Serial.printf("[SD_WRITE] ms=%lu offset=%lu wanted=%u got=%d attempt=%u us=%lu card=0x%02x data=0x%02x file_latched=0x%02x\n",
                          (unsigned long)millis(), (unsigned long)offset, (unsigned)n, got,
                          (unsigned)attempt, (unsigned long)(micros()-started),
                          sd.sdErrorCode(), sd.sdErrorData(), file_.getError());
            lastReport_ = millis();
        }
        return got;
    }
    bool prepareVerify(uint32_t size) {
        // Switch the one transfer file from writer to independent reader only
        // after sync, length and close succeed. The service then hashes bounded
        // read-back batches before it permits commitUpload().
        if (!file_.sync() || file_.size() != size) return false;
        if (!file_.close()) return false;
        file_ = sd.open(temporary_, O_RDONLY);
        return file_ && file_.size() == size;
    }
    bool commitUpload() {
        if (!file_.close() || sd.exists(target_)) return false;
        if (!sd.rename(temporary_, target_)) return false;
        Serial.printf("[UPLOAD] stored=%s\n", target_);
        temporary_[0] = target_[0] = 0;
        return true;
    }
    bool abortUpload() {
        file_.close();
        bool removed = !temporary_[0] || !sd.exists(temporary_) || sd.remove(temporary_);
        Serial.printf("[UPLOAD] abort temporary=%s removed=%u\n", temporary_, removed);
        temporary_[0] = target_[0] = 0;
        return removed;
    }
    const char* deleteFile(const char* path) {
        // Delete/rename are file-only operations. Never recursively mutate a
        // directory. Closing the inspection handle before the mutation avoids
        // leaving a handle pointing to a removed/renamed directory entry.
        if (!sdReady) return "SD_UNAVAILABLE";
        FsFile file = sd.open(path, O_RDONLY);
        if (!file) return "FILE_UNAVAILABLE";
        bool directory = file.isDirectory();
        bool closed = file.close();
        if (directory) return "NOT_A_FILE";
        if (!closed) return "SD_ERROR";
        if (!sd.remove(path)) return "DELETE_FAILED";
        Serial.printf("[FILE] deleted=%s\n", path);
        return nullptr;
    }
    const char* renameFile(const char* path, const char* target) {
        if (!sdReady) return "SD_UNAVAILABLE";
        FsFile file = sd.open(path, O_RDONLY);
        if (!file) return "FILE_UNAVAILABLE";
        bool directory = file.isDirectory();
        bool closed = file.close();
        if (directory) return "NOT_A_FILE";
        if (!closed) return "SD_ERROR";
        if (sd.exists(target)) return "TARGET_EXISTS";
        if (!sd.rename(path, target)) return "RENAME_FAILED";
        Serial.printf("[FILE] renamed=%s target=%s\n", path, target);
        return nullptr;
    }
    bool cleanPartial(const char* path) {
        // CLEAN is explicitly for recognizable reserved temporaries left by an
        // interrupted job. Ordinary images must use the separately confirmed
        // Delete operation in the GCS, never this recovery command.
        if (!kfs::partialPath(path)) return false;
        FsFile file = sd.open(path, O_RDONLY);
        bool valid = file && !file.isDirectory(); file.close();
        return valid && sd.remove(path);
    }
    void close() { file_.close(); }
private:
    bool serviceBusyForCamera() const;
    FsFile file_;
    uint32_t lastReport_ = 0;
    char target_[kfs::MAX_PATH+1] = {}, temporary_[kfs::MAX_PATH+1] = {};
};
SdBackend backend;
kfs::Service<SdBackend> service(backend);
bool SdBackend::serviceBusyForCamera() const { return service.active() || cameraJob.busy(); }
char line[kfs::MAX_LINE];
size_t used = 0;
bool overflow = false;
uint32_t lastByte = 0, lastProtocol = 0, lastHeartbeat = 0;
bool protocolQuiet = false;

void setup() {
    // No while(!Serial): a satellite powered without USB must still start its
    // radio and file service. The larger UART receive ring absorbs short bursts
    // while the single loop is occupied by bounded camera/SD work.
    Serial.begin(115200); // Never wait for USB: the satellite must boot untethered.
    Xbee.setRxBufferSize(2048);
    Xbee.begin(XBEE_UART_BAUD, SERIAL_8N1, XBEE_RX, XBEE_TX);
    Serial.printf("[INFO] KFS1 camera-2 reset_reason=%d SD_SPI_MHZ=%u\n",
                  (int)esp_reset_reason(), (unsigned)SD_SPI_MHZ);
    Serial.printf("[INFO] XBee UART: %lu baud\n", (unsigned long)XBEE_UART_BAUD);
    Serial.printf("[INFO] camera_cs=%u (GPIO12 is an ESP32 boot strap)\n", CAMERA_CS);
    // Park BOTH SPI devices inactive before initializing either one. Shared
    // wires are safe only if the unaddressed device releases MISO. SHARED_SPI
    // below allows camera transactions to use another clock on the same bus.
    pinMode(CAMERA_CS, OUTPUT); digitalWrite(CAMERA_CS, HIGH);
    pinMode(SD_CS, OUTPUT); digitalWrite(SD_CS, HIGH);
    pinMode(LED_BUILTIN, OUTPUT);
    SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SD_CS);
    sdReady = sd.begin(SdSpiConfig(SD_CS, SHARED_SPI, SD_SCK_MHZ(SD_SPI_MHZ), &SPI));
    Serial.println(sdReady ? "[INFO] SD ready" : "[ERROR] SD unavailable; check card and reset");
    if (!sdReady) Serial.printf("[SD_INIT] card=0x%02x data=0x%02x\n", sd.sdErrorCode(), sd.sdErrorData());
    cameraHardware.begin();
    Serial.println("[USB] SDCHECK /absolute/path : three local read/CRC passes; disconnect GCS first");
    Xbee.println(sdReady ? "[INFO] Lab 8 ready - open Remote Files in GCS" :
                           "[ERROR] SD unavailable; check card and reset");
}

void processLine() {
    // The UART collector supplies one complete NUL-terminated line. Protocol
    // responses are consumed immediately because Service reuses its response
    // buffer on the next request. Accepted protocol traffic starts a quiet
    // interval so heartbeat text cannot become unexpected transfer data.
    line[used] = 0;
    const char* response = service.handle(line);
    if (response) {
        lastProtocol = millis(); protocolQuiet = true;
        if (strstr(response, "|ERR|")) Serial.printf("[KFS_ERROR] ms=%lu response=%s", (unsigned long)millis(), response);
        Xbee.write((const uint8_t*)response, strlen(response));
    } else if (!strcmp(line, "1")) {
        Xbee.println("1 help; 3 toggle LED; use GCS Remote Files for SD browsing/downloads");
    } else if (!strcmp(line, "3")) {
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
        Xbee.println(digitalRead(LED_BUILTIN) ? "LED on" : "LED off");
    }
}

// USB-only diagnostic: bypasses the radio and protocol to expose raw SD errors.
void sdCheck(const char* requested) {
    // This intentionally occupies the loop while it scans: disconnect the GCS
    // first. It performs three fresh local reads without radio transfer or the
    // normal readAt recovery retries, exposing a useful SD-only baseline.
    // Matching CRCs test repeatable bytes, not JPEG decoding or RF delivery.
    if (service.active() || cameraJob.busy()) { Serial.println("[SDCHECK] busy: close the transfer or reset first"); return; }
    size_t length = strlen(requested);
    char hex[kfs::MAX_PATH*2+1], path[kfs::MAX_PATH+1];
    if (length > kfs::MAX_PATH) { Serial.println("[SDCHECK] path too long"); return; }
    kfs::hexEncode((const uint8_t*)requested, length, hex);
    if (!kfs::decodePath(hex, path)) { Serial.println("[SDCHECK] use a canonical absolute path"); return; }
    if (!sdReady) { Serial.println("[SDCHECK] SD unavailable"); return; }
    uint32_t reference = 0;
    bool haveReference = false;
    for (unsigned pass = 1; pass <= 3; ++pass) {
        FsFile file = sd.open(path, O_RDONLY);
        if (!sdReady || !file || file.isDirectory() || file.size() > UINT32_MAX) {
            Serial.println("[SDCHECK] file unavailable"); file.close(); return;
        }
        uint32_t size = (uint32_t)file.size(), offset = 0, crc = 0xffffffffUL;
        uint32_t started = millis(), report = started;
        uint8_t bytes[kfs::CHUNK];
        while (offset < size) {
            size_t n = size-offset < sizeof(bytes) ? size-offset : sizeof(bytes);
            int got = file.read(bytes, n);
            if (got != (int)n) {
                Serial.printf("[SDCHECK] FAIL pass=%u offset=%lu wanted=%u got=%d card=0x%02x data=0x%02x file_latched=0x%02x\n",
                              pass, (unsigned long)offset, (unsigned)n, got,
                              sd.sdErrorCode(), sd.sdErrorData(), file.getError());
                break;
            }
            crc = kfs::crcUpdate(crc, bytes, n); offset += n;
            if (millis()-report >= 1000) {
                Serial.printf("[SDCHECK] pass=%u offset=%lu/%lu\n", pass, (unsigned long)offset, (unsigned long)size);
                report = millis();
            }
            yield();
        }
        if (offset == size) {
            crc ^= 0xffffffffUL;
            Serial.printf("[SDCHECK] %s pass=%u bytes=%lu crc32=%08lx ms=%lu\n",
                          haveReference && reference != crc ? "CRC_CHANGED" : "PASS", pass,
                          (unsigned long)size, (unsigned long)crc, (unsigned long)(millis()-started));
            if (!haveReference) { reference = crc; haveReference = true; }
        }
        file.close();
    }
    Serial.println("[SDCHECK] finished");
}

void usbCommands() {
    // USB and XBee have separate parsers. Both enforce capacity limits and drop
    // an overlong command until its newline instead of interpreting its tail
    // as another valid command. Per-pass byte budgets prevent UART work from
    // indefinitely starving the camera state machine.
    static char command[kfs::MAX_PATH+12];
    static size_t length = 0;
    static bool overflowed = false;
    for (unsigned budget = 0; budget < 256 && Serial.available(); ++budget) {
        char c = Serial.read();
        if (c == '\n') {
            command[length] = 0;
            if (overflowed) Serial.println("[USB] command too long");
            else if (!strncmp(command, "SDCHECK ", 8)) sdCheck(command+8);
            else if (length) Serial.println("[USB] use SDCHECK /absolute/path");
            length = 0; overflowed = false;
        } else if (c != '\r' && !overflowed) {
            if (length+1 < sizeof(command)) command[length++] = c;
            else overflowed = true;
        }
    }
}

void loop() {
    // Single-threaded scheduling order is intentional: inspect USB, drain a
    // bounded amount of radio input, advance one camera step, expire inactive
    // transfers, then optionally emit an idle heartbeat. There is no live
    // preview thread or full-image buffer competing with file-transfer reads.
    usbCommands();
    uint32_t now = millis();
    // A truncated request must not poison the next command indefinitely.
    if ((used || overflow) && (uint32_t)(now-lastByte) > 2000) {
        Serial.printf("[UART] truncated request bytes=%u overflow=%u\n", (unsigned)used, overflow);
        used = 0; overflow = false;
    }
    for (int budget = 0; budget < 512 && Xbee.available(); ++budget) {
        char c = (char)Xbee.read(); lastByte = millis();
        if (c == '\n') {
            if (!overflow && used) processLine();
            used = 0; overflow = false;
        } else if (c != '\r' && !overflow) {
            if (used < sizeof(line)-1) line[used++] = c;
            else overflow = true;
        }
    }
    cameraJob.tick(millis());
    now = millis();
    // The 30-second session cleanup is inactivity based, not a maximum file
    // duration. A large healthy transfer refreshes lastProtocol on every reply.
    // Camera job retention is independent and is not erased by Service expiry.
    if (protocolQuiet && (uint32_t)(now-lastProtocol) > 30000) {
        if (service.active()) Serial.println("[KFS_EXPIRE] no protocol activity for 30 seconds");
        service.expire(); protocolQuiet = false;
    }
    // Suppress unsolicited radio text while a protocol session is active.
    if (!protocolQuiet && !cameraJob.busy() && !used && (uint32_t)(now-lastHeartbeat) >= 2000) {
        lastHeartbeat = now;
        Xbee.println(".");
    }
    delay(1);
}
