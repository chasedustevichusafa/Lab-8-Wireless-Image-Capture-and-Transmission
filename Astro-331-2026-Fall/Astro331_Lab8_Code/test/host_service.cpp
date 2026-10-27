// Host adapter for testing the actual firmware protocol against the Python GCS.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>
#include <map>
#include <cctype>
#include <unistd.h>
#include <fcntl.h>
#include "file_service.h"
#include "host_camera.h"

class HostBackend {
public:
    explicit HostBackend(std::filesystem::path root, std::string fault) : root_(root), fault_(fault), cameraHardware_(root, fault), cameraJob_(cameraHardware_) {}
    bool cameraBusy() const { return cameraJob_.busy(); }
    void cameraInfo(char* body, size_t n) {
        snprintf(body, n, "CAMERA|3MP|%lu|%s|%s", (unsigned long)cameraHardware_.mask(),
                 cameraHardware_.ready() ? "NONE" : "CAMERA_NOT_FOUND",
                 fault_ == "camera-sd-absent" ? "SD_UNAVAILABLE" : "SD_READY");
    }
    const char* cameraStart(uint32_t id, unsigned mode, const char* path) {
        return cameraJob_.start(id, mode, path, cameraClock_);
    }
    void cameraTick() {
        cameraClock_ += fault_ == "camera-timeout" ? 8000 : fault_ == "camera-slow-job" ? 30000 : 50;
        cameraJob_.tick(cameraClock_);
    }
    void cameraStatus(uint32_t id, char* body, size_t n) { cameraJob_.status(id, body, n); }
    void cameraDiagnostics(uint32_t id, char* body, size_t n) { cameraJob_.diagnostics(id, body, n); }
    void cameraAbort(uint32_t id) { cameraJob_.abort(id); }
    int list(const char* path, uint32_t index, kfs::Entry& entry) {
        try {
            std::vector<std::filesystem::directory_entry> files;
            for (auto& item : std::filesystem::directory_iterator(root_ / (path+1))) files.push_back(item);
            std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.path() < b.path(); });
            if (index >= files.size()) return 0;
            auto& item = files[index];
            auto name = item.path().filename().string();
            if (name.size() > kfs::MAX_PATH) return -1;
            strcpy(entry.name, name.c_str());
            entry.directory = item.is_directory();
            entry.size = entry.directory ? 0 : (uint32_t)item.file_size();
            return 1;
        } catch (...) { return -1; }
    }
    bool open(const char* path, uint32_t& size) {
        close();
        try {
            auto full = root_ / (path+1);
            if (!std::filesystem::is_regular_file(full)) return false;
            size = (uint32_t)std::filesystem::file_size(full);
            file_.open(full, std::ios::binary | std::ios::in);
            attempts_.clear(); ++opens_;
            return file_.is_open();
        } catch (...) { return false; }
    }
    int readAt(uint32_t offset, uint8_t* bytes, size_t n, uint8_t) {
        file_.clear();
        file_.seekg(offset);
        if (!file_) return -1;
        unsigned attempt = attempts_[offset]++;
        if (fault_ == "read-permanent" && offset >= 4096) return -1;
        bool fail = (offset == 4096 && attempt < 2) ||
                    (fault_ == "read-scattered" && (offset / 128 + opens_) % 17 == 5 && attempt < 2);
        if (fail) {
            if (fault_ == "read-error" || fault_ == "read-scattered") return -1;
            if (fault_ == "read-zero") return 0;
            if (fault_ == "read-short") n /= 2;
        }
        file_.read((char*)bytes, n);
        return (int)file_.gcount();
    }
    bool fatExists(const std::filesystem::path& path) {
        auto lower = [](std::string value) {
            for (char& c : value) c = (char)std::tolower((unsigned char)c);
            return value;
        };
        if (!std::filesystem::is_directory(path.parent_path())) return false;
        for (auto& item : std::filesystem::directory_iterator(path.parent_path()))
            if (lower(item.path().filename().string()) == lower(path.filename().string())) return true;
        return false;
    }
    const char* beginUpload(const char* target, const char* temporary, uint32_t) {
        target_ = root_/(target+1); temporary_ = root_/(temporary+1);
        if (fatExists(target_)) return "TARGET_EXISTS";
        if (!std::filesystem::is_directory(target_.parent_path())) return "DIRECTORY_UNAVAILABLE";
        if (fault_ == "no-space") return "OUT_OF_SPACE";
        int fd = ::open(temporary_.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
        if (fd < 0) return "CREATE_FAILED";
        ::close(fd);
        file_.open(temporary_, std::ios::binary | std::ios::in | std::ios::out);
        return file_.is_open() ? nullptr : "CREATE_FAILED";
    }
    int writeAt(uint32_t offset, const uint8_t* bytes, size_t n, uint8_t attempt) {
        file_.clear(); file_.seekp(offset);
        if (!file_) return -1;
        if (offset >= 512 && fault_ == "write-permanent") return -1;
        if (offset == 512 && attempt < 3) {
            if (fault_ == "write-error") return -1;
            if (fault_ == "write-short") n /= 2;
        }
        file_.write((const char*)bytes, n); file_.flush();
        return file_ ? (int)n : -1;
    }
    bool prepareVerify(uint32_t size) {
        file_.flush();
        if (fault_ == "sync-fail" || !file_ || std::filesystem::file_size(temporary_) != size) return false;
        if (fault_ == "verify-corrupt" && size) {
            file_.seekp(0); file_.put((char)0x7f); file_.flush();
        }
        close(); attempts_.clear();
        file_.open(temporary_, std::ios::binary | std::ios::in);
        return file_.is_open();
    }
    bool commitUpload() {
        close();
        if (fault_ == "commit-fail" || fatExists(target_)) return false;
        // POSIX link provides no-replace publication in this host-only model.
        if (::link(temporary_.c_str(), target_.c_str()) != 0) return false;
        return std::filesystem::remove(temporary_);
    }
    bool abortUpload() {
        close();
        if (fault_ == "abort-fail") return false;
        if (temporary_.empty() || !std::filesystem::exists(temporary_)) return true;
        return std::filesystem::remove(temporary_);
    }
    const char* deleteFile(const char* path) {
        auto full = root_/(path+1);
        if (!std::filesystem::exists(full)) return "FILE_UNAVAILABLE";
        if (!std::filesystem::is_regular_file(full)) return "NOT_A_FILE";
        if (fault_ == "delete-fail") return "DELETE_FAILED";
        return std::filesystem::remove(full) ? nullptr : "DELETE_FAILED";
    }
    const char* renameFile(const char* path, const char* target) {
        auto full = root_/(path+1), destination = root_/(target+1);
        if (!std::filesystem::exists(full)) return "FILE_UNAVAILABLE";
        if (!std::filesystem::is_regular_file(full)) return "NOT_A_FILE";
        if (fatExists(destination)) return "TARGET_EXISTS";
        if (fault_ == "rename-fail") return "RENAME_FAILED";
        if (::link(full.c_str(), destination.c_str()) != 0) return "RENAME_FAILED";
        return std::filesystem::remove(full) ? nullptr : "RENAME_FAILED";
    }
    bool cleanPartial(const char* path) {
        auto full = root_/(path+1);
        return kfs::partialPath(path) && std::filesystem::is_regular_file(full) && std::filesystem::remove(full);
    }
    void close() { file_.close(); file_.clear(); }
private:
    std::filesystem::path root_;
    std::fstream file_;
    std::filesystem::path target_, temporary_;
    std::string fault_;
    std::map<uint32_t, unsigned> attempts_;
    unsigned opens_ = 0;
    HostCameraHardware cameraHardware_;
    kcam::Job<HostCameraHardware> cameraJob_;
    uint32_t cameraClock_ = 0;
};
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    HostBackend backend(argv[1], argc > 2 ? argv[2] : "");
    kfs::Service<HostBackend> service(backend);
    std::string line, fault = argc > 2 ? argv[2] : "";
    bool injected = false;
    unsigned fastReplies = 0;
    std::string heldReply;
    while (std::getline(std::cin, line)) {
        // The embedded UART receiver drops an oversized line before handle().
        if (line.size() >= kfs::MAX_LINE) continue;
        if (!line.empty()) backend.cameraTick();
        // Lose a complete request before the parser sees it. The later member
        // of its batch gets BAD_OFFSET, so recovery tests exercise both sides
        // of the missing-command case, not only cached reply retransmission.
        if (fault == "speed-drop-command" && !injected &&
                (line.find("|READX|") != std::string::npos || line.find("|WRITEX|") != std::string::npos)) {
            injected = true; continue;
        }
        const char* response = service.handle(line.c_str());
        if (!response) continue;
        std::string frame(response);
        if (frame.find("|DATA|") != std::string::npos || frame.find("|ACK|") != std::string::npos) {
            ++fastReplies;
            if (fault == "speed-drop-second" && fastReplies == 2) continue;
            if (fault == "speed-corrupt-second" && fastReplies == 2) frame[frame.size()-3] ^= 1;
            if (fault == "speed-reorder" && fastReplies == 1) { heldReply = frame; continue; }
            if (fault == "speed-reorder" && fastReplies == 2) {
                std::cout << frame << heldReply << std::flush; heldReply.clear(); continue;
            }
        }
        if (fault == "camera-drop-start" && line.find("|CAPTURE|") != std::string::npos) continue;
        if (fault == "camera-drop-status" && frame.find("|CAMJOB|") != std::string::npos) continue;
        if ((fault == "manage-drop-reply" || fault == "manage-drop-all" || fault == "manage-expire-reply") &&
            (frame.find("|DELETED|") != std::string::npos || frame.find("|RENAMED|") != std::string::npos ||
             frame.find("|DELETED\n") != std::string::npos || frame.find("|RENAMED\n") != std::string::npos)) {
            bool result = line.find("|RESULT|") != std::string::npos;
            if (!result && fault == "manage-expire-reply") service.expire();
            if (fault == "manage-drop-all" || !result) continue;
        }
        if (fault == "upload-drop-ready" && frame.find("|READY|") != std::string::npos) continue;
        if (((fault == "upload-drop-commit" && line.find("|COMMIT|") != std::string::npos) ||
             fault == "upload-drop-terminal") && frame.find("|STORED|") != std::string::npos) continue;
        if (frame.find("|ACK|") != std::string::npos) {
            if (fault == "upload-duplicate-write") service.handle(line.c_str());
            if (!injected) {
                injected = true;
                if (fault == "upload-drop-ack") continue;
                if (fault == "upload-corrupt-ack") frame[frame.size()-3] ^= 1;
                if (fault == "upload-disconnect") return 0;
                if (fault == "upload-expire") service.expire();
            }
        }
        if (!injected && frame.find("|DATA|") != std::string::npos) {
            injected = true;
            if (fault == "expire") service.expire();
            if (fault == "drop") continue;
            if (fault == "corrupt") frame[frame.size()-3] = frame[frame.size()-3] == '0' ? '1' : '0';
            if (fault == "truncate") frame = frame.substr(0, 12);
            if (fault == "duplicate") std::cout << frame;
            if (fault == "disconnect") return 0;
        }
        if (fault == "bad-final-crc" && frame.find("|DONE|") != std::string::npos) {
            std::string body = frame.substr(0, frame.rfind('|'));
            body[body.size()-1] = body.back() == '0' ? '1' : '0';
            char crc[16];
            snprintf(crc, sizeof(crc), "|%08lx\n", (unsigned long)(kfs::crcUpdate(0xffffffffUL,
                     (const uint8_t*)body.data(), body.size()) ^ 0xffffffffUL));
            frame = body + crc;
        }
        std::cout << frame << std::flush;
    }
}
