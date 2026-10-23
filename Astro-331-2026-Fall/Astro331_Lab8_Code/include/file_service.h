#pragma once
// KFS1: bounded, checksummed request/response protocol shared with the GCS.
// Portable C++11 so the actual firmware service can also run in host tests.
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace kfs {
static const size_t MAX_LINE = 1024;
static const size_t MAX_PATH = 192;
static const size_t CHUNK = 128;
static const uint8_t READ_ATTEMPTS = 3;

inline uint32_t crcUpdate(uint32_t crc, const uint8_t* bytes, size_t length) {
    while (length--) {
        crc ^= *bytes++;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320UL : 0);
    }
    return crc;
}
inline int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
inline void hexEncode(const uint8_t* src, size_t n, char* dest) {
    const char* digits = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i) {
        dest[2*i] = digits[src[i] >> 4];
        dest[2*i+1] = digits[src[i] & 15];
    }
    dest[2*n] = 0;
}
inline bool number(const char* text, uint32_t& out, int base = 10) {
    if (!*text || strlen(text) > (base == 16 ? 8U : 10U)) return false;
    uint64_t value = 0;
    for (const char* c = text; *c; ++c) {
        int digit = nibble(*c);
        if (digit < 0 || digit >= base) return false;
        value = value * base + digit;
        if (value > UINT32_MAX) return false;
    }
    out = (uint32_t)value;
    return true;
}
inline bool decodePath(const char* hex, char* path) {
    size_t n = strlen(hex);
    if (!n || n % 2 || n / 2 > MAX_PATH) return false;
    for (size_t i = 0; i < n / 2; ++i) {
        int hi = nibble(hex[2*i]), lo = nibble(hex[2*i+1]);
        if (hi < 0 || lo < 0) return false;
        path[i] = (char)((hi << 4) | lo);
        if ((uint8_t)path[i] < 32 || path[i] == '\\' || path[i] == 127) return false;
    }
    path[n/2] = 0;
    if (path[0] != '/') return false;
    // Only canonical absolute SD paths, with no empty/dot components.
    if (n == 2) return true;
    const char* start = path + 1;
    for (const char* c = start; ; ++c) {
        if (*c == '/' || *c == 0) {
            size_t len = (size_t)(c - start);
            if (!len || (len == 1 && start[0] == '.') ||
                (len == 2 && start[0] == '.' && start[1] == '.')) return false;
            if (!*c) break;
            start = c + 1;
        }
    }
    return true;
}
inline bool partialPath(const char* path) {
    const char* name = strrchr(path, '/');
    name = name ? name+1 : path;
    if (strlen(name) != 18 || strncmp(name, ".kfs-", 5) || strcmp(name+13, ".part")) return false;
    for (unsigned i = 5; i < 13; ++i) if (nibble(name[i]) < 0) return false;
    return true;
}
inline bool uploadPath(const char* path, bool temporary = true) {
    const char* name = strrchr(path, '/');
    if (!name || !name[1]) return false;
    ++name;
    if ((strlen(name) >= 5 && name[0] == '.' && (name[1] | 32) == 'k' &&
             (name[2] | 32) == 'f' && (name[3] | 32) == 's' && name[4] == '-') ||
            (temporary && strlen(path)-strlen(name)+18 > MAX_PATH)) return false;
    for (const char* c = path; *c; ++c) {
        if (strchr("<>:\"|?*", *c)) return false;
        if ((*c == '.' || *c == ' ') && (!c[1] || c[1] == '/')) return false;
    }
    return true;
}
inline bool sameFolder(const char* first, const char* second) {
    const char* a = strrchr(first, '/');
    const char* b = strrchr(second, '/');
    return a && b && a-first == b-second && !memcmp(first, second, (size_t)(a-first));
}
struct Entry {
    char name[MAX_PATH + 1];
    bool directory;
    uint32_t size;
};

// Backend: list(path,index,entry) -> 1 entry, 0 end, -1 error;
// open(path,size), readAt(offset,buffer,count,attempt), close(). The open file remains owned by
// one session, and READ is sequential. END returns the whole-file CRC32.
// Upload backend methods own an exclusive temporary file through verification
// and no-replace publication, or cleanup on abort/expiry.
template<class Backend> class Service {
public:
    explicit Service(Backend& backend) : backend_(backend) {}
    bool active() const { return active_; }
    void expire() {
        if (active_ && uploading_) backend_.abortUpload();
        else backend_.close();
        active_ = uploading_ = verifying_ = false; cachedRequest_[0] = 0;
    }

    // Ignore non-protocol text or damaged requests. No unchecked command is run.
    // Returned frame includes newline. The caller writes it as one response.
    const char* handle(const char* line) {
        size_t length = strlen(line);
        if (length >= MAX_LINE || strncmp(line, "@KFS1|", 6)) return nullptr;
        const char* last = strrchr(line, '|');
        uint32_t supplied;
        if (!last || strlen(last + 1) != 8 || !number(last + 1, supplied, 16) ||
            (crcUpdate(0xFFFFFFFFUL, (const uint8_t*)line, last-line) ^ 0xFFFFFFFFUL) != supplied)
            return nullptr;
        if (mutationRequest_[0] && !strcmp(line, mutationRequest_)) return mutationResponse_;
        if (cachedRequest_[0] && !strcmp(line, cachedRequest_)) return response_;
        char work[MAX_LINE];
        memcpy(work, line, last-line);
        work[last-line] = 0;
        char* fields[8];
        size_t count = 0;
        char* cursor = work;
        while (cursor && count < 8) {
            fields[count++] = cursor;
            char* separator = strchr(cursor, '|');
            if (separator) { *separator = 0; cursor = separator + 1; }
            else cursor = nullptr;
        }
        uint32_t id;
        if (cursor || count < 3 || strlen(fields[1]) != 8 || !number(fields[1], id, 16))
            return nullptr;
        strcpy(cachedRequest_, line);
        id_ = id;
        const char* op = fields[2];
        char path[MAX_PATH + 1];
        if (!strcmp(op, "HELLO") && count == 3) return reply("HELLO|1|128");
        if (!strcmp(op, "CAPS") && count == 3) return reply("CAPS|UPLOAD1|MANAGE1|CAMERA1|CAMDIAG1|128");
        if (!strcmp(op, "CAMINFO") && count == 3) {
            char body[128]; backend_.cameraInfo(body, sizeof(body)); return reply(body);
        }
        if (!strcmp(op, "CAPTURE") && count == 6) {
            uint32_t job, mode;
            if (active_) return reply("ERR|BUSY");
            if (strlen(fields[3]) != 8 || !number(fields[3], job, 16) || !number(fields[4], mode) ||
                    !decodePath(fields[5], path)) return reply("ERR|BAD_CAPTURE");
            const char* error = backend_.cameraStart(job, mode, path);
            if (error) { char body[80]; snprintf(body, sizeof(body), "ERR|%s", error); return reply(body); }
            char body[600]; backend_.cameraStatus(job, body, sizeof(body)); return reply(body);
        }
        if ((!strcmp(op, "CAMSTATUS") || !strcmp(op, "CAMABORT")) && count == 4) {
            uint32_t job;
            if (strlen(fields[3]) != 8 || !number(fields[3], job, 16)) return reply("ERR|BAD_JOB");
            if (!strcmp(op, "CAMABORT")) backend_.cameraAbort(job);
            char body[600]; backend_.cameraStatus(job, body, sizeof(body)); return reply(body);
        }
        if (!strcmp(op, "CAMDIAG") && count == 4) {
            uint32_t job;
            if (strlen(fields[3]) != 8 || !number(fields[3], job, 16)) return reply("ERR|BAD_JOB");
            char body[240]; backend_.cameraDiagnostics(job, body, sizeof(body)); return reply(body);
        }
        if (backend_.cameraBusy()) return reply("ERR|BUSY");
        if (!strcmp(op, "RESULT") && count == 4) {
            uint32_t request;
            if (!number(fields[3], request, 16) || !mutationRequest_[0] || request != mutationId_)
                return reply("RESULT|UNKNOWN");
            char body[100];
            snprintf(body, sizeof(body), "RESULT|%s|%s", mutationOp_, mutationBody_);
            return reply(body);
        }
        if ((!strcmp(op, "DELETE") && count == 4) || (!strcmp(op, "RENAME") && count == 5)) {
            const char* error = nullptr;
            char target[MAX_PATH+1];
            if (active_) error = "BUSY";
            else if (!decodePath(fields[3], path) || !uploadPath(path, false)) error = "BAD_PATH";
            else if (!strcmp(op, "RENAME") &&
                     (!decodePath(fields[4], target) || !uploadPath(target, false) || !sameFolder(path, target)))
                error = "BAD_PATH";
            else error = !strcmp(op, "DELETE") ? backend_.deleteFile(path) : backend_.renameFile(path, target);
            char body[64];
            if (error) snprintf(body, sizeof(body), "ERR|%s", error);
            else strcpy(body, !strcmp(op, "DELETE") ? "DELETED" : "RENAMED");
            const char* response = reply(body);
            strcpy(mutationRequest_, cachedRequest_); strcpy(mutationResponse_, response);
            strcpy(mutationBody_, body); strcpy(mutationOp_, op); mutationId_ = id;
            return response;
        }
        if (!strcmp(op, "CLEAN") && count == 4) {
            if (active_) return reply("ERR|BUSY");
            if (!decodePath(fields[3], path) || !partialPath(path)) return reply("ERR|BAD_PATH");
            return reply(backend_.cleanPartial(path) ? "REMOVED" : "ERR|CLEAN_FAILED");
        }
        if (!strcmp(op, "PUT") && count == 6) {
            if (active_) return reply("ERR|BUSY");
            uint32_t size, crc;
            if (!decodePath(fields[3], path) || !uploadPath(path)) return reply("ERR|BAD_PATH");
            if (!number(fields[4], size) || strlen(fields[5]) != 8 || !number(fields[5], crc, 16))
                return reply("ERR|BAD_SIZE_OR_CRC");
            char temporary[MAX_PATH+1];
            const char* name = strrchr(path, '/')+1;
            size_t prefix = (size_t)(name-path);
            memcpy(temporary, path, prefix);
            snprintf(temporary+prefix, sizeof(temporary)-prefix, ".kfs-%08lx.part", (unsigned long)id);
            const char* error = backend_.beginUpload(path, temporary, size);
            if (error) { char body[80]; snprintf(body, sizeof(body), "ERR|%s", error); return reply(body); }
            if (completed_ && completedToken_ == id) completed_ = false;
            size_ = size; expectedCrc_ = crc; token_ = id; next_ = 0; crc_ = 0xffffffffUL;
            active_ = uploading_ = true; verifying_ = false; lastWriteLength_ = 0;
            verifyNext_ = 0; verifyCrc_ = 0xffffffffUL; lastVerify_ = UINT32_MAX;
            char body[64];
            snprintf(body, sizeof(body), "READY|%08lx|%lu", (unsigned long)token_, (unsigned long)size_);
            return reply(body);
        }
        if ((!strcmp(op, "WRITE") && count == 6) ||
            (!strcmp(op, "VERIFY") && count == 5) ||
            ((!strcmp(op, "COMMIT") || !strcmp(op, "ABORT") || !strcmp(op, "STATUS")) && count == 4)) {
            uint32_t token;
            if (!number(fields[3], token, 16)) return reply("ERR|NO_SESSION");
            if (completed_ && token == completedToken_ &&
                (!strcmp(op, "COMMIT") || !strcmp(op, "STATUS") || !strcmp(op, "ABORT")))
                return stored(completedSize_, completedCrc_);
            if (!active_ || !uploading_ || token != token_) return reply("ERR|NO_SESSION");
            if (!strcmp(op, "ABORT")) {
                bool removed = backend_.abortUpload();
                active_ = uploading_ = verifying_ = false;
                return reply(removed ? "ABORTED|REMOVED" : "ABORTED|LEFTOVER");
            }
            if (!strcmp(op, "STATUS")) return position("UPLOADING", next_);
            if (!strcmp(op, "WRITE")) {
                uint32_t offset;
                size_t length = strlen(fields[5]);
                if (!number(fields[4], offset) || !length || length % 2 || length/2 > CHUNK)
                    return reply("ERR|BAD_CHUNK");
                size_t n = length/2;
                uint8_t bytes[CHUNK];
                for (size_t i = 0; i < n; ++i) {
                    int hi = nibble(fields[5][2*i]), lo = nibble(fields[5][2*i+1]);
                    if (hi < 0 || lo < 0) return reply("ERR|BAD_CHUNK");
                    bytes[i] = (uint8_t)((hi << 4) | lo);
                }
                // Preserve last WRITE acknowledgment independently of other
                // requests. A repeated chunk must never be written/CRC'd twice.
                if (lastWriteLength_ == n && offset == lastWriteOffset_ &&
                    !memcmp(lastWrite_, bytes, n)) return position("ACK", next_);
                if (verifying_ || offset != next_ || next_ >= size_ ||
                    n != (size_-next_ < CHUNK ? size_-next_ : CHUNK)) return reply("ERR|BAD_OFFSET");
                int got = -1;
                for (uint8_t attempt = 1; attempt <= READ_ATTEMPTS; ++attempt) {
                    got = backend_.writeAt(next_, bytes, n, attempt);
                    if (got == (int)n) break;
                }
                if (got != (int)n) return uploadFailure("WRITE_FAILED", next_, n, got);
                lastWriteOffset_ = next_; lastWriteLength_ = n; memcpy(lastWrite_, bytes, n);
                crc_ = crcUpdate(crc_, bytes, n); next_ += n;
                return position("ACK", next_);
            }
            if (!strcmp(op, "VERIFY")) {
                uint32_t offset;
                if (!number(fields[4], offset)) return reply("ERR|BAD_OFFSET");
                if (next_ != size_ || (crc_ ^ 0xffffffffUL) != expectedCrc_)
                    return uploadFailure("SIZE_OR_CRC_MISMATCH", next_, 0, -1);
                if (verifying_ && offset == lastVerify_) return position("CHECKED", verifyNext_);
                if (offset != verifyNext_) return reply("ERR|BAD_OFFSET");
                if (!verifying_) {
                    if (!backend_.prepareVerify(size_)) return uploadFailure("SYNC_FAILED", next_, 0, -1);
                    verifying_ = true;
                }
                lastVerify_ = offset;
                // Bounded read-back batches keep progress, retries and Cancel
                // responsive instead of blocking on an entire large file.
                uint32_t end = size_-verifyNext_ < 1024 ? size_ : verifyNext_+1024;
                while (verifyNext_ < end) {
                    uint8_t bytes[CHUNK];
                    size_t n = end-verifyNext_ < CHUNK ? end-verifyNext_ : CHUNK;
                    int got = -1;
                    for (uint8_t attempt = 1; attempt <= READ_ATTEMPTS; ++attempt) {
                        got = backend_.readAt(verifyNext_, bytes, n, attempt);
                        if (got == (int)n) break;
                    }
                    if (got != (int)n) return uploadFailure("VERIFY_READ_FAILED", verifyNext_, n, got);
                    verifyCrc_ = crcUpdate(verifyCrc_, bytes, n); verifyNext_ += n;
                }
                return position("CHECKED", verifyNext_);
            }
            if (!verifying_ || verifyNext_ != size_) return reply("ERR|INCOMPLETE");
            if ((verifyCrc_ ^ 0xffffffffUL) != expectedCrc_)
                return uploadFailure("VERIFY_CRC_MISMATCH", verifyNext_, 0, -1);
            if (!backend_.commitUpload()) return uploadFailure("COMMIT_FAILED", next_, 0, -1);
            completed_ = true; completedToken_ = token_; completedSize_ = size_; completedCrc_ = expectedCrc_;
            active_ = uploading_ = verifying_ = false;
            return stored(completedSize_, completedCrc_);
        }
        if (!strcmp(op, "LIST") && count == 5) {
            uint32_t index;
            if (!decodePath(fields[3], path) || !number(fields[4], index) || index > 4096)
                return reply("ERR|BAD_PATH");
            Entry entry;
            int found = backend_.list(path, index, entry);
            if (found < 0) return reply("ERR|DIRECTORY_UNAVAILABLE");
            if (!found) return reply("END");
            size_t n = strlen(entry.name);
            if (!n || n > MAX_PATH) return reply("ERR|NAME_TOO_LONG");
            char name[MAX_PATH*2 + 1], body[MAX_LINE - 40];
            hexEncode((const uint8_t*)entry.name, n, name);
            snprintf(body, sizeof(body), "ENTRY|%lu|%c|%lu|%s", (unsigned long)(index+1),
                     entry.directory ? 'D' : 'F', (unsigned long)entry.size, name);
            return reply(body);
        }
        if (!strcmp(op, "OPEN") && count == 4) {
            if (active_ && uploading_) return reply("ERR|BUSY");
            if (!decodePath(fields[3], path)) return reply("ERR|BAD_PATH");
            backend_.close(); active_ = false;
            if (!backend_.open(path, size_)) return reply("ERR|FILE_UNAVAILABLE");
            token_ = id; next_ = 0; crc_ = 0xFFFFFFFFUL; active_ = true;
            char body[64];
            snprintf(body, sizeof(body), "FILE|%08lx|%lu", (unsigned long)token_, (unsigned long)size_);
            return reply(body);
        }
        if ((!strcmp(op, "READ") && count == 5) ||
            ((!strcmp(op, "END") || !strcmp(op, "CLOSE")) && count == 4)) {
            uint32_t token;
            if (!number(fields[3], token, 16) || !active_ || uploading_ || token != token_)
                return reply("ERR|NO_SESSION");
            if (!strcmp(op, "CLOSE")) {
                backend_.close(); active_ = false; return reply("CLOSED");
            }
            if (!strcmp(op, "END")) {
                if (next_ != size_) return reply("ERR|INCOMPLETE");
                char body[64];
                snprintf(body, sizeof(body), "DONE|%lu|%08lx", (unsigned long)size_,
                         (unsigned long)(crc_ ^ 0xFFFFFFFFUL));
                backend_.close(); active_ = false;
                return reply(body);
            }
            uint32_t offset;
            if (!number(fields[4], offset) || offset != next_ || next_ >= size_)
                return reply("ERR|BAD_OFFSET");
            uint8_t bytes[CHUNK];
            size_t n = (size_ - next_ < CHUNK) ? size_ - next_ : CHUNK;
            int got = -1;
            for (uint8_t attempt = 1; attempt <= READ_ATTEMPTS; ++attempt) {
                got = backend_.readAt(next_, bytes, n, attempt);
                if (got == (int)n) break;
            }
            if (got != (int)n) {
                char body[100];
                snprintf(body, sizeof(body), "ERR|READ_FAILED|%lu|%u|%d|%u",
                         (unsigned long)next_, (unsigned)n, got, (unsigned)READ_ATTEMPTS);
                backend_.close(); active_ = false; return reply(body);
            }
            char hex[CHUNK*2+1], body[CHUNK*2+40];
            hexEncode(bytes, n, hex);
            snprintf(body, sizeof(body), "DATA|%lu|%s", (unsigned long)next_, hex);
            crc_ = crcUpdate(crc_, bytes, n);
            next_ += n;
            return reply(body);
        }
        return reply("ERR|BAD_COMMAND");
    }
private:
    const char* position(const char* op, uint32_t offset) {
        char body[64];
        snprintf(body, sizeof(body), "%s|%lu|%lu", op, (unsigned long)offset, (unsigned long)size_);
        return reply(body);
    }
    const char* stored(uint32_t size, uint32_t crc) {
        char body[64];
        snprintf(body, sizeof(body), "STORED|%lu|%08lx", (unsigned long)size, (unsigned long)crc);
        return reply(body);
    }
    const char* uploadFailure(const char* reason, uint32_t offset, size_t wanted, int got) {
        backend_.abortUpload(); active_ = uploading_ = verifying_ = false;
        char body[120];
        snprintf(body, sizeof(body), "ERR|%s|%lu|%u|%d|%u", reason, (unsigned long)offset,
                 (unsigned)wanted, got, (unsigned)READ_ATTEMPTS);
        return reply(body);
    }
    const char* reply(const char* body) {
        snprintf(response_, sizeof(response_), "@KFS1|%08lx|%s", (unsigned long)id_, body);
        size_t n = strlen(response_);
        uint32_t crc = crcUpdate(0xFFFFFFFFUL, (const uint8_t*)response_, n) ^ 0xFFFFFFFFUL;
        snprintf(response_ + n, sizeof(response_)-n, "|%08lx\n", (unsigned long)crc);
        return response_;
    }
    char mutationRequest_[MAX_LINE] = {}, mutationResponse_[128] = {};
    char mutationBody_[64] = {}, mutationOp_[8] = {};
    uint32_t mutationId_ = 0;
    Backend& backend_;
    bool active_ = false, uploading_ = false, verifying_ = false, completed_ = false;
    uint32_t expectedCrc_ = 0, verifyNext_ = 0, verifyCrc_ = 0xffffffffUL, lastVerify_ = UINT32_MAX;
    uint32_t lastWriteOffset_ = 0, completedToken_ = 0, completedSize_ = 0, completedCrc_ = 0;
    size_t lastWriteLength_ = 0;
    uint8_t lastWrite_[CHUNK] = {};
    uint32_t id_ = 0, token_ = 0, size_ = 0, next_ = 0, crc_ = 0;
    char cachedRequest_[MAX_LINE] = {};
    char response_[MAX_LINE] = {};
};
} // namespace kfs
