// Lab 8: read-only SD browser and reliable file downloads over the Lab 4 XBee.
// Camera preview is a later milestone; keep the camera deselected on shared SPI.
#include <Arduino.h>
#include <SPI.h>
#include <SdFat.h>
#include "file_service.h"

static const uint8_t SD_CS = 5, CAMERA_CS = 12;
static const int SPI_SCK_PIN = 18, SPI_MISO_PIN = 19, SPI_MOSI_PIN = 23;
static const int XBEE_RX = 16, XBEE_TX = 17;
HardwareSerial Xbee(2);
SdFat sd;
bool sdReady = false;

class SdBackend {
public:
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
    int read(uint8_t* bytes, size_t n) { return file_.read(bytes, n); }
    void close() { file_.close(); }
private:
    FsFile file_;
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
    Xbee.setRxBufferSize(1024);
    Xbee.begin(9600, SERIAL_8N1, XBEE_RX, XBEE_TX);
    pinMode(CAMERA_CS, OUTPUT); digitalWrite(CAMERA_CS, HIGH);
    pinMode(SD_CS, OUTPUT); digitalWrite(SD_CS, HIGH);
    pinMode(LED_BUILTIN, OUTPUT);
    SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SD_CS);
    sdReady = sd.begin(SdSpiConfig(SD_CS, SHARED_SPI, SD_SCK_MHZ(25), &SPI));
    Serial.println(sdReady ? "[INFO] SD ready" : "[ERROR] SD unavailable; check card and reset");
    Xbee.println(sdReady ? "[INFO] Lab 8 ready - open Remote Files in GCS" :
                           "[ERROR] SD unavailable; check card and reset");
}

void processLine() {
    line[used] = 0;
    const char* response = service.handle(line);
    if (response) {
        lastProtocol = millis(); protocolQuiet = true;
        Xbee.write((const uint8_t*)response, strlen(response));
    } else if (!strcmp(line, "1")) {
        Xbee.println("1 help; 3 toggle LED; use GCS Remote Files for SD browsing/downloads");
    } else if (!strcmp(line, "3")) {
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
        Xbee.println(digitalRead(LED_BUILTIN) ? "LED on" : "LED off");
    }
}

void loop() {
    uint32_t now = millis();
    // A truncated request must not poison the next command indefinitely.
    if ((used || overflow) && (uint32_t)(now-lastByte) > 2000) { used = 0; overflow = false; }
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
    now = millis();
    if (protocolQuiet && (uint32_t)(now-lastProtocol) > 30000) {
        service.expire(); protocolQuiet = false;
    }
    // Suppress unsolicited radio text while a protocol session is active.
    if (!protocolQuiet && !used && (uint32_t)(now-lastHeartbeat) >= 2000) {
        lastHeartbeat = now;
        Xbee.println(".");
    }
    delay(1);
}
