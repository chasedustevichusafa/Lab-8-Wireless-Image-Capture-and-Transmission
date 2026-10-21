// Host adapter for testing the actual firmware protocol against the Python GCS.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>
#include <map>
#include "file_service.h"

class HostBackend {
public:
    explicit HostBackend(std::filesystem::path root, std::string fault) : root_(root), fault_(fault) {}
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
            file_.open(full, std::ios::binary);
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
    void close() { file_.close(); file_.clear(); }
private:
    std::filesystem::path root_;
    std::ifstream file_;
    std::string fault_;
    std::map<uint32_t, unsigned> attempts_;
    unsigned opens_ = 0;
};
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    HostBackend backend(argv[1], argc > 2 ? argv[2] : "");
    kfs::Service<HostBackend> service(backend);
    std::string line, fault = argc > 2 ? argv[2] : "";
    bool injected = false;
    while (std::getline(std::cin, line)) {
        // The embedded UART receiver drops an oversized line before handle().
        if (line.size() >= kfs::MAX_LINE) continue;
        const char* response = service.handle(line.c_str());
        if (!response) continue;
        std::string frame(response);
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
