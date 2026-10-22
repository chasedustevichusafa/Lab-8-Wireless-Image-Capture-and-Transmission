// Exercise the production adapter and unchanged vendor C driver with a fake bus.
// This verifies commands, CS isolation and bounded recovery, not electrical timing.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "Arducam/ArducamSpi.h"

static const uint8_t CAMERA_CS = 12, SD_CS = 5;
static const int HIGH = 1, LOW = 0, MSBFIRST = 1, SPI_MODE0 = 0;
static uint32_t clockMs = 0, busyUntil = 0;
static bool powered = false, stuck = false, selected = false, inTransaction = false;
static uint8_t command = 0;
static unsigned step = 0, singles = 0, bursts = 0;
static std::vector<uint8_t> fifo;
static size_t offset = 0;
uint32_t millis() { return clockMs; }
void delay(unsigned ms) { clockMs += ms; }
void digitalWrite(int pin, int value) {
    if (pin == CAMERA_CS) { selected = value == LOW; step = 0; }
    else if (pin == SD_CS) assert(value == HIGH);
}
struct SPISettings { SPISettings(int, int, int) {} };
struct FakeSPI {
    void beginTransaction(SPISettings) { assert(!inTransaction); inTransaction = true; }
    void endTransaction() { assert(inTransaction && !selected); inTransaction = false; }
    uint8_t transfer(uint8_t data) {
        assert(inTransaction && selected);
        if (step++ == 0) {
            command = data;
            if (data == 0x3d) ++singles;
            if (data == 0x3c) ++bursts;
            return 0;
        }
        if (!powered) return 0xff;
        if (command == 0x3d && step == 3) { assert(offset < fifo.size()); return fifo[offset++]; }
        if (command & 0x80) return 0;
        if (command == 0x40) return 0x82;
        if (command == 0x44) return stuck || clockMs < busyUntil ? 0 : 6;
        if (command >= 0x45 && command <= 0x47) return (fifo.size() >> (8*(command-0x45))) & 255;
        return 0;
    }
} SPI;
struct FakeSerial { template<class... Args> void printf(const char*, Args...) {} } Serial;
static const int O_RDWR = 1, O_CREAT = 2, O_EXCL = 4, O_RDONLY = 8;
struct FsFile {
    explicit operator bool() const { return false; }
    bool sync() { return true; }
    bool close() { return true; }
    bool seekSet(uint32_t) { return true; }
    uint32_t curPosition() { return 0; }
    uint32_t size() { return 0; }
    size_t write(const uint8_t*, size_t n) { return n; }
    int read(uint8_t*, size_t) { return -1; }
};
struct FakeVolume {
    int32_t freeClusterCount() { return 1000; }
    uint32_t bytesPerCluster() { return 4096; }
};
struct FakeSD {
    bool exists(const char*) { return false; }
    bool mkdir(const char*) { return true; }
    FsFile open(const char*, int) { return FsFile(); }
    bool rename(const char*, const char*) { return true; }
    bool remove(const char*) { return true; }
    uint8_t sdErrorCode() { return 0; }
    uint8_t sdErrorData() { return 0; }
    FakeVolume* vol() { return &volume; }
    FakeVolume volume;
} sd;
bool sdReady = true;
extern "C" {
void arducamSpiBegin() {}
uint8_t arducamSpiTransfer(uint8_t data) { return SPI.transfer(data); }
void arducamSpiCsOutputMode(int) {}
void arducamSpiCsHigh(int pin) { digitalWrite(pin, HIGH); }
void arducamSpiCsLow(int pin) { digitalWrite(pin, LOW); }
void arducamDelayMs(uint16_t ms) { delay(ms); }
void arducamDelayUs(uint16_t) {}
}
#include "camera_hardware.h"

int main() {
    CameraHardware camera;
    camera.begin(); assert(!camera.ready());
    // Late power recovers through Refresh's retry hook, without reboot.
    powered = true; clockMs += 3000; busyUntil = clockMs+1500;
    camera.retry(); assert(camera.ready());
    assert(!strcmp(camera.model(), "3MP"));
    fifo = {0, 0xff, 0xd8, 1, 2, 3, 0xff, 0xd9, 0, 0};
    assert(camera.start(3));
    uint32_t size = 0; assert(camera.poll(size) == 1 && size == fifo.size());
    uint8_t bytes[16] = {};
    assert(camera.readCamera(bytes, 4) == 4);
    assert(!selected && !inTransaction);
    // An intervening SD transaction must not alter the camera reader's state.
    SPI.beginTransaction(SPISettings(25000000, MSBFIRST, SPI_MODE0)); SPI.endTransaction();
    assert(camera.readCamera(bytes+4, size-4) == (int)size-4);
    assert(!memcmp(bytes, fifo.data(), size));
    assert(camera.readCamera(bytes, 1) == -1);
    assert(singles == size && bursts == 0);
    camera.stop(); assert(!selected && !inTransaction);
    powered = false; assert(camera.poll(size) == -1 && !camera.ready());
    powered = true; stuck = true; clockMs += 3000;
    uint32_t before = clockMs;
    camera.retry(); assert(!camera.ready() && clockMs-before <= 4300);
    stuck = false; clockMs += 3000; camera.retry(); assert(camera.ready());
    puts("Camera adapter: vendor single-byte reads, CS isolation, late power and timeout recovery passed.");
}
