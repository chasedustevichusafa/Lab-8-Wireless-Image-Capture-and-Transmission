#pragma once
// KFS1: bounded, checksummed request/response protocol shared with the GCS.
// Portable C++11 so the actual firmware service can also run in host tests.
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace kfs {
static const size_t MAX_LINE = 768;
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
struct Entry {
    char name[MAX_PATH + 1];
    bool directory;
    uint32_t size;
};

// Backend: list(path,index,entry) -> 1 entry, 0 end, -1 error;
// open(path,size), readAt(offset,buffer,count,attempt), close(). The open file remains owned by
// one session, and READ is sequential. END returns the whole-file CRC32.
template<class Backend> class Service {
public:
    explicit Service(Backend& backend) : backend_(backend) {}
    bool active() const { return active_; }
    void expire() { backend_.close(); active_ = false; cachedRequest_[0] = 0; }

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
            if (!number(fields[3], token, 16) || !active_ || token != token_)
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
    const char* reply(const char* body) {
        snprintf(response_, sizeof(response_), "@KFS1|%08lx|%s", (unsigned long)id_, body);
        size_t n = strlen(response_);
        uint32_t crc = crcUpdate(0xFFFFFFFFUL, (const uint8_t*)response_, n) ^ 0xFFFFFFFFUL;
        snprintf(response_ + n, sizeof(response_)-n, "|%08lx\n", (unsigned long)crc);
        return response_;
    }
    Backend& backend_;
    bool active_ = false;
    uint32_t id_ = 0, token_ = 0, size_ = 0, next_ = 0, crc_ = 0;
    char cachedRequest_[MAX_LINE] = {};
    char response_[MAX_LINE] = {};
};
} // namespace kfs
