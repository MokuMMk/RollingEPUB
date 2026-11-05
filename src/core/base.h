/*
 * RollingEPUB internal base layer: platform wrapper, allocation, logging,
 * geometry and small helpers shared by every module.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef REPUB_SRC_CORE_BASE_H
#define REPUB_SRC_CORE_BASE_H

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "rolling_epub/repub.h"

namespace repub {

struct Rect {
    int32_t x = 0;
    int32_t y = 0;
    int32_t w = 0;
    int32_t h = 0;

    bool empty() const { return w <= 0 || h <= 0; }
    int32_t right() const { return x + w; }
    int32_t bottom() const { return y + h; }
    Rect intersect(const Rect& o) const {
        int32_t l = x > o.x ? x : o.x;
        int32_t t = y > o.y ? y : o.y;
        int32_t r = right() < o.right() ? right() : o.right();
        int32_t b = bottom() < o.bottom() ? bottom() : o.bottom();
        return Rect{l, t, r - l, b - t};
    }
};

inline Rect from_public(const repub_rect_t& r) { return Rect{r.x, r.y, r.w, r.h}; }
inline repub_rect_t to_public(const Rect& r) { return repub_rect_t{r.x, r.y, r.w, r.h}; }

/* --------------------------------------------------------------------- */
/* Logging                                                               */
/* --------------------------------------------------------------------- */

enum LogLevel { LOG_DEBUG = 0, LOG_INFO = 1, LOG_WARN = 2, LOG_ERROR = 3 };

void log_message(const repub_platform_t* platform, int level, const char* message);
void logf(const repub_platform_t* platform, int level, const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

/* --------------------------------------------------------------------- */
/* Allocation through the platform allocator                             */
/* --------------------------------------------------------------------- */

void* palloc(const repub_platform_t* platform, size_t size);
void* prealloc(const repub_platform_t* platform, void* ptr, size_t size);
void pfree(const repub_platform_t* platform, void* ptr);

/* Platform used when the caller passes NULL (stdio + malloc). */
const repub_platform_t* effective_platform(const repub_platform_t* platform);

/* --------------------------------------------------------------------- */
/* Small helpers                                                         */
/* --------------------------------------------------------------------- */

template <typename T>
void append_bytes(std::vector<uint8_t>& out, const T* data, size_t count) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
    out.insert(out.end(), p, p + count);
}

inline uint32_t read_le16(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8);
}
inline uint32_t read_le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline uint64_t read_le64(const uint8_t* p) {
    return static_cast<uint64_t>(read_le32(p)) |
           (static_cast<uint64_t>(read_le32(p + 4)) << 32);
}

}  // namespace repub

#endif  // REPUB_SRC_CORE_BASE_H
