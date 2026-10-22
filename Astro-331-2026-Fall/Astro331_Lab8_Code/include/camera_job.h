// Bounded, cooperative capture/save/read-back engine. No whole-image buffers.
#pragma once
#include "file_service.h"

namespace kcam {
static const uint32_t MAX_IMAGE = 8UL*1024*1024;
static const uint32_t CAPTURE_MS = 15000, JOB_MS = 120000;
inline bool imagePath(const char* path) {
    if (strncmp(path, "/images/", 8) || strchr(path+8, '/') || !kfs::uploadPath(path)) return false;
    size_t n = strlen(path);
    return n > 12 && !strcmp(path+n-4, ".jpg");
}

template<class Hardware> class Job {
public:
    explicit Job(Hardware& hardware) : hw_(hardware) {}
    bool busy() const { return phase_ == Capture || phase_ == Save || phase_ == Verify; }
    const char* start(uint32_t id, unsigned mode, const char* path, uint32_t now) {
        if (known_ && id == id_) {
            if (mode != mode_ || strcmp(path, path_)) return "JOB_MISMATCH";
            return nullptr; // Retained result, independent of file protocol reply cache.
        }
        if (busy()) return "BUSY";
        if (!hw_.ready()) return "CAMERA_UNAVAILABLE";
        if (mode >= 14 || !(hw_.mask() & (1UL << mode))) return "BAD_RESOLUTION";
        if (!imagePath(path)) return "BAD_PATH";
        char temporary[64]; snprintf(temporary, sizeof(temporary), "/images/.kfs-%08lx.part", (unsigned long)id);
        const char* error = hw_.reserve(path, temporary);
        if (error) return error;
        id_ = id; mode_ = mode; strcpy(path_, path); known_ = true;
        started_ = now; done_ = size_ = 0; crc_ = verifyCrc_ = 0xffffffffUL;
        first_[0] = first_[1] = last_[0] = last_[1] = 0;
        error_ = "NONE"; phase_ = Capture;
        if (!hw_.start(mode)) fail("CAMERA_START_FAILED");
        return nullptr;
    }
    void tick(uint32_t now) {
        if (!busy()) return;
        if ((uint32_t)(now-started_) > JOB_MS) { fail("JOB_TIMEOUT"); return; }
        uint8_t bytes[256];
        if (phase_ == Capture) {
            int result = hw_.poll(size_);
            if (result < 0) { fail("CAMERA_CAPTURE_FAILED"); return; }
            if (!result) {
                if ((uint32_t)(now-started_) > CAPTURE_MS) fail("CAMERA_TIMEOUT");
                return;
            }
            if (size_ < 4 || size_ > MAX_IMAGE) { fail("BAD_IMAGE_SIZE"); return; }
            if (!hw_.space(size_)) { fail("OUT_OF_SPACE"); return; }
            phase_ = Save;
        } else if (phase_ == Save) {
            size_t n = size_-done_; if (n > sizeof(bytes)) n = sizeof(bytes);
            if (hw_.readCamera(bytes, n) != (int)n) { fail("CAMERA_READ_FAILED"); return; }
            if (!done_) { first_[0] = bytes[0]; first_[1] = bytes[1]; }
            for (size_t i = 0; i < n; ++i) { last_[0] = last_[1]; last_[1] = bytes[i]; }
            if (!hw_.writeSD(bytes, n)) { fail("SD_WRITE_FAILED"); return; }
            crc_ = kfs::crcUpdate(crc_, bytes, n); done_ += n;
            if (done_ == size_) {
                if (first_[0] != 0xff || first_[1] != 0xd8 || last_[0] != 0xff || last_[1] != 0xd9) {
                    fail("INVALID_JPEG"); return;
                }
                if (!hw_.prepareVerify(size_)) { fail("SD_SYNC_FAILED"); return; }
                phase_ = Verify; done_ = 0;
            }
        } else if (phase_ == Verify) {
            size_t n = size_-done_; if (n > sizeof(bytes)) n = sizeof(bytes);
            if (hw_.readSD(bytes, n) != (int)n) { fail("SD_VERIFY_READ_FAILED"); return; }
            verifyCrc_ = kfs::crcUpdate(verifyCrc_, bytes, n); done_ += n;
            if (done_ == size_) {
                if (verifyCrc_ != crc_) { fail("VERIFY_CRC_MISMATCH"); return; }
                if (!hw_.commit()) { fail("PUBLISH_FAILED"); return; }
                phase_ = Saved; hw_.stop();
            }
        }
    }
    void abort(uint32_t id) {
        if (known_ && id == id_ && busy()) {
            bool cleaned = hw_.cleanup(); hw_.stop();
            phase_ = Cancelled; error_ = cleaned ? "NONE" : "CLEANUP_FAILED";
        }
    }
    void status(uint32_t id, char* body, size_t capacity) const {
        if (!known_ || id != id_) { snprintf(body, capacity, "CAMJOB|%08lx|UNKNOWN|0|0|00000000||UNKNOWN", (unsigned long)id); return; }
        char encoded[kfs::MAX_PATH*2+1];
        kfs::hexEncode((const uint8_t*)path_, strlen(path_), encoded);
        const char* states[] = {"UNKNOWN", "CAPTURING", "SAVING", "VERIFYING", "SAVED", "ERROR", "CANCELLED"};
        snprintf(body, capacity, "CAMJOB|%08lx|%s|%lu|%lu|%08lx|%s|%s", (unsigned long)id_, states[phase_],
                 (unsigned long)done_, (unsigned long)size_, (unsigned long)(crc_^0xffffffffUL), encoded, error_);
    }
private:
    void fail(const char* error) {
        bool cleaned = hw_.cleanup(); hw_.stop();
        if (!cleaned) { snprintf(errorText_, sizeof(errorText_), "%s_CLEANUP_FAILED", error); error_ = errorText_; }
        else error_ = error;
        phase_ = Error;
    }
    enum Phase {Unknown, Capture, Save, Verify, Saved, Error, Cancelled};
    Hardware& hw_;
    Phase phase_ = Unknown;
    bool known_ = false;
    uint32_t id_ = 0, started_ = 0, done_ = 0, size_ = 0, crc_ = 0, verifyCrc_ = 0;
    unsigned mode_ = 0;
    char path_[kfs::MAX_PATH+1] = {};
    const char* error_ = "NONE";
    char errorText_[80] = {};
    uint8_t first_[2] = {}, last_[2] = {};
};
} // namespace kcam
