/*
 * RollingEPUB read-only ZIP container reader.
 *
 * SPDX-License-Identifier: MIT
 *
 * Uses the vendored miniz central-directory reader (third_party/miniz.c) with
 * a custom mz_file_read_func that reads through the repub_platform_t file
 * callbacks, so the container layer never touches stdio directly.
 *
 * Provenance: the platform-IO ZIP wiring follows the approach of Read Pico
 * main/book/zip_reader.c (Apache-2.0) and CrossMux lib/ZipFile/ZipFile.cpp
 * (MIT); decompression/CRC/ZIP64 handling is delegated to miniz (MIT).
 */
#include "container/zip.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "core/text_util.h"

/* miniz is compiled into this single translation unit.  These switches keep it
 * portable and free of stdio; the implementation is wrapped in pragmas because
 * third-party C is not warning-clean under the library's -Wall -Wextra. */
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_NO_ARCHIVE_WRITING_APIS
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "miniz.h"
/* miniz.c re-defines MZ_FILE with slightly different spelling ("void *"); drop
 * the header's spelling first so the build stays warning-free. */
#undef MZ_FILE
#include "../../third_party/miniz.c"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace repub {

namespace {

struct ZipEntryInfo {
    std::string name;
    uint64_t size = 0;
    uint32_t crc = 0;
    uint16_t method = 0;
    uint16_t flags = 0;
    bool directory = false;
    bool encrypted = false;
    mz_uint32 file_index = 0;
};

repub_err_t map_miniz_error(mz_zip_error error) {
    switch (error) {
        case MZ_ZIP_UNSUPPORTED_ENCRYPTION:
            return REPUB_ERR_PROTECTED;
        case MZ_ZIP_UNSUPPORTED_METHOD:
        case MZ_ZIP_UNSUPPORTED_FEATURE:
        case MZ_ZIP_UNSUPPORTED_MULTIDISK:
        case MZ_ZIP_UNSUPPORTED_CDIR_SIZE:
            return REPUB_ERR_UNSUPPORTED;
        case MZ_ZIP_ALLOC_FAILED:
            return REPUB_ERR_MEMORY;
        case MZ_ZIP_FILE_READ_FAILED:
        case MZ_ZIP_FILE_SEEK_FAILED:
        case MZ_ZIP_FILE_STAT_FAILED:
        case MZ_ZIP_FILE_OPEN_FAILED:
            return REPUB_ERR_IO;
        case MZ_ZIP_BUF_TOO_SMALL:
            return REPUB_ERR_BUFFER_SMALL;
        case MZ_ZIP_TOO_MANY_FILES:
            return REPUB_ERR_LIMIT;
        case MZ_ZIP_FILE_NOT_FOUND:
            return REPUB_ERR_NOT_FOUND;
        default:
            return REPUB_ERR_FORMAT;
    }
}

}  // namespace

struct ZipArchive::Impl {
    mz_zip_archive archive{};
    repub_file_t* file = nullptr;
    const repub_platform_t* platform = nullptr;
    std::vector<ZipEntryInfo> entries;
    uint64_t file_size = 0;
    bool initialized = false;

    static size_t read_callback(void* opaque, mz_uint64 offset, void* buffer, size_t count) {
        Impl* self = static_cast<Impl*>(opaque);
        if (!self || !self->platform || !self->file || !buffer) return 0;
        if (count == 0) return 0;
        const repub_platform_t* platform = self->platform;
        if (!platform->file_seek || !platform->file_read) return 0;
        if (platform->file_seek(platform->ctx, self->file, static_cast<int64_t>(offset), 0) != 0) return 0;
        size_t total = 0;
        uint8_t* out = static_cast<uint8_t*>(buffer);
        while (total < count) {
            const size_t remaining = count - total;
            const uint32_t chunk = remaining > 0x20000000u ? 0x20000000u
                                                          : static_cast<uint32_t>(remaining);
            const int32_t got = platform->file_read(platform->ctx, self->file, out + total, chunk);
            if (got <= 0) break;
            total += static_cast<size_t>(got);
        }
        return total;
    }

    static void* alloc_callback(void* opaque, size_t items, size_t size) {
        const repub_platform_t* platform = static_cast<const repub_platform_t*>(opaque);
        if (items && size > std::numeric_limits<size_t>::max() / items) return nullptr;
        const size_t total = items * size;
        return palloc(platform, total ? total : 1);
    }

    static void free_callback(void* opaque, void* address) {
        const repub_platform_t* platform = static_cast<const repub_platform_t*>(opaque);
        pfree(platform, address);
    }
};

ZipArchive::ZipArchive() = default;

ZipArchive::~ZipArchive() { close(); }

repub_err_t ZipArchive::open(const repub_platform_t* platform, const std::string& path) {
    close();
    if (path.empty()) return REPUB_ERR_INVALID_ARG;
    const repub_platform_t* effective = effective_platform(platform);
    if (!effective->file_open || !effective->file_read || !effective->file_seek ||
        !effective->file_close || !effective->file_size) {
        return REPUB_ERR_UNSUPPORTED;
    }

    Impl* impl = new (std::nothrow) Impl();
    if (!impl) return REPUB_ERR_MEMORY;
    impl->platform = effective;

    repub_file_t* file = nullptr;
    repub_err_t err = effective->file_open(effective->ctx, path.c_str(), &file);
    if (err != REPUB_OK || !file) {
        delete impl;
        return err == REPUB_OK ? REPUB_ERR_IO : err;
    }
    impl->file = file;
    const int64_t size = effective->file_size(effective->ctx, file);
    if (size < 0) {
        effective->file_close(effective->ctx, file);
        delete impl;
        return REPUB_ERR_IO;
    }
    impl->file_size = static_cast<uint64_t>(size);
    if (impl->file_size < 22) {
        /* Too small to hold an end-of-central-directory record. */
        effective->file_close(effective->ctx, file);
        delete impl;
        return REPUB_ERR_FORMAT;
    }

    mz_zip_zero_struct(&impl->archive);
    impl->archive.m_pRead = &Impl::read_callback;
    impl->archive.m_pIO_opaque = impl;
    impl->archive.m_pAlloc = &Impl::alloc_callback;
    impl->archive.m_pFree = &Impl::free_callback;
    impl->archive.m_pAlloc_opaque = const_cast<repub_platform_t*>(effective);

    if (!mz_zip_reader_init(&impl->archive, impl->file_size, 0)) {
        const repub_err_t open_err = map_miniz_error(mz_zip_get_last_error(&impl->archive));
        effective->file_close(effective->ctx, file);
        delete impl;
        return open_err == REPUB_OK ? REPUB_ERR_FORMAT : open_err;
    }
    impl->initialized = true;

    const mz_uint file_count = mz_zip_reader_get_num_files(&impl->archive);
    if (file_count > REPUB_MAX_ZIP_ENTRIES) {
        mz_zip_reader_end(&impl->archive);
        effective->file_close(effective->ctx, file);
        delete impl;
        return REPUB_ERR_LIMIT;
    }

    impl->entries.reserve(file_count);
    for (mz_uint i = 0; i < file_count; ++i) {
        mz_zip_archive_file_stat stat;
        std::memset(&stat, 0, sizeof(stat));
        if (!mz_zip_reader_file_stat(&impl->archive, i, &stat)) {
            const repub_err_t stat_err = map_miniz_error(mz_zip_get_last_error(&impl->archive));
            mz_zip_reader_end(&impl->archive);
            effective->file_close(effective->ctx, file);
            delete impl;
            return stat_err == REPUB_OK ? REPUB_ERR_FORMAT : stat_err;
        }
        ZipEntryInfo entry;
        entry.file_index = i;
        entry.size = stat.m_uncomp_size;
        entry.crc = stat.m_crc32;
        entry.method = stat.m_method;
        entry.flags = stat.m_bit_flag;
        entry.directory = stat.m_is_directory != MZ_FALSE;
        /* Bit 0 of the general purpose flags marks traditional encryption. */
        entry.encrypted = stat.m_is_encrypted != MZ_FALSE || (stat.m_bit_flag & 0x0001u) != 0;
        const mz_uint name_length = mz_zip_reader_get_filename(&impl->archive, i, nullptr, 0);
        if (name_length > 1) {
            std::vector<char> buffer(name_length);
            mz_zip_reader_get_filename(&impl->archive, i, buffer.data(), name_length);
            buffer.back() = '\0';
            entry.name.assign(buffer.data());
        }
        impl->entries.push_back(std::move(entry));
    }

    impl_ = impl;
    platform_ = effective;
    path_ = path;
    file_size_ = impl->file_size;
    open_ = true;
    return REPUB_OK;
}

void ZipArchive::close() {
    Impl* impl = impl_;
    impl_ = nullptr;
    open_ = false;
    file_size_ = 0;
    if (!impl) return;
    if (impl->initialized) {
        mz_zip_reader_end(&impl->archive);
        impl->initialized = false;
    }
    if (impl->file && impl->platform && impl->platform->file_close) {
        impl->platform->file_close(impl->platform->ctx, impl->file);
    }
    impl->file = nullptr;
    delete impl;
}

size_t ZipArchive::entry_count() const {
    return impl_ ? impl_->entries.size() : 0;
}

int ZipArchive::find(std::string_view name) const {
    if (!impl_ || name.empty()) return -1;
    /* Exact match first (duplicate names: the first central-directory entry wins). */
    for (size_t i = 0; i < impl_->entries.size(); ++i) {
        if (impl_->entries[i].name == name) return static_cast<int>(i);
    }
    for (size_t i = 0; i < impl_->entries.size(); ++i) {
        if (impl_->entries[i].name.size() == name.size() && compare_ci(impl_->entries[i].name, name) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const std::string& ZipArchive::entry_name(size_t index) const {
    static const std::string kEmpty;
    if (!impl_ || index >= impl_->entries.size()) return kEmpty;
    return impl_->entries[index].name;
}

uint64_t ZipArchive::entry_size(size_t index) const {
    if (!impl_ || index >= impl_->entries.size()) return 0;
    return impl_->entries[index].size;
}

uint32_t ZipArchive::entry_crc(size_t index) const {
    if (!impl_ || index >= impl_->entries.size()) return 0;
    return impl_->entries[index].crc;
}

uint16_t ZipArchive::entry_method(size_t index) const {
    if (!impl_ || index >= impl_->entries.size()) return 0;
    return impl_->entries[index].method;
}

repub_err_t ZipArchive::extract(size_t index, std::vector<uint8_t>* out, size_t max_bytes) const {
    if (!out) return REPUB_ERR_INVALID_ARG;
    out->clear();
    if (!impl_ || !open_ || index >= impl_->entries.size()) return REPUB_ERR_INVALID_ARG;
    const ZipEntryInfo& entry = impl_->entries[index];
    if (entry.encrypted) return REPUB_ERR_PROTECTED;
    if (max_bytes != 0 && entry.size > max_bytes) return REPUB_ERR_LIMIT;
    if (entry.size > std::numeric_limits<size_t>::max()) return REPUB_ERR_LIMIT;
    const size_t size = static_cast<size_t>(entry.size);
    if (entry.directory || size == 0) return REPUB_OK;
    out->resize(size);
    if (!mz_zip_reader_extract_to_mem_no_alloc(&impl_->archive, entry.file_index, out->data(),
                                               out->size(), 0, nullptr, 0)) {
        const repub_err_t err = map_miniz_error(mz_zip_get_last_error(&impl_->archive));
        out->clear();
        return err == REPUB_OK ? REPUB_ERR_FORMAT : err;
    }
    return REPUB_OK;
}

repub_err_t ZipArchive::extract_to_string(size_t index, std::string* out, size_t max_bytes) const {
    if (!out) return REPUB_ERR_INVALID_ARG;
    out->clear();
    std::vector<uint8_t> bytes;
    const repub_err_t err = extract(index, &bytes, max_bytes);
    if (err != REPUB_OK) return err;
    out->assign(bytes.begin(), bytes.end());
    return REPUB_OK;
}

repub_err_t ZipArchive::extract_prefix(size_t index, void* buffer, size_t cap,
                                       size_t* bytes_written) const {
    if (bytes_written) *bytes_written = 0;
    if (!impl_ || !open_ || index >= impl_->entries.size()) return REPUB_ERR_INVALID_ARG;
    if (cap && !buffer) return REPUB_ERR_INVALID_ARG;
    const ZipEntryInfo& entry = impl_->entries[index];
    if (entry.encrypted) return REPUB_ERR_PROTECTED;
    if (cap == 0 || entry.directory || entry.size == 0) return REPUB_OK;

    const size_t target = entry.size < cap ? static_cast<size_t>(entry.size) : cap;
    mz_zip_reader_extract_iter_state* state =
        mz_zip_reader_extract_iter_new(&impl_->archive, entry.file_index, 0);
    if (!state) {
        const repub_err_t err = map_miniz_error(mz_zip_get_last_error(&impl_->archive));
        return err == REPUB_OK ? REPUB_ERR_FORMAT : err;
    }
    size_t total = 0;
    uint8_t* out = static_cast<uint8_t*>(buffer);
    while (total < target) {
        const size_t got = mz_zip_reader_extract_iter_read(state, out + total, target - total);
        if (got == 0) break;
        total += got;
    }
    /* Deliberately ignore the iterator's CRC result: only a prefix was read. */
    mz_zip_reader_extract_iter_free(state);
    if (total != target) return REPUB_ERR_FORMAT;
    if (bytes_written) *bytes_written = total;
    return REPUB_OK;
}

}  // namespace repub
