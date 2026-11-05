/*
 * RollingEPUB book container implementation: container.xml -> OPF -> metadata,
 * manifest, spine, cover, CSS, NCX/NAV TOC, resources, embedded fonts,
 * encryption probe and the public repub_book_* API glue.
 *
 * SPDX-License-Identifier: MIT
 *
 * Provenance:
 *  - Container/OPF/spine/cover pipeline adapted from Read Pico
 *    main/book/book_epub.c (Apache-2.0).
 *  - NCX/NAV parser structure and metadata/series conventions adapted from
 *    CrossMux lib/Epub/Epub/parsers/ContentOpfParser.cpp, TocNcxParser.cpp,
 *    TocNavParser.cpp and ContainerParser.cpp (MIT).
 *  - Image dimension probing uses the render module's image_probe().
 */
#include "container/book.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <utility>

#include "container/xml.h"
#include "core/text_util.h"
#include "render/image.h"

namespace repub {

namespace {

constexpr size_t kMaxContainerXmlBytes = 4u * 1024u * 1024u;
constexpr size_t kMaxOpfBytes = 8u * 1024u * 1024u;
constexpr size_t kMaxTocBytes = 4u * 1024u * 1024u;
constexpr size_t kMaxMetadataText = 1024;
constexpr size_t kMaxTocEntries = 4096;
constexpr size_t kMaxTocTitleBytes = 512;
constexpr size_t kDefaultResourceBytes = 16u * 1024u * 1024u;
constexpr size_t kImageProbePrefixBytes = 64u * 1024u;

/* ------------------------------------------------------------------------- */
/* Small helpers                                                             */
/* ------------------------------------------------------------------------- */

std::string xml_local(XmlParser& parser) {
    std::string local;
    parser.local_name(&local);
    return local;
}

bool list_has_word(std::string_view list, std::string_view word) {
    size_t index = 0;
    while (index < list.size()) {
        while (index < list.size() && static_cast<unsigned char>(list[index]) <= ' ') ++index;
        const size_t start = index;
        while (index < list.size() && static_cast<unsigned char>(list[index]) > ' ') ++index;
        if (start == index) break;
        if (compare_ci(list.substr(start, index - start), word) == 0) return true;
    }
    return false;
}

std::string clamp_text(std::string value, size_t max_bytes) {
    if (value.size() <= max_bytes) return value;
    size_t cut = max_bytes;
    while (cut > 0 && (static_cast<unsigned char>(value[cut]) & 0xC0) == 0x80) --cut;
    value.resize(cut);
    return value;
}

/* Collect the flattened text content of the element the parser is positioned
 * on; consumes tokens through the matching end tag. */
std::string collect_element_text(XmlParser& parser, size_t max_bytes = kMaxMetadataText) {
    std::string out;
    if (parser.self_closing()) return out;
    const int depth = parser.depth();
    while (parser.next()) {
        if (parser.token() == XmlParser::Token::EndElement && parser.depth() == depth) break;
        if (parser.token() == XmlParser::Token::Text || parser.token() == XmlParser::Token::Cdata) {
            if (out.size() >= max_bytes) continue;
            const std::string& piece = parser.text();
            const size_t room = max_bytes - out.size();
            out.append(piece.data(), piece.size() < room ? piece.size() : room);
        }
        if (parser.failed()) break;
    }
    return out;
}

std::string clean_meta_text(std::string_view raw) {
    return clamp_text(collapse_whitespace(trim(raw)), kMaxMetadataText);
}

/* Append metadata text: first value wins, except creator (comma joined). */
void append_first(std::string& slot, std::string_view raw) {
    if (!slot.empty()) return;
    slot = clean_meta_text(raw);
}

void append_joined(std::string& slot, std::string_view raw) {
    std::string clean = clean_meta_text(raw);
    if (clean.empty()) return;
    if (slot.empty()) {
        slot = std::move(clean);
    } else {
        slot += ", ";
        slot += clean;
    }
}

std::string file_basename(std::string_view path) {
    const size_t pos = path.find_last_of('/');
    return std::string(pos == std::string_view::npos ? path : path.substr(pos + 1));
}

std::string strip_extension(std::string_view name) {
    const size_t pos = name.find_last_of('.');
    return std::string(pos == std::string_view::npos ? name : name.substr(0, pos));
}

bool starts_with_image_media(std::string_view media_type) {
    return starts_with_ci(media_type, "image/");
}

/* ------------------------------------------------------------------------- */
/* Resource lookup                                                           */
/* ------------------------------------------------------------------------- */

/* Resolve an href against the OPF location and return the ZIP central
 * directory index.  Accepts both OPF-relative ("images/x.png") and
 * package-relative ("OEBPS/images/x.png") hrefs. */
repub_err_t find_resource_entry(const repub_book& book, std::string_view href,
                                int* zip_index, uint64_t* entry_size) {
    if (!zip_index) return REPUB_ERR_INVALID_ARG;
    *zip_index = -1;
    if (entry_size) *entry_size = 0;
    if (href.empty() || is_external_href(href)) return REPUB_ERR_NOT_FOUND;

    std::string file;
    std::string fragment;
    split_fragment(href, &file, &fragment);
    if (file.empty()) return REPUB_ERR_NOT_FOUND;

    std::string candidates[4];
    size_t count = 0;
    candidates[count++] = resolve_path(book.opf_dir, file);
    candidates[count++] = normalize_path(file);
    const std::string decoded = percent_decode(normalize_path(file));
    candidates[count++] = decoded;
    candidates[count++] = percent_decode(resolve_path(book.opf_dir, file));

    for (size_t i = 0; i < count; ++i) {
        if (candidates[i].empty()) continue;
        bool duplicate = false;
        for (size_t j = 0; j < i; ++j) {
            if (candidates[j] == candidates[i]) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        const int index = book.zip.find(candidates[i]);
        if (index >= 0) {
            *zip_index = index;
            if (entry_size) *entry_size = book.zip.entry_size(static_cast<size_t>(index));
            return REPUB_OK;
        }
    }
    return REPUB_ERR_NOT_FOUND;
}

/* Resolve a TOC target to a package-relative ZIP path.
 * `base_dir` is the directory of the document that carries the href. */
std::string resolve_toc_target(std::string_view base_dir, std::string_view raw_href,
                               std::string* anchor) {
    std::string file;
    std::string fragment;
    split_fragment(raw_href, &file, &fragment);
    if (anchor) *anchor = percent_decode(fragment);
    if (file.empty()) return std::string();
    if (is_external_href(file)) return file;
    return resolve_path(base_dir, file);
}

int32_t spine_index_for_target(const repub_book& book, const std::string& target) {
    if (target.empty() || is_external_href(target)) return -1;
    return book.spine_index_for_href(target);
}

/* ------------------------------------------------------------------------- */
/* OPF parsing                                                               */
/* ------------------------------------------------------------------------- */

repub_err_t parse_container(repub_book* book, std::string* opf_path) {
    const int index = book->zip.find("META-INF/container.xml");
    if (index < 0) return REPUB_ERR_FORMAT;
    std::string xml;
    repub_err_t err = book->zip.extract_to_string(static_cast<size_t>(index), &xml,
                                                  kMaxContainerXmlBytes);
    if (err != REPUB_OK) return err;

    XmlParser parser(xml);
    bool root_container = false;
    bool found = false;
    while (parser.next()) {
        if (parser.token() != XmlParser::Token::StartElement) continue;
        const std::string local = xml_local(parser);
        if (parser.depth() == 1 && local == "container") {
            root_container = true;
            continue;
        }
        if (root_container && local == "rootfile") {
            const std::string full_path = parser.attr("full-path");
            const std::string media_type = parser.attr("media-type");
            if (full_path.empty()) continue;
            if (!media_type.empty() && compare_ci(media_type, "application/oebps-package+xml") != 0) {
                continue;
            }
            *opf_path = resolve_path("", full_path);
            found = !opf_path->empty();
            if (found) break;
        }
    }
    if (parser.failed() || !root_container || !found) return REPUB_ERR_FORMAT;
    return REPUB_OK;
}

struct SpineRef {
    std::string idref;
    bool linear = true;
};

void set_cover_from_manifest(repub_book* book, const std::string& cover_id) {
#if REPUB_ENABLE_COVER
    if (!cover_id.empty()) {
        const ManifestItem* item = book->manifest_by_id(cover_id);
        if (item && starts_with_image_media(item->media_type)) book->cover_href = item->href;
    }
    if (book->cover_href.empty()) {
        for (const ManifestItem& item : book->manifest) {
            if (list_has_word(item.properties, "cover-image") && starts_with_image_media(item.media_type)) {
                book->cover_href = item.href;
                break;
            }
        }
    }
    if (book->cover_href.empty()) {
        for (const ManifestItem& item : book->manifest) {
            if (!starts_with_image_media(item.media_type)) continue;
            const std::string stem = to_lower(strip_extension(file_basename(item.href)));
            if (stem == "cover" || stem == "frontcover" || stem == "front-cover" ||
                stem == "cover-image" || stem == "\u5c01\u9762") {
                book->cover_href = item.href;
                break;
            }
        }
    }
#else
    (void)book;
    (void)cover_id;
#endif
}

repub_err_t parse_opf(repub_book* book) {
    const int index = book->zip.find(book->opf_path);
    if (index < 0) return REPUB_ERR_FORMAT;
    std::string xml;
    repub_err_t err = book->zip.extract_to_string(static_cast<size_t>(index), &xml, kMaxOpfBytes);
    if (err != REPUB_OK) return err;

    XmlParser parser(xml);
    bool root_package = false;
    bool in_metadata = false;
    bool in_manifest = false;
    bool in_spine = false;
    int metadata_depth = -1;
    int manifest_depth = -1;
    int spine_depth = -1;
    std::string cover_id;
    std::string spine_toc_id;
    std::vector<SpineRef> spine_refs;
    std::string nav_href;
    std::string ncx_href;

    while (parser.next()) {
        const XmlParser::Token token = parser.token();
        const std::string local = xml_local(parser);

        if (token == XmlParser::Token::StartElement) {
            if (parser.depth() == 1 && local == "package") {
                root_package = true;
                continue;
            }
            if (local == "metadata") {
                in_metadata = true;
                metadata_depth = parser.depth();
                continue;
            }
            if (local == "manifest") {
                in_manifest = true;
                manifest_depth = parser.depth();
                continue;
            }
            if (local == "spine") {
                in_spine = true;
                spine_depth = parser.depth();
                spine_toc_id = parser.attr("toc");
                continue;
            }

            if (in_metadata && parser.depth() == metadata_depth + 1) {
                if (local == "title") {
                    append_first(book->title, collect_element_text(parser));
                } else if (local == "creator") {
                    append_joined(book->author, collect_element_text(parser));
                } else if (local == "language") {
                    append_first(book->language, collect_element_text(parser));
                } else if (local == "publisher") {
                    append_first(book->publisher, collect_element_text(parser));
                } else if (local == "identifier") {
                    append_first(book->identifier, collect_element_text(parser));
                } else if (local == "date") {
                    append_first(book->date, collect_element_text(parser));
                } else if (local == "description") {
                    append_first(book->description, collect_element_text(parser));
                } else if (local == "meta") {
                    const std::string property = to_lower(parser.attr("property"));
                    const std::string name = to_lower(parser.attr("name"));
                    const std::string content = parser.attr("content");
                    if (name == "cover") {
                        cover_id = trim(content);
                    } else if (name == "calibre:series") {
                        if (book->series.empty()) book->series = clean_meta_text(content);
                    } else if (name == "calibre:series_index") {
                        if (book->series_index < 0.0f) {
                            book->series_index = parse_float(trim(content), -1.0f);
                        }
                    }
                    if (property == "belongs-to-collection") {
                        const std::string text = clean_meta_text(collect_element_text(parser));
                        if (book->series.empty() && !text.empty()) book->series = text;
                    } else if (property == "group-position") {
                        const std::string text = trim(collect_element_text(parser));
                        const float value = parse_float(text, -1.0f);
                        if (book->series_index < 0.0f && value >= 0.0f) book->series_index = value;
                    } else if (property == "collection-type") {
                        /* Consume the element text; series detection uses belongs-to-collection. */
                        collect_element_text(parser);
                    }
                }
                continue;
            }

            if (in_manifest && parser.depth() == manifest_depth + 1 && local == "item") {
                ManifestItem item;
                item.id = parser.attr("id");
                const std::string raw_href = parser.attr("href");
                item.media_type = parser.attr("media-type");
                item.properties = parser.attr("properties");
                if (item.id.empty() || raw_href.empty()) continue;
                item.href = resolve_path(book->opf_dir, raw_href);
                if (item.href.empty()) continue;
                if (to_lower(item.media_type) == "text/css") book->css_hrefs.push_back(item.href);
                if (list_has_word(item.properties, "nav") && nav_href.empty()) nav_href = item.href;
                if (to_lower(item.media_type) == "application/x-dtbncx+xml" && ncx_href.empty()) {
                    ncx_href = item.href;
                }
                book->manifest.push_back(std::move(item));
                continue;
            }

            if (in_spine && parser.depth() == spine_depth + 1 && local == "itemref") {
                SpineRef ref;
                ref.idref = parser.attr("idref");
                ref.linear = compare_ci(trim(parser.attr("linear")), "no") != 0;
                if (!ref.idref.empty()) spine_refs.push_back(std::move(ref));
                continue;
            }
            continue;
        }

        if (token == XmlParser::Token::EndElement) {
            if (in_metadata && local == "metadata" && parser.depth() == metadata_depth) {
                in_metadata = false;
                continue;
            }
            if (in_manifest && local == "manifest" && parser.depth() == manifest_depth) {
                in_manifest = false;
                continue;
            }
            if (in_spine && local == "spine" && parser.depth() == spine_depth) {
                in_spine = false;
                continue;
            }
            if (local == "package" && parser.depth() == 1) break;
        }
    }
    if (parser.failed() || !root_package) return REPUB_ERR_FORMAT;

    /* Spine: every itemref keeps its position so TOC indices stay stable even
     * when a manifest reference is missing. */
    if (spine_refs.size() > REPUB_MAX_SPINE_ITEMS) return REPUB_ERR_LIMIT;
    book->spine.clear();
    book->spine.reserve(spine_refs.size());
    for (const SpineRef& ref : spine_refs) {
        SpineItem item;
        item.idref = ref.idref;
        item.linear = ref.linear;
        const ManifestItem* manifest_item = book->manifest_by_id(ref.idref);
        if (manifest_item) {
            item.href = manifest_item->href;
            item.media_type = manifest_item->media_type;
            item.id = manifest_item->id;
            const int zip_index = book->zip.find(item.href);
            if (zip_index >= 0) {
                const uint64_t size = book->zip.entry_size(static_cast<size_t>(zip_index));
                item.size = static_cast<uint32_t>(size > 0xFFFFFFFFull ? 0xFFFFFFFFull : size);
            } else {
                logf(book->platform, LOG_WARN, "spine item missing from archive: %s", item.href.c_str());
            }
        } else {
            logf(book->platform, LOG_WARN, "spine idref not in manifest: %s", ref.idref.c_str());
        }
        book->spine.push_back(std::move(item));
    }
    if (book->spine.empty()) return REPUB_ERR_FORMAT;

    /* NCX from the spine toc attribute wins over a bare manifest media type. */
    if (!spine_toc_id.empty()) {
        const ManifestItem* item = book->manifest_by_id(spine_toc_id);
        if (item && !item->href.empty()) ncx_href = item->href;
    }
    book->nav_href = std::move(nav_href);
    book->ncx_href = std::move(ncx_href);
    set_cover_from_manifest(book, cover_id);

    size_t content_bytes = 0;
    for (const SpineItem& item : book->spine) content_bytes += item.size;
    book->content_bytes = content_bytes;

    return REPUB_OK;
}

/* ------------------------------------------------------------------------- */
/* Encryption probe                                                          */
/* ------------------------------------------------------------------------- */

void probe_encryption(repub_book* book) {
#if REPUB_ENABLE_ENCRYPTION_PROBE
    const int index = book->zip.find("META-INF/encryption.xml");
    if (index < 0) return;
    book->protected_entries = true;
    std::string xml;
    if (book->zip.extract_to_string(static_cast<size_t>(index), &xml, kMaxContainerXmlBytes) ==
            REPUB_OK &&
        contains_ci(xml, "EncryptionMethod")) {
        if (contains_ci(xml, "aes128") || contains_ci(xml, "aes256")) {
            book->protection_error =
                "book contains encrypted resources (AES); decryption is not supported";
        } else {
            book->protection_error =
                "book contains encrypted resources; decryption is not supported";
        }
    } else {
        book->protection_error = "book contains an encryption manifest; decryption is not supported";
    }
    logf(book->platform, LOG_WARN, "%s", book->protection_error.c_str());
#else
    (void)book;
#endif
}

/* ------------------------------------------------------------------------- */
/* TOC parsing                                                               */
/* ------------------------------------------------------------------------- */

struct TocOrder {
    int play_order = -1;
    size_t index = 0;
};

void add_toc_entry(repub_book* book, std::string title, std::string href_zip,
                   std::string anchor, int level, int32_t spine_index) {
    if (book->toc.size() >= kMaxTocEntries) return;
    title = clamp_text(collapse_whitespace(trim(title)), kMaxTocTitleBytes);
    anchor = clamp_text(trim(anchor), kMaxTocTitleBytes);
    if (title.empty() && href_zip.empty()) return;
    if (spine_index < 0 && !href_zip.empty()) spine_index = spine_index_for_target(*book, href_zip);
    TocEntry entry;
    entry.title = std::move(title);
    entry.href = std::move(href_zip);
    entry.anchor = std::move(anchor);
    if (!entry.anchor.empty()) {
        entry.href += '#';
        entry.href += entry.anchor;
    }
    entry.level = level < 0 ? 0 : level;
    entry.spine_index = spine_index;
    book->toc.push_back(std::move(entry));
}

bool is_toc_nav(XmlParser& parser) {
    const std::string epub_type = to_lower(parser.attr("epub:type", parser.attr("type")));
    if (list_has_word(epub_type, "toc")) return true;
    const std::string role = to_lower(parser.attr("role"));
    return list_has_word(role, "doc-toc");
}

bool parse_ncx_toc(repub_book* book, const std::string& ncx_href) {
    std::string xml;
    if (book->zip.find(ncx_href) < 0) return false;
    if (book_read_resource_string(*book, ncx_href, &xml, kMaxTocBytes) != REPUB_OK) return false;

    const std::string base_dir = dirname_of(ncx_href);
    struct NcxNode {
        int element_depth = 0;
        int level = 0;
        int play_order = -1;
        std::string title;
        std::string src;
    };

    XmlParser parser(xml);
    bool root_ncx = false;
    bool in_nav_map = false;
    int nav_map_depth = -1;
    std::vector<NcxNode> stack;
    std::vector<TocOrder> order;

    while (parser.next()) {
        const XmlParser::Token token = parser.token();
        const std::string local = xml_local(parser);
        if (token == XmlParser::Token::StartElement) {
            if (local == "ncx" && parser.depth() == 1) {
                root_ncx = true;
                continue;
            }
            if (!root_ncx) continue;
            if (local == "navMap") {
                in_nav_map = true;
                nav_map_depth = parser.depth();
                continue;
            }
            if (!in_nav_map) continue;
            if (local == "navPoint") {
                NcxNode node;
                node.element_depth = parser.depth();
                node.level = static_cast<int>(stack.size());
                node.play_order = parse_int(parser.attr("playOrder"), -1);
                if (parser.self_closing()) {
                    const std::string title;
                    add_toc_entry(book, "", "", "", node.level, -1);
                } else {
                    stack.push_back(std::move(node));
                }
                continue;
            }
            if (stack.empty()) continue;
            if (local == "text") {
                stack.back().title = collect_element_text(parser);
                continue;
            }
            if (local == "content") {
                stack.back().src = parser.attr("src");
                continue;
            }
            continue;
        }
        if (token != XmlParser::Token::EndElement) continue;
        if (local == "navPoint" && !stack.empty() && stack.back().element_depth == parser.depth()) {
            NcxNode node = std::move(stack.back());
            stack.pop_back();
            std::string anchor;
            const std::string target = resolve_toc_target(base_dir, node.src, &anchor);
            if (!node.title.empty() || !target.empty()) {
                const size_t index = book->toc.size();
                add_toc_entry(book, node.title, target, anchor, node.level, -1);
                if (book->toc.size() > index) order.push_back(TocOrder{node.play_order, index});
            }
            continue;
        }
        if (local == "navMap" && parser.depth() == nav_map_depth) {
            in_nav_map = false;
            break;
        }
        if (local == "ncx" && parser.depth() == 1) break;
    }
    if (parser.failed()) return false;

    /* Honour playOrder when every navPoint provided one. */
    if (!order.empty() && order.size() == book->toc.size()) {
        bool complete = true;
        for (const TocOrder& entry : order) {
            if (entry.play_order < 0) {
                complete = false;
                break;
            }
        }
        if (complete) {
            std::stable_sort(order.begin(), order.end(),
                             [](const TocOrder& a, const TocOrder& b) {
                                 return a.play_order < b.play_order;
                             });
            std::vector<TocEntry> sorted;
            sorted.reserve(book->toc.size());
            for (const TocOrder& entry : order) sorted.push_back(std::move(book->toc[entry.index]));
            book->toc = std::move(sorted);
        }
    }
    return true;
}

bool parse_nav_toc(repub_book* book, const std::string& nav_href) {
    if (book->zip.find(nav_href) < 0) return false;
    std::string xml;
    if (book_read_resource_string(*book, nav_href, &xml, kMaxTocBytes) != REPUB_OK) return false;

    const std::string base_dir = dirname_of(nav_href);
    XmlParser parser(xml);
    bool in_toc = false;
    int toc_nav_depth = -1;
    int ol_depth = 0;
    bool in_li = false;
    int li_depth = -1;

    while (parser.next()) {
        const XmlParser::Token token = parser.token();
        const std::string local = xml_local(parser);
        if (token == XmlParser::Token::StartElement) {
            if (local == "nav") {
                if (!in_toc && is_toc_nav(parser)) {
                    in_toc = true;
                    toc_nav_depth = parser.depth();
                }
                continue;
            }
            if (!in_toc) continue;
            if (local == "ol") {
                ++ol_depth;
                continue;
            }
            if (local == "li") {
                in_li = true;
                li_depth = parser.depth();
                continue;
            }
            if (local == "a" && in_li) {
                const std::string raw_href = parser.attr("href");
                const std::string title = collect_element_text(parser);
                std::string anchor;
                const std::string target = resolve_toc_target(base_dir, raw_href, &anchor);
                add_toc_entry(book, title, target, anchor, ol_depth - 1, -1);
                continue;
            }
            if (local == "span" && in_li) {
                const std::string title = collect_element_text(parser);
                if (!clean_meta_text(title).empty()) {
                    add_toc_entry(book, title, std::string(), std::string(), ol_depth - 1, -1);
                }
                continue;
            }
            continue;
        }
        if (token != XmlParser::Token::EndElement) continue;
        if (local == "nav" && in_toc && parser.depth() == toc_nav_depth) {
            in_toc = false;
            break;
        }
        if (!in_toc) continue;
        if (local == "li" && in_li && parser.depth() == li_depth) {
            in_li = false;
            continue;
        }
        if (local == "ol" && ol_depth > 0) --ol_depth;
    }
    return !parser.failed();
}

void rebuild_public_spine(repub_book* book) {
    book->public_spine.clear();
    book->public_spine.reserve(book->spine.size());
    for (const SpineItem& item : book->spine) {
        repub_spine_item_t pub{};
        pub.href = item.href.c_str();
        pub.media_type = item.media_type.c_str();
        pub.id = item.id.c_str();
        pub.linear = item.linear ? 1 : 0;
        pub.size = item.size;
        book->public_spine.push_back(pub);
    }
}

void rebuild_public_toc(repub_book* book) {
    book->public_toc.clear();
    book->public_toc.reserve(book->toc.size());
    for (const TocEntry& entry : book->toc) {
        repub_toc_item_t pub{};
        pub.title = entry.title.c_str();
        pub.href = entry.href.empty() ? nullptr : entry.href.c_str();
        pub.level = entry.level;
        pub.spine_index = entry.spine_index;
        book->public_toc.push_back(pub);
    }
}

/* ------------------------------------------------------------------------- */
/* Chapter heading fallback                                                  */
/* ------------------------------------------------------------------------- */

std::string heading_title_from_html(std::string_view html) {
    for (size_t index = 0; index + 4 < html.size(); ++index) {
        if (html[index] != '<') continue;
        const char tag = html[index + 1];
        if (tag != 'h' && tag != 'H') continue;
        const char digit = html[index + 2];
        if (digit < '1' || digit > '6') continue;
        const char after = html[index + 3];
        if (after != '>' && after != ' ' && after != '\t' && after != '\r' && after != '\n') continue;
        const size_t open_end = html.find('>', index + 3);
        if (open_end == std::string_view::npos) return std::string();
        std::string title;
        size_t cursor = open_end + 1;
        bool closed = false;
        while (cursor < html.size() && title.size() < kMaxTocTitleBytes) {
            const size_t tag_start = html.find('<', cursor);
            const size_t text_end = tag_start == std::string_view::npos ? html.size() : tag_start;
            if (text_end > cursor) title += decode_entities(html.substr(cursor, text_end - cursor));
            if (tag_start == std::string_view::npos) break;
            const size_t tag_end = html.find('>', tag_start);
            if (tag_end == std::string_view::npos) break;
            /* Closing </hN> ends the heading; nested inline tags are skipped. */
            if (tag_start + 2 < html.size() && html[tag_start + 1] == '/') {
                const size_t name_start = tag_start + 2;
                if (name_start < html.size() && (html[name_start] == 'h' || html[name_start] == 'H')) {
                    closed = true;
                    break;
                }
            }
            cursor = tag_end + 1;
        }
        (void)closed;
        const std::string clean = clean_meta_text(title);
        if (!clean.empty()) return clamp_text(clean, kMaxTocTitleBytes);
        index = open_end;
    }
    return std::string();
}

}  // namespace

/* ------------------------------------------------------------------------- */
/* repub_book methods                                                        */
/* ------------------------------------------------------------------------- */

const ManifestItem* repub_book::manifest_by_id(std::string_view id) const {
    if (id.empty()) return nullptr;
    for (const ManifestItem& item : manifest) {
        if (item.id == id) return &item;
    }
    return nullptr;
}

int32_t repub_book::spine_index_for_href(std::string_view href) const {
    std::string file;
    std::string fragment;
    split_fragment(href, &file, &fragment);
    if (file.empty()) return -1;
    const std::string normalized = normalize_path(file);
    const std::string decoded = normalize_path(percent_decode(file));
    auto match = [this](const std::string& candidate) -> int32_t {
        if (candidate.empty()) return -1;
        for (size_t i = 0; i < spine.size(); ++i) {
            if (spine[i].href == candidate) return static_cast<int32_t>(i);
        }
        for (size_t i = 0; i < spine.size(); ++i) {
            if (normalize_path(percent_decode(spine[i].href)) == candidate) return static_cast<int32_t>(i);
        }
        return -1;
    };
    int32_t index = match(normalized);
    if (index < 0 && decoded != normalized) index = match(decoded);
    return index;
}

/* ------------------------------------------------------------------------- */
/* Book open/close                                                           */
/* ------------------------------------------------------------------------- */

repub_err_t book_open(repub_book* book, const repub_platform_t* platform,
                      const char* path, const repub_open_options_t* options) {
    if (!book) return REPUB_ERR_INVALID_ARG;
    book_close(book);
    if (!path || !path[0]) return REPUB_ERR_INVALID_ARG;

    book->platform = effective_platform(platform);
    book->path = path;
    book->options = repub_open_options_t{};
    book->options.enable_css = 1;
    bool enable_css = true;
    if (options) {
        book->options = *options;
        const char* user_css = options->user_css;
        if (user_css) {
            const size_t length = options->user_css_len ? options->user_css_len : std::strlen(user_css);
            book->user_css.assign(user_css, length);
        }
        if (options->struct_size != 0 && options->enable_css == 0) enable_css = false;
        book->options.enable_css = enable_css ? 1 : 0;
    }

    repub_err_t err = book->zip.open(book->platform, book->path);
    if (err != REPUB_OK) {
        book_close(book);
        return err;
    }

    err = parse_container(book, &book->opf_path);
    if (err == REPUB_OK) book->opf_dir = dirname_of(book->opf_path);
    if (err == REPUB_OK) err = parse_opf(book);
    if (err != REPUB_OK) {
        book_close(book);
        return err;
    }
    probe_encryption(book);
    rebuild_public_spine(book);

    book->fonts.reset(new (std::nothrow) FontManager(book->platform));
    if (!book->fonts) {
        book_close(book);
        return REPUB_ERR_MEMORY;
    }

#if REPUB_ENABLE_CSS
    if (enable_css) {
        err = book_load_css(book);
        if (err != REPUB_OK) logf(book->platform, LOG_WARN, "stylesheet load failed");
    }
#else
    (void)enable_css;
#endif

    if (options && (options->flags & REPUB_OPEN_SKIP_TOC)) {
        book->toc.clear();
        book->toc_loaded = true;
        rebuild_public_toc(book);
    } else {
        book_load_toc(book);
    }

    book_load_embedded_fonts(book);

    book->metadata = repub_metadata_t{};
    book->metadata.title = book->title.c_str();
    book->metadata.author = book->author.c_str();
    book->metadata.language = book->language.c_str();
    book->metadata.publisher = book->publisher.c_str();
    book->metadata.identifier = book->identifier.c_str();
    book->metadata.date = book->date.c_str();
    book->metadata.description = book->description.c_str();
    book->metadata.series = book->series.c_str();
    book->metadata.series_index = book->series_index;

    book->opened = true;
    return REPUB_OK;
}

void book_close(repub_book* book) {
    if (!book) return;
    book->zip.close();
    book->manifest.clear();
    book->spine.clear();
    book->toc.clear();
    book->css_hrefs.clear();
    book->embedded_fonts.clear();
    book->nav_href.clear();
    book->ncx_href.clear();
    book->public_spine.clear();
    book->public_toc.clear();
    book->css.clear();
    book->fonts.reset();
    book->opf_path.clear();
    book->opf_dir.clear();
    book->title.clear();
    book->author.clear();
    book->language.clear();
    book->publisher.clear();
    book->identifier.clear();
    book->date.clear();
    book->description.clear();
    book->series.clear();
    book->series_index = -1.0f;
    book->cover_href.clear();
    book->body_font.clear();
    book->user_css.clear();
    book->protection_error.clear();
    book->protected_entries = false;
    book->content_bytes = 0;
    book->toc_loaded = false;
    book->css_loaded = false;
    book->embedded_fonts_enabled = true;
    book->opened = false;
    book->options = repub_open_options_t{};
    book->metadata = repub_metadata_t{};
    book->path.clear();
    book->platform = nullptr;
}

/* ------------------------------------------------------------------------- */
/* CSS / fonts / TOC loading                                                 */
/* ------------------------------------------------------------------------- */

repub_err_t book_load_css(repub_book* book) {
    if (!book) return REPUB_ERR_INVALID_ARG;
    if (book->css_loaded) return REPUB_OK;
#if REPUB_ENABLE_CSS
    book->css.clear();
    for (const std::string& href : book->css_hrefs) {
        std::string css;
        const repub_err_t err =
            book_read_resource_string(*book, href, &css, REPUB_MAX_CHAPTER_BYTES);
        if (err != REPUB_OK) {
            logf(book->platform, LOG_WARN, "cannot read stylesheet: %s", href.c_str());
            continue;
        }
        book->css.add_stylesheet(css);
    }
    if (!book->user_css.empty()) book->css.add_stylesheet(book->user_css);

    book->embedded_fonts.clear();
    for (const CssFontFace& face : book->css.font_faces()) {
        if (face.family.empty() || face.src_href.empty()) continue;
        EmbeddedFontRef ref;
        ref.family = face.family;
        ref.href = normalize_path(face.src_href);
        ref.weight = face.weight;
        ref.italic = face.italic;
        book->embedded_fonts.push_back(std::move(ref));
    }
#endif
    book->css_loaded = true;
    return REPUB_OK;
}

repub_err_t book_load_embedded_fonts(repub_book* book) {
    if (!book || !book->fonts) return book ? REPUB_ERR_INVALID_ARG : REPUB_ERR_INVALID_ARG;
    if (!book->embedded_fonts_enabled) return REPUB_OK;
#if REPUB_ENABLE_TTF
    for (const EmbeddedFontRef& ref : book->embedded_fonts) {
        if (ref.href.empty()) continue;
        std::vector<uint8_t> data;
        const repub_err_t err =
            book_read_resource(*book, ref.href, &data, REPUB_MAX_IMAGE_BYTES);
        if (err != REPUB_OK) {
            logf(book->platform, LOG_WARN, "cannot read embedded font: %s", ref.href.c_str());
            continue;
        }
        const int style = (ref.weight >= 600 ? REPUB_FONT_BOLD : REPUB_FONT_REGULAR) |
                          (ref.italic ? REPUB_FONT_ITALIC : 0);
        const repub_err_t add = book->fonts->add_face(ref.family, data.data(), data.size(), 0, style);
        if (add != REPUB_OK) {
            logf(book->platform, LOG_WARN, "cannot add embedded font: %s", ref.family.c_str());
        }
    }
#endif
    return REPUB_OK;
}

repub_err_t book_load_toc(repub_book* book) {
    if (!book) return REPUB_ERR_INVALID_ARG;
    book->toc.clear();
#if REPUB_ENABLE_TOC
    bool parsed = false;
    if (!book->nav_href.empty()) parsed = parse_nav_toc(book, book->nav_href);
    if (!parsed || book->toc.empty()) {
        if (!book->ncx_href.empty()) parse_ncx_toc(book, book->ncx_href);
    }
#endif
    book->toc_loaded = true;
    rebuild_public_toc(book);
    return REPUB_OK;
}

/* ------------------------------------------------------------------------- */
/* Resources                                                                 */
/* ------------------------------------------------------------------------- */

repub_err_t book_read_resource(const repub_book& book, std::string_view href,
                               std::vector<uint8_t>* out, size_t max_bytes) {
    if (!out) return REPUB_ERR_INVALID_ARG;
    out->clear();
    int index = -1;
    uint64_t size = 0;
    const repub_err_t err = find_resource_entry(book, href, &index, &size);
    if (err != REPUB_OK) return err;
    const size_t limit = max_bytes ? max_bytes : kDefaultResourceBytes;
    return book.zip.extract(static_cast<size_t>(index), out, limit);
}

repub_err_t book_read_resource_string(const repub_book& book, std::string_view href,
                                      std::string* out, size_t max_bytes) {
    if (!out) return REPUB_ERR_INVALID_ARG;
    out->clear();
    int index = -1;
    uint64_t size = 0;
    const repub_err_t err = find_resource_entry(book, href, &index, &size);
    if (err != REPUB_OK) return err;
    const size_t limit = max_bytes ? max_bytes : kDefaultResourceBytes;
    return book.zip.extract_to_string(static_cast<size_t>(index), out, limit);
}

repub_err_t book_image_dims(const repub_book& book, std::string_view href,
                            int32_t* width, int32_t* height) {
    if (!width || !height) return REPUB_ERR_INVALID_ARG;
    *width = 0;
    *height = 0;
#if REPUB_ENABLE_IMAGES
    int index = -1;
    uint64_t size = 0;
    const repub_err_t err = find_resource_entry(book, href, &index, &size);
    if (err != REPUB_OK) return err;
    if (size == 0) return REPUB_ERR_FORMAT;

    const size_t prefix = size < kImageProbePrefixBytes ? static_cast<size_t>(size)
                                                        : kImageProbePrefixBytes;
    std::vector<uint8_t> buffer(prefix);
    size_t written = 0;
    repub_err_t probe_err =
        book.zip.extract_prefix(static_cast<size_t>(index), buffer.data(), prefix, &written);
    if (probe_err != REPUB_OK) return probe_err;
    probe_err = image_probe(buffer.data(), written, width, height);
    if (probe_err == REPUB_OK) return REPUB_OK;
    if (*width > 0 && *height > 0) return REPUB_OK;

    /* Some JPEGs carry the frame header far into the file: retry with the
     * whole (bounded) resource. */
    if (static_cast<size_t>(size) > prefix && size <= REPUB_MAX_IMAGE_BYTES) {
        std::vector<uint8_t> full;
        const repub_err_t read_err = book_read_resource(book, href, &full, REPUB_MAX_IMAGE_BYTES);
        if (read_err == REPUB_OK) {
            probe_err = image_probe(full.data(), full.size(), width, height);
            if (probe_err == REPUB_OK) return REPUB_OK;
        }
    }
    return probe_err == REPUB_OK ? REPUB_ERR_UNSUPPORTED : probe_err;
#else
    (void)book;
    (void)href;
    return REPUB_ERR_UNSUPPORTED;
#endif
}

/* ------------------------------------------------------------------------- */
/* Chapters and progress                                                     */
/* ------------------------------------------------------------------------- */

repub_err_t book_chapter_open(repub_book* book, size_t spine_index,
                              const ParseOptions& options, repub_chapter** out) {
    if (!book || !out) return REPUB_ERR_INVALID_ARG;
    *out = nullptr;
    if (!book->opened) return REPUB_ERR_INVALID_ARG;
    if (spine_index >= book->spine.size()) return REPUB_ERR_INVALID_ARG;
    const SpineItem& item = book->spine[spine_index];
    if (item.href.empty()) return REPUB_ERR_NOT_FOUND;

    std::string html;
    const repub_err_t err =
        book_read_resource_string(*book, item.href, &html, REPUB_MAX_CHAPTER_BYTES);
    if (err != REPUB_OK) return err;

    repub_chapter* chapter = new (std::nothrow) repub_chapter();
    if (!chapter) return REPUB_ERR_MEMORY;
    const repub_err_t parse_err =
        parse_chapter(*book, spine_index, html, dirname_of(item.href), options, chapter);
    if (parse_err != REPUB_OK) {
        delete chapter;
        return parse_err;
    }
    chapter_finalize(chapter);
    chapter->book = book;
    chapter->spine_index = static_cast<uint32_t>(spine_index);
    if (chapter->title.empty()) {
        std::string toc_title;
        for (const TocEntry& entry : book->toc) {
            if (entry.spine_index == static_cast<int32_t>(spine_index) && !entry.title.empty()) {
                toc_title = entry.title;
                break;
            }
        }
        if (toc_title.empty()) toc_title = heading_title_from_html(html);
        chapter->title = std::move(toc_title);
    }
    *out = chapter;
    return REPUB_OK;
}

std::string book_toc_title_for_spine(const repub_book& book, size_t spine_index) {
    for (const TocEntry& entry : book.toc) {
        if (entry.spine_index == static_cast<int32_t>(spine_index) && !entry.title.empty()) {
            return entry.title;
        }
    }
    if (spine_index >= book.spine.size()) return std::string();
    const std::string& href = book.spine[spine_index].href;
    if (href.empty()) return std::string();
    std::string html;
    if (book_read_resource_string(book, href, &html, REPUB_MAX_CHAPTER_BYTES) != REPUB_OK) {
        return std::string();
    }
    return heading_title_from_html(html);
}

uint32_t book_spine_byte_offset(const repub_book& book, size_t spine_index) {
    size_t offset = 0;
    for (size_t i = 0; i < spine_index && i < book.spine.size(); ++i) offset += book.spine[i].size;
    if (offset > 0xFFFFFFFFull) return 0xFFFFFFFFu;
    return static_cast<uint32_t>(offset);
}

repub_err_t book_resolve_href(const repub_book& book, std::string_view href,
                              int32_t* spine_index, std::string* anchor) {
    if (!spine_index) return REPUB_ERR_INVALID_ARG;
    *spine_index = -1;
    if (anchor) anchor->clear();
    std::string file;
    std::string fragment;
    split_fragment(href, &file, &fragment);
    if (anchor) *anchor = percent_decode(fragment);
    if (file.empty()) return REPUB_ERR_NOT_FOUND;
    if (is_external_href(file)) return REPUB_ERR_NOT_FOUND;

    std::string candidates[3];
    size_t count = 0;
    candidates[count++] = resolve_path(book.opf_dir, file);
    candidates[count++] = normalize_path(file);
    candidates[count++] = percent_decode(normalize_path(file));

    for (size_t i = 0; i < count; ++i) {
        if (candidates[i].empty()) continue;
        bool duplicate = false;
        for (size_t j = 0; j < i; ++j) {
            if (candidates[j] == candidates[i]) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        const int32_t index = book.spine_index_for_href(candidates[i]);
        if (index >= 0) {
            *spine_index = index;
            return REPUB_OK;
        }
    }
    return REPUB_ERR_NOT_FOUND;
}

}  // namespace repub

/* ------------------------------------------------------------------------- */
/* Public C API glue (book half)                                             */
/* ------------------------------------------------------------------------- */

namespace {

repub::repub_book* to_internal(repub_book_t* book) {
    return reinterpret_cast<repub::repub_book*>(book);
}

const repub::repub_book* to_internal(const repub_book_t* book) {
    return reinterpret_cast<const repub::repub_book*>(book);
}

}  // namespace

extern "C" {

repub_book_t* repub_book_open(const repub_platform_t* platform, const char* path,
                              const repub_open_options_t* options, repub_err_t* err) {
    repub::repub_book* book = new (std::nothrow) repub::repub_book();
    if (!book) {
        if (err) *err = REPUB_ERR_MEMORY;
        return nullptr;
    }
    const repub_err_t result = repub::book_open(book, platform, path, options);
    if (result != REPUB_OK) {
        delete book;
        if (err) *err = result;
        return nullptr;
    }
    if (err) *err = REPUB_OK;
    return reinterpret_cast<repub_book_t*>(book);
}

void repub_book_close(repub_book_t* book) {
    repub::repub_book* internal = to_internal(book);
    if (!internal) return;
    repub::book_close(internal);
    delete internal;
}

const repub_platform_t* repub_book_platform(const repub_book_t* book) {
    const repub::repub_book* internal = to_internal(book);
    return internal ? internal->platform : nullptr;
}

const char* repub_book_path(const repub_book_t* book) {
    const repub::repub_book* internal = to_internal(book);
    return internal ? internal->path.c_str() : nullptr;
}

const repub_metadata_t* repub_book_metadata(const repub_book_t* book) {
    repub::repub_book* internal = const_cast<repub::repub_book*>(to_internal(book));
    if (!internal) return nullptr;
    internal->metadata.title = internal->title.c_str();
    internal->metadata.author = internal->author.c_str();
    internal->metadata.language = internal->language.c_str();
    internal->metadata.publisher = internal->publisher.c_str();
    internal->metadata.identifier = internal->identifier.c_str();
    internal->metadata.date = internal->date.c_str();
    internal->metadata.description = internal->description.c_str();
    internal->metadata.series = internal->series.c_str();
    internal->metadata.series_index = internal->series_index;
    return &internal->metadata;
}

size_t repub_book_spine_count(const repub_book_t* book) {
    const repub::repub_book* internal = to_internal(book);
    return internal ? internal->spine.size() : 0;
}

const repub_spine_item_t* repub_book_spine_item(const repub_book_t* book, size_t index) {
    const repub::repub_book* internal = to_internal(book);
    if (!internal || index >= internal->public_spine.size()) return nullptr;
    return &internal->public_spine[index];
}

size_t repub_book_toc_count(const repub_book_t* book) {
    const repub::repub_book* internal = to_internal(book);
    return internal ? internal->public_toc.size() : 0;
}

const repub_toc_item_t* repub_book_toc_item(const repub_book_t* book, size_t index) {
    const repub::repub_book* internal = to_internal(book);
    if (!internal || index >= internal->public_toc.size()) return nullptr;
    return &internal->public_toc[index];
}

repub_err_t repub_book_resolve_href(const repub_book_t* book, const char* href,
                                    int32_t* spine_index, char* anchor_buf,
                                    size_t anchor_cap) {
    const repub::repub_book* internal = to_internal(book);
    if (!internal || !href || !spine_index) return REPUB_ERR_INVALID_ARG;
    std::string anchor;
    const repub_err_t err = repub::book_resolve_href(*internal, href, spine_index, &anchor);
    if (anchor_buf && anchor_cap) {
        const size_t copy = anchor.size() < anchor_cap - 1 ? anchor.size() : anchor_cap - 1;
        std::memcpy(anchor_buf, anchor.data(), copy);
        anchor_buf[copy] = '\0';
    }
    return err;
}

const char* repub_book_cover_href(const repub_book_t* book) {
    const repub::repub_book* internal = to_internal(book);
    if (!internal || internal->cover_href.empty()) return nullptr;
    return internal->cover_href.c_str();
}

repub_err_t repub_book_cover_image(const repub_book_t* book, struct repub_image** out) {
    const repub::repub_book* internal = to_internal(book);
    if (!internal || !out) return REPUB_ERR_INVALID_ARG;
    if (internal->cover_href.empty()) return REPUB_ERR_NOT_FOUND;
    return repub_book_image_decode(book, internal->cover_href.c_str(), 0, 0, out);
}

repub_err_t repub_book_read_resource(const repub_book_t* book, const char* href, void** data,
                                     size_t* size) {
    const repub::repub_book* internal = to_internal(book);
    if (!internal || !href || !data || !size) return REPUB_ERR_INVALID_ARG;
    *data = nullptr;
    *size = 0;
    std::vector<uint8_t> bytes;
    const repub_err_t err = repub::book_read_resource(*internal, href, &bytes, 0);
    if (err != REPUB_OK) return err;
    if (bytes.empty()) return REPUB_OK;
    void* buffer = repub::palloc(internal->platform, bytes.size());
    if (!buffer) return REPUB_ERR_MEMORY;
    std::memcpy(buffer, bytes.data(), bytes.size());
    *data = buffer;
    *size = bytes.size();
    return REPUB_OK;
}

repub_err_t repub_book_resource_size(const repub_book_t* book, const char* href, size_t* size) {
    const repub::repub_book* internal = to_internal(book);
    if (!internal || !href || !size) return REPUB_ERR_INVALID_ARG;
    *size = 0;
    int index = -1;
    uint64_t entry_size = 0;
    const repub_err_t err = repub::find_resource_entry(*internal, href, &index, &entry_size);
    if (err != REPUB_OK) return err;
    if (entry_size > std::numeric_limits<size_t>::max()) return REPUB_ERR_LIMIT;
    *size = static_cast<size_t>(entry_size);
    return REPUB_OK;
}

repub_err_t repub_book_image_dims(const repub_book_t* book, const char* href, int32_t* width,
                                  int32_t* height) {
    const repub::repub_book* internal = to_internal(book);
    if (!internal || !href || !width || !height) return REPUB_ERR_INVALID_ARG;
    return repub::book_image_dims(*internal, href, width, height);
}

repub_err_t repub_book_image_decode(const repub_book_t* book, const char* href,
                                    int32_t max_width, int32_t max_height,
                                    struct repub_image** out) {
    const repub::repub_book* internal = to_internal(book);
    if (!internal || !href || !out) return REPUB_ERR_INVALID_ARG;
    *out = nullptr;
#if REPUB_ENABLE_IMAGES
    std::vector<uint8_t> bytes;
    const repub_err_t err =
        repub::book_read_resource(*internal, href, &bytes, REPUB_MAX_IMAGE_BYTES);
    if (err != REPUB_OK) return err;
    if (bytes.empty()) return REPUB_ERR_FORMAT;
    return repub::image_decode(internal->platform, bytes.data(), bytes.size(), max_width,
                               max_height, reinterpret_cast<repub::repub_image**>(out));
#else
    return REPUB_ERR_UNSUPPORTED;
#endif
}

repub_err_t repub_book_add_font(repub_book_t* book, const repub_font_desc_t* desc) {
    repub::repub_book* internal = to_internal(book);
    if (!internal || !desc || !desc->family || !desc->data || desc->size == 0) {
        return REPUB_ERR_INVALID_ARG;
    }
    if (!internal->fonts) return REPUB_ERR_INVALID_ARG;
    return internal->fonts->add_face(desc->family, desc->data, desc->size, desc->face_index,
                                     static_cast<int>(desc->style));
}

repub_err_t repub_book_set_body_font(repub_book_t* book, const char* family,
                                     repub_font_style_t style) {
    repub::repub_book* internal = to_internal(book);
    if (!internal) return REPUB_ERR_INVALID_ARG;
    internal->body_font = family ? family : "";
    (void)style;
    return REPUB_OK;
}

void repub_book_set_embedded_fonts(repub_book_t* book, int enable) {
    repub::repub_book* internal = to_internal(book);
    if (!internal) return;
    const bool was_enabled = internal->embedded_fonts_enabled;
    internal->embedded_fonts_enabled = enable != 0;
    if (!was_enabled && internal->embedded_fonts_enabled && internal->opened) {
        repub::book_load_embedded_fonts(internal);
    }
}

}  // extern "C"
