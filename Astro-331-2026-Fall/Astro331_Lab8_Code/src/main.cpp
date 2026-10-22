// Lab 8: SD browser and verified file uploads/downloads over the Lab 4 XBee.
// Remote single JPEG captures use the Lab 5 camera on the shared SPI bus.
#include <Arduino.h>
#include <SPI.h>
#include <SdFat.h>
#include <esp_system.h>
#include "file_service.h"

#ifndef XBEE_UART_BAUD
#define XBEE_UART_BAUD 9600
#endif
#ifndef SD_SPI_MHZ
#define SD_SPI_MHZ 25
#endif
static_assert(SD_SPI_MHZ >= 1 && SD_SPI_MHZ <= 25, "SD SPI test clock must be 1..25 MHz");
static_assert(XBEE_UART_BAUD == 9600 || XBEE_UART_BAUD == 115200,
              "Select a documented XBee UART baud rate");

static const uint8_t SD_CS = 5, CAMERA_CS = 12;
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
        snprintf(body, n, "CAMERA|%s|%lu|%s|%s", cameraHardware.model(),
                 (unsigned long)cameraHardware.mask(), cameraHardware.fault(), sdReady ? "SD_READY" : "SD_UNAVAILABLE");
    }
    const char* cameraStart(uint32_t id, unsigned mode, const char* path) { return cameraJob.start(id, mode, path, millis()); }
    void cameraStatus(uint32_t id, char* body, size_t n) { cameraJob.status(id, body, n); }
    void cameraAbort(uint32_t id) { cameraJob.abort(id); }
    int list(const char* path, uint32_t index, kfs::Entry& entry) {
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
        close();
        if (!sdReady) return false;
        file_ = sd.open(path, O_RDONLY);
        if (!file_ || file_.isDirectory() || file_.size() > UINT32_MAX) { close(); return false; }
        size = (uint32_t)file_.size();
        return true;
    }
    int readAt(uint32_t offset, uint8_t* bytes, size_t n, uint8_t attempt) {
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
        if (!kfs::partialPath(path)) return false;
        FsFile file = sd.open(path, O_RDONLY);
        bool valid = file && !file.isDirectory(); file.close();
        return valid && sd.remove(path);
    }
    void close() { file_.close(); }
private:
    FsFile file_;
    uint32_t lastReport_ = 0;
    char target_[kfs::MAX_PATH+1] = {}, temporary_[kfs::MAX_PATH+1] = {};
};
SdBackend backend;
kfs::Service<SdBackend> service(backend);
char line[kfs::MAX_LINE];
size_t used = 0;
bool overflow = false;
uint32_t lastByte = 0, lastProtocol = 0, lastHeartbeat = 0;
bool protocolQuiet = false;

void setup() {
    Serial.begin(115200); // Never wait for USB: the satellite must boot untethered.
    Xbee.setRxBufferSize(2048);
    Xbee.begin(XBEE_UART_BAUD, SERIAL_8N1, XBEE_RX, XBEE_TX);
    Serial.printf("[INFO] KFS1 camera-1 reset_reason=%d SD_SPI_MHZ=%u\n",
                  (int)esp_reset_reason(), (unsigned)SD_SPI_MHZ);
    Serial.printf("[INFO] XBee UART: %lu baud\n", (unsigned long)XBEE_UART_BAUD);
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
