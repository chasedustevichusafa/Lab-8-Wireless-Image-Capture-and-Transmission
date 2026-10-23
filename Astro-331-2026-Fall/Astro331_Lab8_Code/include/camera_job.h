// Bounded, cooperative capture/save/read-back engine. No whole-image buffers.
#pragma once
#include "file_service.h"
#include "jpeg_stream.h"

namespace kcam {
// This is a cooperative state machine, not a camera thread. The Arduino loop
// calls tick() repeatedly between UART service passes. Each save/verify tick
// handles at most one small buffer, allowing CAMSTATUS and other permitted
// commands to be serviced while a picture is being processed.
//
// Resource lifetime:
//   start/reserve -> Capture -> Save -> Verify -> Saved (published .jpg)
//                           any failure -> Error (attempt .part cleanup)
//                           explicit abort -> Cancelled (attempt cleanup)
// The Hardware adapter owns SPI and the open SD temporary file. This class owns
// only bounded buffers, counters, parsing state and the last capture's identity.
// No radio connection is required for an accepted capture to finish on SD.
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
        // A lost CAPTURE acknowledgment must not expose the sensor twice. The
        // last ID is checked BEFORE busy/hardware/path checks so an exact retry
        // can recover a retained terminal result, even after its image was
        // deleted. Reusing that ID with a changed destination/mode is an error.
        // Retention is only the latest job in RAM and does not survive reset.
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
        // Keep transport and saved-image counts separate. A FIFO may include
        // bytes before SOI or after EOI. During Save, done_/size_ describe FIFO
        // consumption; at Verify they switch to the extracted JPEG's size.
        // CAMDIAG retains independent counters for interpreting that transition.
        started_ = now; done_ = size_ = 0; crc_ = verifyCrc_ = 0xffffffffUL;
        fifoSize_ = read_ = saved_ = prefix_ = 0; jpeg_ = JpegStream();
        memset(first_, 0, sizeof(first_)); memset(last_, 0, sizeof(last_));
        error_ = "NONE"; phase_ = Capture;
        if (!hw_.start(mode)) fail("CAMERA_START_FAILED");
        return nullptr;
    }
    void tick(uint32_t now) {
        // Unsigned elapsed subtraction handles the normal millis() wraparound.
        // A whole-job deadline bounds capture, save and read-back together;
        // the shorter capture deadline applies while waiting for the sensor.
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
            fifoSize_ = size_;
            if (!hw_.space(size_)) { fail("OUT_OF_SPACE"); return; }
            phase_ = Save;
        } else if (phase_ == Save) {
            size_t n = size_-done_; if (n > sizeof(bytes)) n = sizeof(bytes);
            if (hw_.readCamera(bytes, n) != (int)n) { fail("CAMERA_READ_FAILED"); return; }
            // A start marker can straddle blocks, so output may be n+1 bytes.
            uint8_t output[sizeof(bytes)+2]; size_t used = 0;
            // The hardware has already consumed all n FIFO bytes. Record their
            // boundaries before parsing so an early framing error still reports
            // the actual consumed block, rather than only its examined prefix.
            uint32_t offset = read_; read_ += n;
            // Maintain fixed-size metadata samples rather than retaining the
            // whole camera buffer. They help diagnose readout/marker faults.
            for (size_t i = 0; i < n; ++i) {
                if (offset+i < sizeof(first_)) first_[offset+i] = bytes[i];
                memmove(last_, last_+1, sizeof(last_)-1); last_[sizeof(last_)-1] = bytes[i];
            }
            for (size_t i = 0; i < n; ++i) {
                JpegStream::Result result = jpeg_.push(bytes[i]);
                if (result == JpegStream::Invalid) { fail("INVALID_JPEG"); return; }
                // Start represents BOTH SOI bytes, including a leading FF from
                // the preceding block. Byte/End emit one byte; Skip emits none.
                // Parser segment lengths prevent a thumbnail's EOI inside EXIF
                // from being mistaken for the outer image's end.
                if (result == JpegStream::Start) {
                    prefix_ = offset+i-1; output[used++] = 0xff; output[used++] = 0xd8;
                } else if (result == JpegStream::Byte || result == JpegStream::End) output[used++] = bytes[i];
            }
            // Write retries belong to the adapter and reuse output. Reading the
            // FIFO again after a failed write would consume different bytes and
            // corrupt the image. CRC accounts for emitted JPEG bytes only.
            if (used && !hw_.writeSD(output, used)) { fail("SD_WRITE_FAILED"); return; }
            crc_ = kfs::crcUpdate(crc_, output, used); saved_ += used; done_ += n;
            if (done_ == size_) {
                if (!jpeg_.complete()) { fail("INVALID_JPEG"); return; }
                size_ = saved_; // Published size/CRC describe the JPEG, not FIFO padding.
                if (!hw_.prepareVerify(size_)) { fail("SD_SYNC_FAILED"); return; }
                phase_ = Verify; done_ = 0;
            }
        } else if (phase_ == Verify) {
            // This is an independent SD read-back, not a second computation on
            // the same RAM buffer. Matching receive-time and stored-file CRCs
            // establishes that the extracted bytes survived the SD write path.
            // Full image decoding is performed later by the GCS when View runs.
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
        // Only the matching active job can be cancelled. A delayed abort after
        // publication leaves a successful image/result intact. GCS disconnect
        // and Stop waiting do not automatically invoke this protocol operation.
        if (known_ && id == id_ && busy()) {
            bool cleaned = hw_.cleanup(); hw_.stop();
            phase_ = Cancelled; error_ = cleaned ? "NONE" : "CLEANUP_FAILED";
        }
    }
    void status(uint32_t id, char* body, size_t capacity) const {
        // Status is observational: it does not advance capture or start a new
        // exposure. UNKNOWN may mean reset or replacement by a newer job; the
        // client must inspect the gallery before deciding whether to recapture.
        if (!known_ || id != id_) { snprintf(body, capacity, "CAMJOB|%08lx|UNKNOWN|0|0|00000000||UNKNOWN", (unsigned long)id); return; }
        char encoded[kfs::MAX_PATH*2+1];
        kfs::hexEncode((const uint8_t*)path_, strlen(path_), encoded);
        const char* states[] = {"UNKNOWN", "CAPTURING", "SAVING", "VERIFYING", "SAVED", "ERROR", "CANCELLED"};
        snprintf(body, capacity, "CAMJOB|%08lx|%s|%lu|%lu|%08lx|%s|%s", (unsigned long)id_, states[phase_],
                 (unsigned long)done_, (unsigned long)size_, (unsigned long)(crc_^0xffffffffUL), encoded, error_);
    }
    void diagnostics(uint32_t id, char* body, size_t capacity) const {
        if (!known_ || id != id_) { snprintf(body, capacity, "ERR|UNKNOWN_JOB"); return; }
        char first[17], last[17];
        kfs::hexEncode(first_, sizeof(first_), first); kfs::hexEncode(last_, sizeof(last_), last);
        snprintf(body, capacity, "CAMDETAIL|%08lx|%lu|%lu|%lu|%lu|%s|%s|%s",
                 (unsigned long)id_, (unsigned long)fifoSize_, (unsigned long)read_,
                 (unsigned long)saved_, (unsigned long)prefix_, first, last, error_);
    }
private:
    void fail(const char* error) {
        // Cleanup failure must not hide the original cause. Preserve both in
        // the returned error so a leftover reserved .part can be investigated
        // and explicitly removed through Remote Files after the job stops.
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
    JpegStream jpeg_;
    uint32_t fifoSize_ = 0, read_ = 0, saved_ = 0, prefix_ = 0;
    uint8_t first_[8] = {}, last_[8] = {};
};
} // namespace kcam
