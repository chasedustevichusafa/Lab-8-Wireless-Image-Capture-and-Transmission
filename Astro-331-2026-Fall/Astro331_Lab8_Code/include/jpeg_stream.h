// Incremental JPEG framing for the Mega FIFO, which can contain transport bytes.
// Segment payloads are skipped by length so an EXIF thumbnail cannot end a photo.
#pragma once
#include <stdint.h>

namespace kcam {
class JpegStream {
public:
    enum Result {Skip, Start, Byte, End, Invalid};
    Result push(uint8_t value) {
        if (state_ == Failed) return Invalid;
        if (state_ == Finished) return Skip;
        if (state_ == Seek) {
            if (previous_ == 0xff && value == 0xd8) {
                state_ = MarkerFF; return Start; // Caller emits both SOI bytes.
            }
            previous_ = value; return Skip;
        }
        switch (state_) {
        case MarkerFF:
            if (value != 0xff) return invalid();
            state_ = Marker; break;
        case Scan:
            if (value == 0xff) state_ = ScanMarker;
            break;
        case Marker: case ScanMarker: {
            bool scan = state_ == ScanMarker;
            if (value == 0xff) break; // Marker fill bytes.
            if (scan && (value == 0 || (value >= 0xd0 && value <= 0xd7))) {
                state_ = Scan; break; // Stuffed FF or restart marker.
            }
            if (value == 0xd9) {
                if (!sawFrame_ || !sawScan_) return invalid();
                state_ = Finished; return End;
            }
            if (value == 0 || value == 0xd8 || (value >= 0xd0 && value <= 0xd7)) return invalid();
            if (value == 0x01) { state_ = scan ? Scan : MarkerFF; break; }
            marker_ = value; state_ = LengthHigh; break;
        }
        case LengthHigh:
            length_ = (uint16_t)value << 8; state_ = LengthLow; break;
        case LengthLow:
            length_ |= value;
            if (length_ < 2 || (marker_ == 0xda && length_ < 6) ||
                    (isFrame(marker_) && length_ < 8)) return invalid();
            if (isFrame(marker_)) sawFrame_ = true;
            if (marker_ == 0xda) {
                if (!sawFrame_) return invalid();
                sawScan_ = true;
            }
            length_ -= 2;
            state_ = length_ ? Payload : afterPayload(); break;
        case Payload:
            if (--length_ == 0) state_ = afterPayload();
            break;
        default: return invalid();
        }
        return Byte;
    }
    bool complete() const { return state_ == Finished; }
private:
    enum State {Seek, MarkerFF, Marker, LengthHigh, LengthLow, Payload, Scan, ScanMarker, Finished, Failed};
    static bool isFrame(uint8_t marker) {
        return marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc;
    }
    State afterPayload() const { return marker_ == 0xda ? Scan : MarkerFF; }
    Result invalid() { state_ = Failed; return Invalid; }
    State state_ = Seek;
    uint8_t previous_ = 0, marker_ = 0;
    uint16_t length_ = 0;
    bool sawFrame_ = false, sawScan_ = false;
};
} // namespace kcam
