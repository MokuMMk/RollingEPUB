/*
 * RollingEPUB base layer implementation: default platform, logging,
 * allocation helpers, build information.
 *
 * SPDX-License-Identifier: MIT
 */
#include "core/base.h"

#include <cstdlib>

namespace repub {

namespace {

void* default_alloc(void*, size_t size) { return std::malloc(size); }
void* default_realloc(void*, void* ptr, size_t size) { return std::realloc(ptr, size); }
void default_free(void*, void* ptr) { std::free(ptr); }

repub_err_t default_file_open(void* ctx, const char* path, repub_file_t** out) {
    (void)ctx;
    if (!path || !out) return REPUB_ERR_INVALID_ARG;
    FILE* file = std::fopen(path, "rb");
    if (!file) return REPUB_ERR_IO;
    *out = reinterpret_cast<repub_file_t*>(file);
    return REPUB_OK;
}

int32_t default_file_read(void* ctx, repub_file_t* file, void* buf, uint32_t n) {
    (void)ctx;
    if (!file || (!buf && n)) return -1;
    FILE* f = reinterpret_cast<FILE*>(file);
    size_t got = std::fread(buf, 1, n, f);
    if (got == 0 && std::ferror(f)) return -1;
    return static_cast<int32_t>(got);
}

int32_t default_file_seek(void* ctx, repub_file_t* file, int64_t offset, int whence) {
    (void)ctx;
    if (!file) return -1;
    FILE* f = reinterpret_cast<FILE*>(file);
    int origin = SEEK_SET;
    if (whence == 1) origin = SEEK_CUR;
    if (whence == 2) origin = SEEK_END;
#if defined(_WIN32)
    int rc = _fseeki64(f, offset, origin);
#else
    int rc = fseeko(f, static_cast<off_t>(offset), origin);
#endif
    return rc == 0 ? 0 : -1;
}

int64_t default_file_tell(void* ctx, repub_file_t* file) {
    (void)ctx;
    if (!file) return -1;
    FILE* f = reinterpret_cast<FILE*>(file);
#if defined(_WIN32)
    return _ftelli64(f);
#else
    return static_cast<int64_t>(ftello(f));
#endif
}

int64_t default_file_size(void* ctx, repub_file_t* file) {
    (void)ctx;
    if (!file) return -1;
    FILE* f = reinterpret_cast<FILE*>(file);
    int64_t pos = default_file_tell(ctx, file);
    if (pos < 0) return -1;
    if (default_file_seek(ctx, file, 0, 2) != 0) return -1;
    int64_t size = default_file_tell(ctx, file);
    default_file_seek(ctx, file, pos, 0);
    return size;
}

void default_file_close(void* ctx, repub_file_t* file) {
    (void)ctx;
    if (file) std::fclose(reinterpret_cast<FILE*>(file));
}

void default_log(void* ctx, int level, const char* message) {
    (void)ctx;
    static const char* const kPrefix[] = {"[repub][D]", "[repub][I]", "[repub][W]", "[repub][E]"};
    if (level < 0) level = 0;
    if (level > 3) level = 3;
    std::fprintf(stderr, "%s %s\n", kPrefix[level], message ? message : "");
}

uint32_t default_ticks_ms(void* ctx) {
    (void)ctx;
    return 0;
}

uint32_t feature_mask() {
    uint32_t mask = 0;
#if REPUB_ENABLE_TOC
    mask |= REPUB_FEATURE_TOC;
#endif
#if REPUB_ENABLE_COVER
    mask |= REPUB_FEATURE_COVER;
#endif
#if REPUB_ENABLE_CSS
    mask |= REPUB_FEATURE_CSS;
#endif
#if REPUB_ENABLE_FOOTNOTES
    mask |= REPUB_FEATURE_FOOTNOTES;
#endif
#if REPUB_ENABLE_TABLES
    mask |= REPUB_FEATURE_TABLES;
#endif
#if REPUB_ENABLE_DROP_CAPS
    mask |= REPUB_FEATURE_DROP_CAPS;
#endif
#if REPUB_ENABLE_RUBY
    mask |= REPUB_FEATURE_RUBY;
#endif
#if REPUB_ENABLE_HYPHENATION
    mask |= REPUB_FEATURE_HYPHENATION;
#endif
#if REPUB_ENABLE_VERTICAL_TEXT
    mask |= REPUB_FEATURE_VERTICAL_TEXT;
#endif
#if REPUB_ENABLE_BIDI
    mask |= REPUB_FEATURE_BIDI;
#endif
#if REPUB_ENABLE_TTF
    mask |= REPUB_FEATURE_TTF;
#endif
#if REPUB_ENABLE_BUILTIN_FONT
    mask |= REPUB_FEATURE_BUILTIN_FONT;
#endif
#if REPUB_ENABLE_TEXT_ANTIALIASING
    mask |= REPUB_FEATURE_ANTIALIASING;
#endif
#if REPUB_ENABLE_DITHERING
    mask |= REPUB_FEATURE_DITHERING;
#endif
#if REPUB_ENABLE_GRAYSCALE
    mask |= REPUB_FEATURE_GRAYSCALE;
#endif
#if REPUB_ENABLE_IMAGES
    mask |= REPUB_FEATURE_IMAGES;
#endif
#if REPUB_ENABLE_IMAGE_ZOOM
    mask |= REPUB_FEATURE_IMAGE_ZOOM;
#endif
#if REPUB_ENABLE_GIF
    mask |= REPUB_FEATURE_GIF;
#endif
#if REPUB_ENABLE_WEBP
    mask |= REPUB_FEATURE_WEBP;
#endif
#if REPUB_ENABLE_CACHE
    mask |= REPUB_FEATURE_CACHE;
#endif
#if REPUB_ENABLE_PROGRESS
    mask |= REPUB_FEATURE_PROGRESS;
#endif
#if REPUB_ENABLE_ENCRYPTION_PROBE
    mask |= REPUB_FEATURE_ENCRYPTION_PROBE;
#endif
    return mask;
}

const repub_build_info_t kBuildInfo = {
    REPUB_ABI_VERSION,
    feature_mask(),
    REPUB_VERSION_STRING,
#if defined(__ANDROID__)
    "android",
#elif defined(_WIN32)
    "windows",
#elif defined(ESP_PLATFORM)
    "esp-idf",
#elif defined(ARDUINO)
    "arduino",
#elif defined(__linux__)
    "linux",
#elif defined(__APPLE__)
    "darwin",
#else
    "unknown",
#endif
};

}  // namespace

const repub_build_info_t* repub_build_info(void) { return &kBuildInfo; }

int repub_feature_enabled(uint32_t feature_bit) {
    return (kBuildInfo.features & feature_bit) ? 1 : 0;
}

repub_platform_t repub_platform_default(void) {
    repub_platform_t platform{};
    platform.ctx = nullptr;
    platform.allocator.ctx = nullptr;
    platform.allocator.alloc = default_alloc;
    platform.allocator.realloc = default_realloc;
    platform.allocator.free = default_free;
    platform.file_open = default_file_open;
    platform.file_read = default_file_read;
    platform.file_seek = default_file_seek;
    platform.file_tell = default_file_tell;
    platform.file_size = default_file_size;
    platform.file_close = default_file_close;
    platform.log = default_log;
    platform.ticks_ms = default_ticks_ms;
    return platform;
}

void log_message(const repub_platform_t* platform, int level, const char* message) {
    if (platform && platform->log) {
        platform->log(platform->ctx, level, message);
    } else {
        default_log(nullptr, level, message);
    }
}

void logf(const repub_platform_t* platform, int level, const char* fmt, ...) {
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    log_message(platform, level, buffer);
}

void* palloc(const repub_platform_t* platform, size_t size) {
    if (size == 0) return nullptr;
    const repub_platform_t* p = effective_platform(platform);
    if (p->allocator.alloc) return p->allocator.alloc(p->allocator.ctx, size);
    return std::malloc(size);
}

void* prealloc(const repub_platform_t* platform, void* ptr, size_t size) {
    const repub_platform_t* p = effective_platform(platform);
    if (p->allocator.realloc) return p->allocator.realloc(p->allocator.ctx, ptr, size);
    return std::realloc(ptr, size);
}

void pfree(const repub_platform_t* platform, void* ptr) {
    if (!ptr) return;
    const repub_platform_t* p = effective_platform(platform);
    if (p->allocator.free) {
        p->allocator.free(p->allocator.ctx, ptr);
        return;
    }
    std::free(ptr);
}

const repub_platform_t* effective_platform(const repub_platform_t* platform) {
    static const repub_platform_t kDefault = repub_platform_default();
    return platform ? platform : &kDefault;
}

}  // namespace repub
