/*
 * RollingEPUB minimal XML pull parser.
 *
 * SPDX-License-Identifier: MIT
 *
 * Provenance: tokeniser structure adapted from Read Pico main/book/book_epub.c
 * (Apache-2.0) and from the Expat-based state machines under CrossMux
 * lib/Epub/Epub/parsers (MIT), rewritten as a pull parser.
 *
 * Depth convention: after next() returns, depth() is the nesting level of the
 * element a StartElement/EndElement token refers to (root element = 1).  The
 * parser decrements its internal open-element count lazily at the start of the
 * following next() call, so an EndElement token reports the same level as its
 * matching StartElement token.  self_closing() is true for self-closing start
 * tags; skip_subtree() is a no-op for those.
 */
#include "container/xml.h"

#include <cstring>

#include "core/text_util.h"

namespace repub {

namespace {

bool xml_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

bool name_char(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u >= 0x80) return true; /* allow UTF-8 element/attribute names */
    if ((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9')) return true;
    return c == '_' || c == ':' || c == '-' || c == '.';
}

}  // namespace

XmlParser::XmlParser(std::string_view input) : input_(input) {
    /* Skip an optional UTF-8 BOM. */
    if (input_.size() >= 3 && static_cast<unsigned char>(input_[0]) == 0xEF &&
        static_cast<unsigned char>(input_[1]) == 0xBB &&
        static_cast<unsigned char>(input_[2]) == 0xBF) {
        pos_ = 3;
    }
}

bool XmlParser::starts_with(std::string_view text) const {
    return input_.size() - pos_ >= text.size() && input_.compare(pos_, text.size(), text) == 0;
}

bool XmlParser::read_name(std::string* out) {
    if (!out) return false;
    size_t start = pos_;
    if (pos_ < input_.size() && !name_char(input_[pos_])) return false;
    while (pos_ < input_.size() && name_char(input_[pos_])) ++pos_;
    if (pos_ == start) return false;
    out->assign(input_.data() + start, pos_ - start);
    for (char& c : *out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return true;
}

void XmlParser::skip_space() {
    while (pos_ < input_.size() && xml_space(input_[pos_])) ++pos_;
}

bool XmlParser::decode_into(std::string_view raw, std::string* out) const {
    if (!out) return false;
    *out = decode_entities(raw);
    return true;
}

const XmlAttribute* XmlParser::attribute(size_t index) const {
    if (index >= attributes_.size()) return nullptr;
    return &attributes_[index];
}

void XmlParser::local_name(std::string* out) const {
    if (!out) return;
    const size_t colon = name_.find(':');
    if (colon == std::string::npos) {
        *out = name_;
    } else {
        *out = name_.substr(colon + 1);
    }
}

const std::string* XmlParser::attribute_value(std::string_view name) const {
    for (const XmlAttribute& attribute : attributes_) {
        if (attribute.name == name) return &attribute.value;
    }
    for (const XmlAttribute& attribute : attributes_) {
        const size_t colon = attribute.name.find(':');
        const std::string_view local =
            colon == std::string::npos ? std::string_view(attribute.name)
                                       : std::string_view(attribute.name).substr(colon + 1);
        if (compare_ci(local, name) == 0) return &attribute.value;
    }
    return nullptr;
}

std::string XmlParser::attr(std::string_view name, std::string_view fallback) const {
    const std::string* value = attribute_value(name);
    return value ? *value : std::string(fallback);
}

bool XmlParser::next() {
    if (failed_) return false;

    /* Lazy decrement of the open-element count for the previous token:
     *  - EndElement closes the element it named;
     *  - a self-closing StartElement opened and closed at once.
     * This keeps depth() consistent for both tokens of an element pair. */
    if (token_ == Token::EndElement || (token_ == Token::StartElement && self_closing_)) {
        if (depth_ > 0) --depth_;
    }

    attributes_.clear();
    name_.clear();
    text_.clear();
    self_closing_ = false;
    token_ = Token::None;

    if (pos_ >= input_.size()) {
        token_ = Token::Eof;
        depth_ = 0;
        return false;
    }

    const char c = input_[pos_];
    if (c != '<') return parse_text();

    if (starts_with("<!--")) return parse_comment();
    if (starts_with("<![CDATA[")) return parse_cdata();
    if (starts_with("<?")) return parse_declaration();
    if (starts_with("<!DOCTYPE") || starts_with("<!doctype")) return parse_doctype();
    if (starts_with("<!")) return parse_doctype();
    return parse_start_or_end();
}

bool XmlParser::parse_text() {
    const size_t start = pos_;
    while (pos_ < input_.size() && input_[pos_] != '<') ++pos_;
    token_ = Token::Text;
    if (!decode_into(input_.substr(start, pos_ - start), &text_)) {
        failed_ = true;
        error_ = "invalid text";
        return false;
    }
    return true;
}

bool XmlParser::parse_comment() {
    const size_t content_start = pos_ + 4;
    const size_t end = input_.find("-->", content_start);
    if (end == std::string_view::npos) {
        failed_ = true;
        error_ = "unterminated comment";
        return false;
    }
    text_.assign(input_.data() + content_start, end - content_start);
    pos_ = end + 3;
    token_ = Token::Comment;
    return true;
}

bool XmlParser::parse_cdata() {
    const size_t content_start = pos_ + 9;
    const size_t end = input_.find("]]>", content_start);
    if (end == std::string_view::npos) {
        failed_ = true;
        error_ = "unterminated CDATA";
        return false;
    }
    text_.assign(input_.data() + content_start, end - content_start);
    pos_ = end + 3;
    token_ = Token::Cdata;
    return true;
}

bool XmlParser::parse_declaration() {
    const size_t content_start = pos_ + 2;
    const size_t end = input_.find("?>", content_start);
    if (end == std::string_view::npos) {
        failed_ = true;
        error_ = "unterminated processing instruction";
        return false;
    }
    text_.assign(input_.data() + content_start, end - content_start);
    pos_ = end + 2;
    token_ = Token::Declaration;
    return true;
}

bool XmlParser::parse_doctype() {
    /* Skip <!DOCTYPE ...> including an internal subset in square brackets.
     * Bracket and quote tracking keeps ">" inside the subset from ending it. */
    size_t start = pos_ + 2;
    if (starts_with("<!DOCTYPE") || starts_with("<!doctype")) start = pos_ + 9;
    pos_ = start;
    int brackets = 0;
    char quote = 0;
    for (;;) {
        if (pos_ >= input_.size()) {
            failed_ = true;
            error_ = "unterminated markup declaration";
            return false;
        }
        const char c = input_[pos_];
        if (quote) {
            if (c == quote) quote = 0;
            ++pos_;
            continue;
        }
        if (c == '\'' || c == '"') {
            quote = c;
            ++pos_;
            continue;
        }
        if (c == '[') {
            ++brackets;
            ++pos_;
            continue;
        }
        if (c == ']') {
            if (brackets > 0) --brackets;
            ++pos_;
            continue;
        }
        if (c == '>' && brackets == 0) {
            text_.assign(input_.data() + start, pos_ - start);
            ++pos_;
            token_ = Token::Doctype;
            return true;
        }
        ++pos_;
    }
}

bool XmlParser::parse_start_or_end() {
    ++pos_; /* consume '<' */
    bool closing = false;
    if (pos_ < input_.size() && input_[pos_] == '/') {
        closing = true;
        ++pos_;
    }

    std::string tag_name;
    if (!read_name(&tag_name)) {
        failed_ = true;
        error_ = "invalid element name";
        return false;
    }

    bool self_closing = false;
    bool closed = false;
    while (!closed) {
        skip_space();
        if (pos_ >= input_.size()) {
            failed_ = true;
            error_ = "unterminated element tag";
            return false;
        }
        const char c = input_[pos_];
        if (c == '>') {
            ++pos_;
            closed = true;
            break;
        }
        if (c == '/') {
            if (pos_ + 1 < input_.size() && input_[pos_ + 1] == '>') {
                self_closing = true;
                pos_ += 2;
                closed = true;
                break;
            }
            failed_ = true;
            error_ = "unexpected '/' in element tag";
            return false;
        }
        if (closing) {
            failed_ = true;
            error_ = "attributes on end tag";
            return false;
        }

        std::string attr_name;
        if (!read_name(&attr_name)) {
            failed_ = true;
            error_ = "invalid attribute name";
            return false;
        }
        skip_space();
        std::string value;
        if (pos_ < input_.size() && input_[pos_] == '=') {
            ++pos_;
            skip_space();
            if (pos_ >= input_.size()) {
                failed_ = true;
                error_ = "missing attribute value";
                return false;
            }
            const char quote = input_[pos_];
            if (quote == '"' || quote == '\'') {
                ++pos_;
                const size_t value_start = pos_;
                const size_t value_end = input_.find(quote, value_start);
                if (value_end == std::string_view::npos) {
                    failed_ = true;
                    error_ = "unterminated attribute value";
                    return false;
                }
                decode_into(input_.substr(value_start, value_end - value_start), &value);
                pos_ = value_end + 1;
            } else {
                const size_t value_start = pos_;
                while (pos_ < input_.size() && !xml_space(input_[pos_]) && input_[pos_] != '>') {
                    ++pos_;
                }
                decode_into(input_.substr(value_start, pos_ - value_start), &value);
            }
        }
        attributes_.push_back(XmlAttribute{std::move(attr_name), std::move(value)});
    }

    name_ = std::move(tag_name);
    if (closing) {
        if (depth_ == 0) {
            failed_ = true;
            error_ = "unexpected end tag";
            return false;
        }
        token_ = Token::EndElement;
        self_closing_ = true; /* pending-close marker; see next() */
        return true;
    }

    token_ = Token::StartElement;
    self_closing_ = self_closing;
    /* Both kinds of start tag report depth_ + 1; a self-closing tag is popped
     * lazily at the start of the next next() call. */
    ++depth_;
    return true;
}

void XmlParser::skip_subtree() {
    if (token_ != Token::StartElement || self_closing_) return;
    const int target = depth_;
    while (next()) {
        if (token_ == Token::EndElement && depth_ == target) return;
    }
}

}  // namespace repub
