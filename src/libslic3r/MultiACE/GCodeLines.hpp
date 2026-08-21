#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Slic3r { namespace MultiACE {

// Canonical line splitter for gcode text, used by every MultiACE gcode-scanning/
// rewriting function so line handling is consistent in exactly one place. Splits
// on \n, \r\n, or bare \r (matching Python's splitlines() semantics that the
// ported logic was originally written against) and drops the line terminator,
// mirroring how gcode is produced by Orca's own writer.
inline std::vector<std::string> split_lines(const std::string& text)
{
    std::vector<std::string> lines;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\n') {
            lines.emplace_back(text.substr(start, i - start));
            start = i + 1;
        } else if (c == '\r') {
            lines.emplace_back(text.substr(start, i - start));
            if (i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
            start = i + 1;
        }
    }
    if (start < text.size())
        lines.emplace_back(text.substr(start));
    return lines;
}

// Matches Python's literal str.split('\n') exactly - NOT the same as
// splitlines()/split_lines() above. split('\n') always yields exactly
// (count of '\n') + 1 elements, so "X\n" -> ["X", ""] (a trailing empty
// element), never special-cases \r, and round-trips perfectly through
// join_lines() with no information loss. Some of the ported functions'
// Python sources use .split('\n') instead of .splitlines() for their line
// list (apply_head_mode_rewrite, inject_auto_load) - using split_lines()
// there instead would silently disagree with Python on trailing-newline
// handling, so match whichever method the specific function being ported
// actually calls.
inline std::vector<std::string> split_on_newline(const std::string& text)
{
    std::vector<std::string> lines;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            lines.emplace_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    lines.emplace_back(text.substr(start));
    return lines;
}

// Character offset in `text` where the (0-indexed) `at`-th line begins,
// exactly matching split_lines(text)'s own line boundaries - correctly
// handles \r\n as a 2-byte terminator, unlike a naive "sum of line
// lengths + 1" calculation (which silently corrupts every downstream offset
// once the text contains even one \r\n). `at` may equal the number of line
// breaks (offset of a would-be trailing empty line) or exceed it (returns
// text.size()).
inline size_t nth_line_offset(const std::string& text, size_t at)
{
    if (at == 0)
        return 0;
    size_t line_no = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\n') {
            ++line_no;
            if (line_no == at) return i + 1;
        } else if (c == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
            ++line_no;
            if (line_no == at) return i + 1;
        }
    }
    return text.size();
}

inline std::string join_lines(const std::vector<std::string>& lines)
{
    std::string out;
    size_t total = 0;
    for (const auto& l : lines) total += l.size() + 1;
    out.reserve(total);
    for (size_t i = 0; i < lines.size(); ++i) {
        out += lines[i];
        if (i + 1 < lines.size())
            out += '\n';
    }
    return out;
}

// split_lines()/join_lines() are inherently lossy about the source text's
// trailing run of newlines (matching Python's splitlines(), which has the
// same loss - "a\nb\n".splitlines() == "a\nb".splitlines(), and both collapse
// "a\nb\n\n" down to a single trailing \n on rejoin). That's harmless when a
// transformed piece IS the final output and Python's own reconstruction is
// equally lossy there (see rewrite()'s body/pass 5), but it's a real
// divergence anywhere Python instead uses a whole-string re.sub - re.sub
// never touches untouched trailing bytes, so it preserves however many
// newlines were originally there exactly, not just "at least one". Call
// this after any split_lines()/join_lines() round trip that's standing in
// for what Python did as a whole-string re.sub, with the pre-transformation
// text and the post-transformation result; it copies the original's exact
// trailing-\n count onto the result, overwriting however many the round
// trip happened to leave behind.
inline std::string restore_trailing_newline(const std::string& original, std::string transformed)
{
    size_t n = 0;
    while (n < original.size() && original[original.size() - 1 - n] == '\n')
        ++n;
    size_t t = 0;
    while (t < transformed.size() && transformed[transformed.size() - 1 - t] == '\n')
        ++t;
    transformed.resize(transformed.size() - t);
    transformed.append(n, '\n');
    return transformed;
}

// Trim ASCII whitespace from both ends - equivalent to Python's str.strip() for
// the plain-ASCII gcode text this module deals with exclusively.
inline std::string_view strip(std::string_view s)
{
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

} } // namespace Slic3r::MultiACE
