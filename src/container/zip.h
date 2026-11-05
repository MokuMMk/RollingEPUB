/*
 * RollingEPUB ZIP container reader (read-only, streaming, ZIP64 capable).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef REPUB_SRC_CONTAINER_ZIP_H
#define REPUB_SRC_CONTAINER_ZIP_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/base.h"

namespace repub {

/*
 * Read-only ZIP archive built on miniz's central-directory reader with a
 * platform file callback (no stdio dependency).  All extraction results are
 * bounded by REPUB_MAX_CHAPTER_BYTES / REPUB_MAX_IMAGE_BYTES callers; this
 * class itself only enforces the configured entry count.
 */
class ZipArchive {
   public:
    ZipArchive();
    ~ZipArchive();
    ZipArchive(const ZipArchive&) = delete;
    ZipArchive& operator=(const ZipArchive&) = delete;

    repub_err_t open(const repub_platform_t* platform, const std::string& path);
    void close();
    bool is_open() const { return open_; }

    size_t entry_count() const;
    /* -1 when not found. Case-sensitive first, then case-insensitive fallback. */
    int find(std::string_view name) const;
    const std::string& entry_name(size_t index) const;
    uint64_t entry_size(size_t index) const;
    uint32_t entry_crc(size_t index) const;
    uint16_t entry_method(size_t index) const;

    /* Full extraction; verifies CRC. */
    repub_err_t extract(size_t index, std::vector<uint8_t>* out,
                        size_t max_bytes = 0) const;
    repub_err_t extract_to_string(size_t index, std::string* out,
                                  size_t max_bytes = 0) const;
    /* Extract at most cap bytes (prefix probe); does not verify the full CRC. */
    repub_err_t extract_prefix(size_t index, void* buffer, size_t cap,
                               size_t* bytes_written) const;

    uint64_t file_size() const { return file_size_; }
    const std::string& path() const { return path_; }
    const repub_platform_t* platform() const { return platform_; }

   private:
    struct Impl;
    Impl* impl_ = nullptr;
    const repub_platform_t* platform_ = nullptr;
    std::string path_;
    uint64_t file_size_ = 0;
    bool open_ = false;
};

}  // namespace repub

#endif  // REPUB_SRC_CONTAINER_ZIP_H
