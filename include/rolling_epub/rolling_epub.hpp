/*
 * RollingEPUB - portable EPUB parsing, layout and rendering library.
 *
 * SPDX-License-Identifier: MIT
 *
 * Header-only C++17 facade over the C API (rolling_epub/repub.h).
 *
 * Every class is a small RAII wrapper around one opaque C handle:
 *
 *   repub::Book        repub_book_t*
 *   repub::Chapter     repub_chapter_t*   (owned or borrowed)
 *   repub::Layout      repub_layout_t*
 *   repub::Page        non-owning view of a repub_page_t
 *   repub::Reader      repub_reader_t*    (owns its book)
 *   repub::Canvas      repub_canvas_t*
 *   repub::Image       repub_image_t*
 *   repub::NotePopup   repub_note_popup_t*
 *   repub::ImageViewer repub_image_viewer_t*
 *
 * The wrappers are move-only, never throw and never require exceptions; a
 * failed factory returns an invalid object (test with operator bool / valid())
 * and, when passed, writes the repub_err_t to the optional `err` argument.
 *
 * Ownership notes:
 *  - A Reader owns the Book it was opened/attached with.  Do not also keep a
 *    separate repub::Book for the same handle.
 *  - Chapter objects returned by Reader::chapter() borrow; ones returned by
 *    Book::open_chapter() own the parse result.
 *  - Page/Layout views borrow: a Page is only valid while its Layout (or
 *    Reader) is alive and unchanged.
 *  - NotePopup and ImageViewer must not outlive their Reader.
 *
 * Threading: as with the C API, no object may be used from two threads at the
 * same time.  Separate readers may live in separate tasks.
 */
#ifndef ROLLING_EPUB_ROLLING_EPUB_HPP
#define ROLLING_EPUB_ROLLING_EPUB_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "rolling_epub/repub.h"

namespace repub {

/* Forward declarations so the classes can reference each other. */
class Canvas;
class Image;
class Chapter;
class Page;
class Layout;
class Book;
class Reader;
class NotePopup;
class ImageViewer;

namespace detail {

struct CanvasDeleter {
    void operator()(repub_canvas_t* canvas) const {
        if (canvas) repub_canvas_destroy(canvas);
    }
};

struct ImageDeleter {
    void operator()(repub_image_t* image) const {
        if (image) repub_image_free(image);
    }
};

struct BookDeleter {
    void operator()(repub_book_t* book) const {
        if (book) repub_book_close(book);
    }
};

struct LayoutDeleter {
    void operator()(repub_layout_t* layout) const {
        if (layout) repub_layout_free(layout);
    }
};

struct ReaderDeleter {
    void operator()(repub_reader_t* reader) const {
        if (reader) repub_reader_close(reader);
    }
};

struct NotePopupDeleter {
    void operator()(repub_note_popup_t* popup) const {
        if (popup) repub_note_popup_free(popup);
    }
};

struct ImageViewerDeleter {
    void operator()(repub_image_viewer_t* viewer) const {
        if (viewer) repub_image_viewer_free(viewer);
    }
};

/* Chapter may own or borrow the C handle. */
struct ChapterDeleter {
    bool owns = true;
    void operator()(repub_chapter_t* chapter) const {
        if (chapter && owns) repub_chapter_free(chapter);
    }
};

inline std::string copy_string(const char* text) { return text ? std::string(text) : std::string(); }

}  // namespace detail

/* Fills a layout spec with the documented defaults and sets page_rect and, when
 * non-zero, the body font size. */
inline repub_layout_spec_t default_layout_spec(repub_rect_t page_rect,
                                               int32_t font_size_px = 0) {
    repub_layout_spec_t spec{};
    repub_layout_spec_defaults(&spec);
    spec.page_rect = page_rect;
    if (font_size_px > 0) spec.font_size_px = font_size_px;
    return spec;
}

/* Fills render options with the documented defaults. */
inline repub_render_options_t default_render_options() {
    repub_render_options_t options{};
    repub_render_options_defaults(&options);
    return options;
}

/* ------------------------------------------------------------------------- */
/* Canvas                                                                    */
/* ------------------------------------------------------------------------- */

class Canvas {
   public:
    Canvas() = default;

    static Canvas create(int32_t width, int32_t height,
                         repub_pixel_format_t format = REPUB_FORMAT_MONO1,
                         const repub_platform_t* platform = nullptr,
                         repub_err_t* err = nullptr) {
        Canvas canvas;
        canvas.handle_.reset(
            repub_canvas_create(platform, width, height, format, err));
        return canvas;
    }

    static Canvas wrap(void* pixels, int32_t width, int32_t height,
                       int32_t stride_bytes,
                       repub_pixel_format_t format = REPUB_FORMAT_MONO1,
                       const repub_platform_t* platform = nullptr,
                       repub_err_t* err = nullptr) {
        Canvas canvas;
        canvas.handle_.reset(repub_canvas_wrap(platform, pixels, width, height,
                                               stride_bytes, format, err));
        return canvas;
    }

    explicit operator bool() const { return handle_ != nullptr; }
    bool valid() const { return handle_ != nullptr; }
    repub_canvas_t* get() const { return handle_.get(); }

    int32_t width() const { return repub_canvas_width(handle_.get()); }
    int32_t height() const { return repub_canvas_height(handle_.get()); }
    int32_t stride() const { return repub_canvas_stride(handle_.get()); }
    repub_pixel_format_t format() const {
        return repub_canvas_format(handle_.get());
    }
    void* data() const { return repub_canvas_data(handle_.get()); }

    void clear(int32_t level) { repub_canvas_clear(handle_.get(), level); }
    void fill_rect(repub_rect_t rect, int32_t level) {
        repub_canvas_fill_rect(handle_.get(), rect, level);
    }
    void set_pixel(int32_t x, int32_t y, int32_t level) {
        repub_canvas_set_pixel(handle_.get(), x, y, level);
    }
    /* Draw an image with an explicit view (fit/zoom/pan). */
    repub_err_t draw_image(const Image& image,
                           const repub_image_view_t* view = nullptr);

    void reset() { handle_.reset(); }

   private:
    std::unique_ptr<repub_canvas_t, detail::CanvasDeleter> handle_;
};

/* ------------------------------------------------------------------------- */
/* Image                                                                     */
/* ------------------------------------------------------------------------- */

class Image {
   public:
    Image() = default;

    static Image create(int32_t width, int32_t height,
                        const repub_platform_t* platform = nullptr,
                        repub_err_t* err = nullptr) {
        Image image;
        image.handle_.reset(repub_image_create(platform, width, height, err));
        return image;
    }

    static Image decode(const void* data, size_t size, int32_t max_width = 0,
                        int32_t max_height = 0,
                        const repub_platform_t* platform = nullptr,
                        repub_err_t* err = nullptr) {
        Image image;
        const repub_err_t rc =
            repub_image_decode(platform, data, size, max_width, max_height,
                               &image.raw_);
        if (err) *err = rc;
        if (rc == REPUB_OK) image.adopt();
        return image;
    }

    static Image decode(const std::string& bytes, int32_t max_width = 0,
                        int32_t max_height = 0,
                        const repub_platform_t* platform = nullptr,
                        repub_err_t* err = nullptr) {
        return decode(bytes.data(), bytes.size(), max_width, max_height,
                      platform, err);
    }

    /* Takes ownership of a handle returned by the C API. */
    static Image adopt(repub_image_t* image) {
        Image wrapper;
        wrapper.raw_ = image;
        wrapper.adopt();
        return wrapper;
    }

    explicit operator bool() const { return handle_ != nullptr; }
    bool valid() const { return handle_ != nullptr; }

    int32_t width() const { return repub_image_width(handle_.get()); }
    int32_t height() const { return repub_image_height(handle_.get()); }
    const uint8_t* pixels() const {
        return repub_image_pixels(handle_.get());
    }
    repub_image_t* get() const { return handle_.get(); }

    /* Scaled copy; bilinear downscale, nearest/area upscale. */
    Image scale(int32_t width, int32_t height,
                const repub_platform_t* platform = nullptr,
                repub_err_t* err = nullptr) const {
        Image image;
        const repub_err_t rc = repub_image_scale(
            platform, handle_.get(), width, height, &image.raw_);
        if (err) *err = rc;
        if (rc == REPUB_OK) image.adopt();
        return image;
    }

    void reset() { handle_.reset(); }

   private:
    void adopt() { handle_.reset(raw_); }

    repub_image_t* raw_ = nullptr;
    std::unique_ptr<repub_image_t, detail::ImageDeleter> handle_;
};

/* ------------------------------------------------------------------------- */
/* Chapter                                                                   */
/* ------------------------------------------------------------------------- */

class Chapter {
   public:
    Chapter() = default;

    explicit operator bool() const { return handle_ != nullptr; }
    bool valid() const { return handle_ != nullptr; }
    repub_chapter_t* get() const { return handle_.get(); }

    size_t spine_index() const {
        return repub_chapter_spine_index(handle_.get());
    }
    std::string title() const {
        return detail::copy_string(repub_chapter_title(handle_.get()));
    }
    /* Normalized UTF-8 text, owned by the chapter. */
    std::string_view text() const {
        size_t len = 0;
        const char* text = repub_chapter_text(handle_.get(), &len);
        return std::string_view(text ? text : "", len);
    }
    size_t block_count() const {
        return repub_chapter_block_count(handle_.get());
    }
    const repub_block_info_t* block(size_t index) const {
        return repub_chapter_block(handle_.get(), index);
    }
    size_t note_count() const {
        return repub_chapter_note_count(handle_.get());
    }
    const repub_note_info_t* note(size_t index) const {
        return repub_chapter_note(handle_.get(), index);
    }
    size_t image_count() const {
        return repub_chapter_image_count(handle_.get());
    }
    const repub_chapter_image_info_t* image(size_t index) const {
        return repub_chapter_image(handle_.get(), index);
    }
    size_t link_count() const {
        return repub_chapter_link_count(handle_.get());
    }
    const repub_chapter_link_info_t* link(size_t index) const {
        return repub_chapter_link(handle_.get(), index);
    }
    repub_err_t anchor_offset(const char* anchor, uint32_t* offset) const {
        return repub_chapter_anchor_offset(handle_.get(), anchor, offset);
    }

    void reset() { handle_.reset(); }

   private:
    friend class Book;
    friend class Reader;

    static Chapter borrow(repub_chapter_t* chapter) {
        Chapter wrapper;
        wrapper.handle_ = std::unique_ptr<repub_chapter_t, detail::ChapterDeleter>(
            chapter, detail::ChapterDeleter{false});
        return wrapper;
    }

    std::unique_ptr<repub_chapter_t, detail::ChapterDeleter> handle_{
        nullptr, detail::ChapterDeleter{true}};
};

/* ------------------------------------------------------------------------- */
/* Page (non-owning view)                                                    */
/* ------------------------------------------------------------------------- */

class Page {
   public:
    Page() = default;
    explicit Page(const repub_page_t* page) : page_(page) {}

    explicit operator bool() const { return page_ != nullptr; }
    bool valid() const { return page_ != nullptr; }
    const repub_page_t* get() const { return page_; }

    const repub_page_info_t* info() const { return repub_page_info(page_); }
    size_t span_count() const {
        const repub_page_info_t* i = info();
        return i ? i->span_count : 0;
    }
    const repub_span_t* span(size_t index) const {
        return repub_page_span(page_, index);
    }
    size_t image_count() const {
        const repub_page_info_t* i = info();
        return i ? i->image_count : 0;
    }
    const repub_page_image_t* image(size_t index) const {
        return repub_page_image_at(page_, index);
    }
    size_t note_count() const {
        const repub_page_info_t* i = info();
        return i ? i->note_count : 0;
    }
    const repub_page_note_marker_t* note(size_t index) const {
        return repub_page_note_at(page_, index);
    }
    size_t link_count() const {
        const repub_page_info_t* i = info();
        return i ? i->link_count : 0;
    }
    const repub_page_link_t* link(size_t index) const {
        return repub_page_link_at(page_, index);
    }
    /* Plain text of the page (owned copy). */
    std::string text() const {
        const repub_page_info_t* i = info();
        if (!i) return {};
        std::string result;
        result.resize(static_cast<size_t>(i->end_offset - i->start_offset));
        const size_t written =
            repub_page_text(page_, result.data(), result.size() + 1);
        result.resize(written);
        return result;
    }

   private:
    const repub_page_t* page_ = nullptr;
};

/* ------------------------------------------------------------------------- */
/* Layout                                                                    */
/* ------------------------------------------------------------------------- */

class Layout {
   public:
    Layout() = default;

    static Layout create(const Chapter& chapter,
                         const repub_layout_spec_t* spec = nullptr,
                         repub_err_t* err = nullptr) {
        Layout layout;
        const repub_err_t rc = repub_layout_create(
            chapter.get(), spec, &layout.raw_);
        if (err) *err = rc;
        if (rc == REPUB_OK) layout.adopt();
        return layout;
    }

    explicit operator bool() const { return handle_ != nullptr; }
    bool valid() const { return handle_ != nullptr; }
    repub_layout_t* get() const { return handle_.get(); }

    size_t page_count() const { return repub_layout_page_count(handle_.get()); }
    Page page(size_t index) const {
        return Page(repub_layout_page(handle_.get(), index));
    }
    uint32_t page_start_offset(size_t page) const {
        return repub_layout_page_start_offset(handle_.get(), page);
    }
    size_t page_for_offset(uint32_t offset) const {
        return repub_layout_page_for_offset(handle_.get(), offset);
    }
    const repub_layout_spec_t* spec() const {
        return repub_layout_spec(handle_.get());
    }
    const repub_chapter_t* chapter() const {
        return repub_layout_chapter(handle_.get());
    }
    repub_err_t render(size_t page, Canvas& canvas,
                       const repub_render_options_t* options = nullptr) const;

    void reset() { handle_.reset(); }

   private:
    friend class Book;

    void adopt() { handle_.reset(raw_); }

    repub_layout_t* raw_ = nullptr;
    std::unique_ptr<repub_layout_t, detail::LayoutDeleter> handle_;
};

/* ------------------------------------------------------------------------- */
/* Book                                                                      */
/* ------------------------------------------------------------------------- */

class Book {
   public:
    Book() = default;

    static Book open(const char* path,
                     const repub_platform_t* platform = nullptr,
                     const repub_open_options_t* options = nullptr,
                     repub_err_t* err = nullptr) {
        Book book;
        book.handle_.reset(repub_book_open(platform, path, options, err));
        return book;
    }
    static Book open(const std::string& path,
                     const repub_platform_t* platform = nullptr,
                     const repub_open_options_t* options = nullptr,
                     repub_err_t* err = nullptr) {
        return open(path.c_str(), platform, options, err);
    }

    explicit operator bool() const { return handle_ != nullptr; }
    bool valid() const { return handle_ != nullptr; }
    repub_book_t* get() const { return handle_.get(); }
    const repub_platform_t* platform() const {
        return repub_book_platform(handle_.get());
    }
    std::string path() const {
        return detail::copy_string(repub_book_path(handle_.get()));
    }

    const repub_metadata_t* metadata() const {
        return repub_book_metadata(handle_.get());
    }
    std::string title() const {
        const repub_metadata_t* m = metadata();
        return m ? detail::copy_string(m->title) : std::string();
    }
    std::string author() const {
        const repub_metadata_t* m = metadata();
        return m ? detail::copy_string(m->author) : std::string();
    }
    std::string language() const {
        const repub_metadata_t* m = metadata();
        return m ? detail::copy_string(m->language) : std::string();
    }
    std::string identifier() const {
        const repub_metadata_t* m = metadata();
        return m ? detail::copy_string(m->identifier) : std::string();
    }
    std::string series() const {
        const repub_metadata_t* m = metadata();
        return m ? detail::copy_string(m->series) : std::string();
    }

    size_t spine_count() const { return repub_book_spine_count(handle_.get()); }
    const repub_spine_item_t* spine_item(size_t index) const {
        return repub_book_spine_item(handle_.get(), index);
    }
    size_t toc_count() const { return repub_book_toc_count(handle_.get()); }
    const repub_toc_item_t* toc_item(size_t index) const {
        return repub_book_toc_item(handle_.get(), index);
    }

    const char* cover_href() const {
        return repub_book_cover_href(handle_.get());
    }
    Image cover_image(repub_err_t* err = nullptr) const {
        repub_image_t* image = nullptr;
        const repub_err_t rc = repub_book_cover_image(handle_.get(), &image);
        if (err) *err = rc;
        return rc == REPUB_OK ? Image::adopt(image) : Image();
    }

    repub_err_t resolve_href(const char* href, int32_t* spine_index,
                             char* anchor_buf, size_t anchor_cap) const {
        return repub_book_resolve_href(handle_.get(), href, spine_index,
                                       anchor_buf, anchor_cap);
    }

    /* Reads a package resource; fills out (empty on failure). */
    repub_err_t read_resource(const char* href, std::string* out) const {
        if (out) out->clear();
        void* data = nullptr;
        size_t size = 0;
        const repub_err_t rc =
            repub_book_read_resource(handle_.get(), href, &data, &size);
        if (rc != REPUB_OK) return rc;
        if (out && data && size) out->assign(static_cast<const char*>(data), size);
        repub_free(repub_book_platform(handle_.get()), data);
        return REPUB_OK;
    }
    repub_err_t resource_size(const char* href, size_t* size) const {
        return repub_book_resource_size(handle_.get(), href, size);
    }
    repub_err_t image_dims(const char* href, int32_t* width,
                           int32_t* height) const {
        return repub_book_image_dims(handle_.get(), href, width, height);
    }
    Image image(const char* href, int32_t max_width = 0,
                int32_t max_height = 0, repub_err_t* err = nullptr) const {
        repub_image_t* image = nullptr;
        const repub_err_t rc = repub_book_image_decode(
            handle_.get(), href, max_width, max_height, &image);
        if (err) *err = rc;
        return rc == REPUB_OK ? Image::adopt(image) : Image();
    }

    repub_err_t add_font(const repub_font_desc_t* desc) {
        return repub_book_add_font(handle_.get(), desc);
    }
    repub_err_t set_body_font(const char* family,
                              repub_font_style_t style = REPUB_FONT_REGULAR) {
        return repub_book_set_body_font(handle_.get(), family, style);
    }
    repub_err_t add_font_file(const char* family, const std::string& ttf,
                              repub_font_style_t style = REPUB_FONT_REGULAR) {
        repub_font_desc_t desc{};
        desc.family = family;
        desc.data = ttf.data();
        desc.size = ttf.size();
        desc.face_index = 0;
        desc.style = style;
        return add_font(&desc);
    }
    void set_embedded_fonts(bool enable) {
        repub_book_set_embedded_fonts(handle_.get(), enable ? 1 : 0);
    }

    /* Parse one spine item into an owned chapter. */
    Chapter open_chapter(size_t spine_index, repub_err_t* err = nullptr) const {
        repub_chapter_t* chapter = nullptr;
        const repub_err_t rc =
            repub_book_chapter_open(handle_.get(), spine_index, &chapter);
        if (err) *err = rc;
        if (rc != REPUB_OK) return Chapter();
        Chapter wrapper;
        wrapper.handle_ = std::unique_ptr<repub_chapter_t, detail::ChapterDeleter>(
            chapter, detail::ChapterDeleter{true});
        return wrapper;
    }

    /* Lay out a standalone UTF-8 string (popups, excerpts, dialogs). */
    Layout layout_text(std::string_view text, const repub_layout_spec_t* spec,
                       repub_err_t* err = nullptr) const {
        Layout layout;
        const repub_err_t rc =
            repub_book_layout_text(handle_.get(), text.data(), text.size(), spec,
                                   &layout.raw_);
        if (err) *err = rc;
        if (rc == REPUB_OK) layout.adopt();
        return layout;
    }

    /* Relinquish ownership (used by Reader::attach). */
    repub_book_t* release() {
        return handle_.release();
    }

    void reset() { handle_.reset(); }

   private:
    std::unique_ptr<repub_book_t, detail::BookDeleter> handle_;
};

/* ------------------------------------------------------------------------- */
/* Reader                                                                    */
/* ------------------------------------------------------------------------- */

class Reader {
   public:
    Reader() = default;

    static Reader open(const char* path, const repub_layout_spec_t* layout = nullptr,
                       const repub_open_options_t* options = nullptr,
                       const repub_platform_t* platform = nullptr,
                       repub_err_t* err = nullptr) {
        Reader reader;
        reader.handle_.reset(
            repub_reader_open(platform, path, layout, options, err));
        return reader;
    }
    static Reader open(const std::string& path,
                       const repub_layout_spec_t* layout = nullptr,
                       const repub_open_options_t* options = nullptr,
                       const repub_platform_t* platform = nullptr,
                       repub_err_t* err = nullptr) {
        return open(path.c_str(), layout, options, platform, err);
    }

    /* Takes ownership of the book handle. */
    static Reader attach(Book book, const repub_layout_spec_t* layout = nullptr,
                         repub_err_t* err = nullptr) {
        Reader reader;
        reader.handle_.reset(repub_reader_attach(book.release(), layout, err));
        return reader;
    }

    explicit operator bool() const { return handle_ != nullptr; }
    bool valid() const { return handle_ != nullptr; }
    repub_reader_t* get() const { return handle_.get(); }

    repub_book_t* book() const { return repub_reader_book(handle_.get()); }

    size_t chapter_index() const {
        return repub_reader_chapter_index(handle_.get());
    }
    size_t chapter_count() const {
        return repub_reader_chapter_count(handle_.get());
    }
    size_t page_count() const { return repub_reader_page_count(handle_.get()); }
    size_t page_index() const { return repub_reader_page_index(handle_.get()); }

    repub_err_t set_layout(const repub_layout_spec_t* layout) {
        return repub_reader_set_layout(handle_.get(), layout);
    }
    const repub_layout_spec_t* layout_spec() const {
        return repub_reader_layout_spec(handle_.get());
    }

    repub_err_t goto_chapter(size_t chapter) {
        return repub_reader_goto_chapter(handle_.get(), chapter);
    }
    repub_err_t goto_offset(size_t chapter, uint32_t text_offset) {
        return repub_reader_goto_offset(handle_.get(), chapter, text_offset);
    }
    repub_err_t goto_href(const char* href) {
        return repub_reader_goto_href(handle_.get(), href);
    }
    repub_err_t goto_progress(float fraction) {
        return repub_reader_goto_progress(handle_.get(), fraction);
    }
    repub_err_t next_page() { return repub_reader_next_page(handle_.get()); }
    repub_err_t prev_page() { return repub_reader_prev_page(handle_.get()); }

    Page page() const { return Page(repub_reader_page(handle_.get())); }
    const repub_layout_t* layout() const {
        return repub_reader_layout(handle_.get());
    }
    /* Borrowed view of the current chapter (do not outlive the reader). */
    Chapter chapter() const {
        return Chapter::borrow(
            const_cast<repub_chapter_t*>(repub_reader_chapter(handle_.get())));
    }

    repub_err_t render(Canvas& canvas,
                       const repub_render_options_t* options = nullptr) {
        return repub_reader_render(handle_.get(), canvas.get(), options);
    }
    float progress() const { return repub_reader_progress(handle_.get()); }

    int32_t note_at(int32_t x, int32_t y) const {
        return repub_reader_note_at(handle_.get(), x, y);
    }
    repub_err_t note_text(size_t note_index, const char** text,
                          size_t* len) const {
        return repub_reader_note_text(handle_.get(), note_index, text, len);
    }
    NotePopup note_popup(size_t note_index,
                         const repub_layout_spec_t* popup_spec = nullptr,
                         repub_err_t* err = nullptr);

    int32_t image_at(int32_t x, int32_t y) const {
        return repub_reader_image_at(handle_.get(), x, y);
    }
    Image image(size_t image_index, int32_t max_width = 0,
                int32_t max_height = 0, repub_err_t* err = nullptr) const {
        repub_image_t* image = nullptr;
        const repub_err_t rc = repub_reader_image_decode(
            handle_.get(), image_index, max_width, max_height, &image);
        if (err) *err = rc;
        return rc == REPUB_OK ? Image::adopt(image) : Image();
    }
    ImageViewer image_viewer(size_t image_index, repub_err_t* err = nullptr);

    void reset() { handle_.reset(); }

   private:
    std::unique_ptr<repub_reader_t, detail::ReaderDeleter> handle_;
};

/* ------------------------------------------------------------------------- */
/* NotePopup                                                                 */
/* ------------------------------------------------------------------------- */

class NotePopup {
   public:
    NotePopup() = default;

    explicit operator bool() const { return handle_ != nullptr; }
    bool valid() const { return handle_ != nullptr; }
    repub_note_popup_t* get() const { return handle_.get(); }

    size_t page_count() const {
        return repub_note_popup_page_count(handle_.get());
    }
    repub_err_t render(size_t page, Canvas& canvas,
                       const repub_render_options_t* options = nullptr) const {
        return repub_note_popup_render(handle_.get(), page, canvas.get(),
                                       options);
    }
    const repub_layout_t* layout() const {
        return repub_note_popup_layout(handle_.get());
    }
    /* Render page 0 (most popups fit on one page). */
    repub_err_t render(Canvas& canvas,
                       const repub_render_options_t* options = nullptr) const {
        return render(0, canvas, options);
    }

    void reset() { handle_.reset(); }

   private:
    friend class Reader;
    std::unique_ptr<repub_note_popup_t, detail::NotePopupDeleter> handle_;
};

/* ------------------------------------------------------------------------- */
/* ImageViewer                                                               */
/* ------------------------------------------------------------------------- */

class ImageViewer {
   public:
    ImageViewer() = default;

    explicit operator bool() const { return handle_ != nullptr; }
    bool valid() const { return handle_ != nullptr; }
    repub_image_viewer_t* get() const { return handle_.get(); }

    /* Decoded image, owned by the viewer. */
    const repub_image_t* image() const {
        return repub_image_viewer_image(handle_.get());
    }
    repub_err_t render(Canvas& canvas, repub_rect_t dest, float zoom = 1.0f,
                       float pan_x = 0.0f, float pan_y = 0.0f,
                       const repub_render_options_t* options = nullptr) {
        return repub_image_viewer_render(handle_.get(), canvas.get(), dest, zoom,
                                         pan_x, pan_y, options);
    }
    /* Render into the whole canvas with the default fit/zoom. */
    repub_err_t render(Canvas& canvas,
                       const repub_render_options_t* options = nullptr) {
        return render(canvas, repub_rect_t{0, 0, canvas.width(), canvas.height()},
                      1.0f, 0.0f, 0.0f, options);
    }

    void reset() { handle_.reset(); }

   private:
    friend class Reader;
    std::unique_ptr<repub_image_viewer_t, detail::ImageViewerDeleter> handle_;
};

/* ---- Out-of-line member definitions needing complete types ------------ */

inline repub_err_t Canvas::draw_image(const Image& image,
                                      const repub_image_view_t* view) {
    return repub_canvas_draw_image(handle_.get(), image.get(), view);
}

inline repub_err_t Layout::render(size_t page, Canvas& canvas,
                                  const repub_render_options_t* options) const {
    return repub_layout_render(handle_.get(), page, canvas.get(), options);
}

inline NotePopup Reader::note_popup(size_t note_index,
                                    const repub_layout_spec_t* popup_spec,
                                    repub_err_t* err) {
    NotePopup popup;
    popup.handle_.reset(
        repub_reader_note_popup_open(handle_.get(), note_index, popup_spec, err));
    return popup;
}

inline ImageViewer Reader::image_viewer(size_t image_index, repub_err_t* err) {
    ImageViewer viewer;
    viewer.handle_.reset(
        repub_reader_image_viewer_open(handle_.get(), image_index, err));
    return viewer;
}

}  // namespace repub

#endif  // ROLLING_EPUB_ROLLING_EPUB_HPP
