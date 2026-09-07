#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace core {

namespace detail {

struct CodepointRange {
    char32_t lo = 0;
    char32_t hi = 0;
};

} // namespace detail

// Controls that break the line box, and the invisible marks that let a string lie
// about what it says.
constexpr bool isPresentableCodepoint(char32_t codepoint) {
    static constexpr std::array blocked{
        detail::CodepointRange{ 0x0000, 0x001F },  // C0
        detail::CodepointRange{ 0x007F, 0x009F },  // DEL and C1
        detail::CodepointRange{ 0x2028, 0x2029 },  // line and paragraph separators
        detail::CodepointRange{ 0x200B, 0x200F },  // zero-width and directional marks
        detail::CodepointRange{ 0x202A, 0x202E },  // bidi embedding and override
        detail::CodepointRange{ 0x2066, 0x2069 },  // bidi isolates
        detail::CodepointRange{ 0xFEFF, 0xFEFF },  // zero-width no-break space
    };
    return !std::ranges::any_of(blocked, [codepoint](detail::CodepointRange range) {
        return codepoint >= range.lo && codepoint <= range.hi;
    });
}

// Server-supplied text reaches ui::text, so it is filtered here and not merely where
// it was written: the API host is assumed hostile, and its own validator is the first
// thing a breach bypasses. Rejects the whole string rather than repairing it, so the
// caller falls back to the compiled wording.
constexpr bool isPresentableText(std::string_view text) {
    for (size_t at = 0; at < text.size();) {
        const auto lead = static_cast<uint8_t>(text[at]);
        char32_t codepoint = 0;
        size_t length = 0;
        if (lead < 0x80) {
            codepoint = lead;
            length = 1;
        } else if (lead >= 0xC2 && lead <= 0xDF) {
            codepoint = lead & 0x1F;
            length = 2;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            codepoint = lead & 0x0F;
            length = 3;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            codepoint = lead & 0x07;
            length = 4;
        } else {
            return false;
        }
        if (at + length > text.size()) return false;

        for (size_t i = 1; i < length; ++i) {
            const auto continuation = static_cast<uint8_t>(text[at + i]);
            if ((continuation & 0xC0) != 0x80) return false;
            codepoint = (codepoint << 6) | (continuation & 0x3F);
        }

        // Overlongs, surrogates, and out-of-range values all decode without complaint.
        if (length == 2 && codepoint < 0x80) return false;
        if (length == 3 && (codepoint < 0x800 || (codepoint >= 0xD800 && codepoint <= 0xDFFF))) return false;
        if (length == 4 && (codepoint < 0x10000 || codepoint > 0x10FFFF)) return false;
        if (!isPresentableCodepoint(codepoint)) return false;

        at += length;
    }
    return true;
}

static_assert(isPresentableText("Back in ten minutes"));
static_assert(isPresentableText("Manutenção até as 18h"));
static_assert(isPresentableText(""));
static_assert(!isPresentableText("two\nlines"));
static_assert(!isPresentableText("bell\x07"));
static_assert(!isPresentableText("\xE2\x80\xAE" "desrever"));  // U+202E
static_assert(!isPresentableText("\xEF\xBB\xBF" "hidden"));    // U+FEFF
static_assert(!isPresentableText("\xC0\xAF"));              // overlong solidus
static_assert(!isPresentableText("\xED\xA0\x80"));          // surrogate half
static_assert(!isPresentableText("\xE2\x80"));              // truncated sequence

} // namespace core
