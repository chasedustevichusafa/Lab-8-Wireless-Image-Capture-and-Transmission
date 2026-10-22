// Host model for the actual cooperative camera_job.h; faults never touch hardware.
#pragma once
#include "camera_job.h"

class HostCameraHardware {
public:
    HostCameraHardware(std::filesystem::path root, std::string fault) : root_(root), fault_(fault) {
        image_ = {0xff, 0xd8};
        for (unsigned i = 0; i < 700; ++i) image_.push_back((uint8_t)i);
        image_.push_back(0xff); image_.push_back(0xd9);
        if ((fault_ == "camera-invalid-jpeg" || fault_ == "camera-cleanup-fail")) image_[0] = 0;
    }
    bool ready() const { return fault_ != "camera-absent"; }
    uint32_t mask() const { return ready() ? 0x1fff : 0; }
    const char* reserve(const char* path, const char* temporary) {
        if (fault_ == "camera-sd-absent") return "SD_UNAVAILABLE";
        try {
            std::filesystem::create_directories(root_/"images");
            target_ = root_/(path+1); temporary_ = root_/(temporary+1);
            if (std::filesystem::exists(target_)) return "TARGET_EXISTS";
            if (std::filesystem::exists(temporary_)) return "TEMP_EXISTS";
            file_.open(temporary_, std::ios::binary | std::ios::out | std::ios::trunc);
            position_ = 0;
            return file_ ? nullptr : "CREATE_FAILED";
        } catch (...) { return "CREATE_FAILED"; }
    }
    bool start(unsigned) { ++starts; return fault_ != "camera-start-fail"; }
    int poll(uint32_t& size) { size = fault_ == "camera-too-large" ? kcam::MAX_IMAGE+1 : image_.size(); return fault_ == "camera-timeout" ? 0 : 1; }
    bool space(uint32_t) { return fault_ != "camera-full"; }
    int readCamera(uint8_t* bytes, size_t n) {
        if (fault_ == "camera-read-fail") return -1;
        if (position_+n > image_.size()) return -1;
        memcpy(bytes, image_.data()+position_, n); position_ += n; return (int)n;
    }
    bool writeSD(const uint8_t* bytes, size_t n) {
        if (fault_ == "camera-write-fail") return false;
        file_.write((const char*)bytes, n); return (bool)file_;
    }
    bool prepareVerify(uint32_t) {
        file_.flush(); file_.close();
        if (fault_ == "camera-sync-fail") return false;
        if (fault_ == "camera-corrupt-sd") {
            file_.open(temporary_, std::ios::in | std::ios::out | std::ios::binary);
            file_.put('X'); file_.close();
        }
        file_.clear(); file_.open(temporary_, std::ios::in | std::ios::binary);
        return (bool)file_;
    }
    int readSD(uint8_t* bytes, size_t n) {
        if (fault_ == "camera-verify-fail") return -1;
        file_.read((char*)bytes, n); return (int)file_.gcount();
    }
    bool commit() {
        file_.close();
        if (fault_ == "camera-publish-fail" || std::filesystem::exists(target_)) return false;
        if (::link(temporary_.c_str(), target_.c_str()) != 0) return false;
        return std::filesystem::remove(temporary_);
    }
    bool cleanup() {
        file_.close(); file_.clear();
        if (fault_ == "camera-cleanup-fail") return false;
        return temporary_.empty() || !std::filesystem::exists(temporary_) || std::filesystem::remove(temporary_);
    }
    void stop() {}
    unsigned starts = 0;
private:
    std::filesystem::path root_, target_, temporary_;
    std::string fault_;
    std::fstream file_;
    std::vector<uint8_t> image_;
    size_t position_ = 0;
};
