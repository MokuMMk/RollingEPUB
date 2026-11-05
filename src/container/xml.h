/*
 * RollingEPUB minimal XML pull parser.
 *
 * Tolerant of common EPUB XML (namespaces, CDATA, comments, DOCTYPE,
 * processing instructions) and entity references.  Built for OPF/NCX/NAV and
 * small metadata files, not a validating parser.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef REPUB_SRC_CONTAINER_XML_H
#define REPUB_SRC_CONTAINER_XML_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/base.h"

namespace repub {

struct XmlAttribute {
    std::string name;   /* lower-case, namespace prefix preserved ("opf:role") */
    std::string value;  /* entity decoded */
};

class XmlParser {
   public:
    enum class Token {
        None,
        StartElement,
        EndElement,
        Text,
        Cdata,
        Comment,
        Declaration, /* <?xml ...?> */
        Doctype,
        Eof
    };

    explicit XmlParser(std::string_view input);

    /* Advance to the next token. Returns false at EOF/parse error. */
    bool next();
    Token token() const { return token_; }
    bool failed() const { return failed_; }

    /* Current token accessors (valid for StartElement/EndElement). */
    const std::string& name() const { return name_; }
    void local_name(std::string* out) const;
    bool self_closing() const { return self_closing_; }
    size_t attribute_count() const { return attributes_.size(); }
    const XmlAttribute* attribute(size_t index) const;
    /* Lookup by exact name, then by local name (ignoring prefix). */
    const std::string* attribute_value(std::string_view name) const;
    std::string attr(std::string_view name, std::string_view fallback = {}) const;
    bool has_attr(std::string_view name) const { return attribute_value(name) != nullptr; }

    /* Current token text (Text/Cdata already entity decoded for Text). */
    const std::string& text() const { return text_; }
    int depth() const { return depth_; }

    /* Skip the current element's subtree (call after StartElement). */
    void skip_subtree();

    /* Position in the input (byte offset), for diagnostics/tests. */
    size_t position() const { return pos_; }
    const std::string& error() const { return error_; }

   private:
    bool parse_start_or_end();
    bool parse_text();
    bool parse_comment();
    bool parse_cdata();
    bool parse_declaration();
    bool parse_doctype();
    bool read_name(std::string* out);
    void skip_space();
    bool decode_into(std::string_view raw, std::string* out) const;
    bool starts_with(std::string_view text) const;

    std::string_view input_;
    size_t pos_ = 0;
    Token token_ = Token::None;
    std::string name_;
    std::string text_;
    std::vector<XmlAttribute> attributes_;
    bool self_closing_ = false;
    int depth_ = 0;
    bool failed_ = false;
    std::string error_;
    std::string decode_buffer_;
};

}  // namespace repub

#endif  // REPUB_SRC_CONTAINER_XML_H
