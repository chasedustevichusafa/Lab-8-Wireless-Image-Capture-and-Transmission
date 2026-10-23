// Lab 5 Arducam Mega adapter, sharing SPI with the Lab 4 SdFat card.
// The copied vendor driver is unchanged. Override its two unbounded waits here.
#pragma once
#include <Arducam_Mega.h>
#include "camera_job.h"

class CameraHardware {
public:
    void begin() {
        instance_ = this;
        ready_ = false; lastInit_ = millis(); attempted_ = true;
        digitalWrite(CAMERA_CS, HIGH);
        delay(200); // Same power-up settling time used by Lab 5.
        cameraTransaction();
        uint8_t id = 0;
        for (unsigned attempt = 0; attempt < 5; ++attempt) {
            digitalWrite(CAMERA_CS, LOW);
            SPI.transfer(0x40); SPI.transfer(0); id = SPI.transfer(0);
            digitalWrite(CAMERA_CS, HIGH);
            if (id >= 0x81 && id <= 0x87) break;
            delay(20);
        }
        SPI.endTransaction();
        if (id < 0x81 || id > 0x87) {
            fault_ = "CAMERA_NOT_FOUND";
            Serial.printf("[CAMERA] sensor=0x%02x status=%s; check Lab 5 wiring/power\n", id, fault_);
            return;
        }
        camera_ = createArducamCamera(CAMERA_CS);
        digitalWrite(CAMERA_CS, HIGH); // Vendor constructor leaves CS asserted.
        operations_ = *camera_.arducamCameraOp;
        operations_.waitI2cIdle = boundedIdle;
        operations_.setCapture = startOnly;
        camera_.arducamCameraOp = &operations_;
        camera_.lowPowerMode = 0;
        deadline_ = millis()+4000; timedOut_ = false;
        cameraTransaction(); ::begin(&camera_); SPI.endTransaction();
        ready_ = !timedOut_ && camera_.cameraId >= 0x81 && camera_.cameraId <= 0x87;
        fault_ = ready_ ? "NONE" : "CAMERA_INIT_TIMEOUT";
        Serial.printf("[CAMERA] sensor=0x%02x model=%s mask=%lu status=%s\n", camera_.cameraId,
                      model(), (unsigned long)mask(), fault_);
    }
    void retry() {
        if (!ready_ && (!attempted_ || (uint32_t)(millis()-lastInit_) >= 3000)) begin();
    }
    bool ready() const { return ready_; }
    const char* model() const { return ready_ ? camera_.myCameraInfo.cameraId : "unknown"; }
    const char* fault() const { return fault_; }
    uint32_t mask() const { return ready_ ? (uint32_t)camera_.myCameraInfo.supportResolution : 0; }
    bool start(unsigned mode) {
        Serial.printf("[CAMERA] capture mode=%u\n", mode);
        deadline_ = millis()+2000; timedOut_ = false;
        cameraTransaction();
        ::takePicture(&camera_, (CAM_IMAGE_MODE)mode, CAM_IMAGE_PIX_FMT_JPG);
        SPI.endTransaction();
        if (timedOut_) { ready_ = false; fault_ = "CAMERA_CONTROL_TIMEOUT"; }
        return !timedOut_;
    }
    int poll(uint32_t& size) {
        cameraTransaction();
        uint8_t state = camera_.arducamCameraOp->readReg(&camera_, 0x44);
        if (state == 0xff) {
            SPI.endTransaction(); ready_ = false; fault_ = "CAMERA_DISCONNECTED"; return -1;
        }
        bool complete = state & 0x04;
        if (complete) {
            size = camera_.arducamCameraOp->readFifoLength(&camera_);
            camera_.totalLength = camera_.receivedLength = size;
            camera_.burstFirstFlag = 0;
            Serial.printf("[CAMERA] fifo_bytes=%lu reader=single\n", (unsigned long)size);
        }
        SPI.endTransaction();
        return complete ? 1 : 0;
    }
    int readCamera(uint8_t* bytes, size_t n) {
        if (n > camera_.receivedLength) return -1;
        cameraTransaction();
        // Match the vendor capture2SD example: independent byte reads have no
        // resumed-burst/dummy-byte state to preserve across SD transactions.
        for (size_t i = 0; i < n; ++i) bytes[i] = ::readByte(&camera_);
        SPI.endTransaction();
        return (int)n;
    }
    void stop() {
        digitalWrite(CAMERA_CS, HIGH);
        if (ready_) {
            cameraTransaction(); camera_.arducamCameraOp->clearFifoFlag(&camera_); SPI.endTransaction();
        }
    }
    const char* reserve(const char* path, const char* temporary) {
        if (!sdReady) return "SD_UNAVAILABLE";
        if (!sd.exists("/images") && !sd.mkdir("/images")) return "DIRECTORY_UNAVAILABLE";
        if (sd.exists(path)) return "TARGET_EXISTS";
        if (sd.exists(temporary)) return "TEMP_EXISTS";
        file_ = sd.open(temporary, O_RDWR | O_CREAT | O_EXCL);
        if (!file_) return "CREATE_FAILED";
        strcpy(path_, path); strcpy(temporary_, temporary);
        Serial.printf("[CAMERA] temporary=%s target=%s\n", temporary_, path_);
        if (!file_.sync()) { cleanup(); return "SD_SYNC_FAILED"; }
        return nullptr;
    }
    bool space(uint32_t size) {
        int32_t clusters = sd.vol()->freeClusterCount();
        return clusters >= 0 && (uint64_t)clusters*sd.vol()->bytesPerCluster() >= size;
    }
    bool writeSD(const uint8_t* bytes, size_t n) {
        uint32_t offset = file_.curPosition();
        for (unsigned attempt = 1; attempt <= kfs::READ_ATTEMPTS; ++attempt) {
            if (attempt > 1) {
                delay(10);
                bool synced = file_.sync(); bool closed = file_.close();
                if (!synced || !closed) return false;
                file_ = sd.open(temporary_, O_RDWR);
            }
            if (file_ && file_.seekSet(offset) && file_.write(bytes, n) == n) return true;
            Serial.printf("[CAM_SD_WRITE] offset=%lu attempt=%u error=0x%02x\n",
                          (unsigned long)offset, attempt, sd.sdErrorCode());
        }
        return false;
    }
    bool prepareVerify(uint32_t size) {
        bool synced = file_.sync() && file_.size() == size;
        bool closed = file_.close();
        if (!synced || !closed) return false;
        file_ = sd.open(temporary_, O_RDONLY);
        verifyOffset_ = 0;
        return file_ && file_.size() == size;
    }
    int readSD(uint8_t* bytes, size_t n) {
        for (unsigned attempt = 1; attempt <= kfs::READ_ATTEMPTS; ++attempt) {
            bool positioned = true;
            if (attempt > 1) { delay(10); positioned = file_.seekSet(1) && file_.seekSet(0); }
            int got = positioned && file_.seekSet(verifyOffset_) ? file_.read(bytes, n) : -1;
            if (got == (int)n) { verifyOffset_ += n; return got; }
            Serial.printf("[CAM_SD_READ] offset=%lu wanted=%u got=%d attempt=%u error=0x%02x\n",
                          (unsigned long)verifyOffset_, (unsigned)n, got, attempt, sd.sdErrorCode());
        }
        return -1;
    }
    bool commit() {
        if (!file_.close() || sd.exists(path_) || !sd.rename(temporary_, path_)) return false;
        Serial.printf("[CAMERA] saved=%s\n", path_);
        temporary_[0] = 0;
        return true;
    }
    bool cleanup() {
        file_.close();
        bool removed = !temporary_[0] || !sd.exists(temporary_) || sd.remove(temporary_);
        Serial.printf("[CAMERA] cleanup=%u temporary=%s card=0x%02x data=0x%02x\n",
                      removed, temporary_, sd.sdErrorCode(), sd.sdErrorData());
        if (removed) temporary_[0] = 0;
        return removed;
    }
private:
    void cameraTransaction() {
        digitalWrite(SD_CS, HIGH);
        SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    }
    static void boundedIdle(ArducamCamera* camera) {
        while ((camera->arducamCameraOp->readReg(camera, 0x44) & 3) != 2) {
            if ((int32_t)(millis()-instance_->deadline_) >= 0) { instance_->timedOut_ = true; return; }
            delay(1);
        }
    }
    static void startOnly(ArducamCamera* camera) {
        if (instance_->timedOut_) return;
        camera->arducamCameraOp->clearFifoFlag(camera); camera->arducamCameraOp->startCapture(camera);
        camera->totalLength = camera->receivedLength = 0;
        camera->burstFirstFlag = 0;
    }
    static CameraHardware* instance_;
    ArducamCamera camera_ = {};
    CameraOperations operations_ = {};
    bool ready_ = false, timedOut_ = false, attempted_ = false;
    uint32_t deadline_ = 0, verifyOffset_ = 0, lastInit_ = 0;
    const char* fault_ = "CAMERA_NOT_FOUND";
    FsFile file_;
    char path_[kfs::MAX_PATH+1] = {}, temporary_[64] = {};
};
CameraHardware* CameraHardware::instance_ = nullptr;
