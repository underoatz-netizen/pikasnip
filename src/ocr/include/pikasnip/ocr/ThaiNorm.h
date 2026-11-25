#pragma once

#include <string>
#include <string_view>

namespace pikasnip::ocr {

/// Normalizes Thai text to canonical NFC form:
/// - Maps legacy PUA glyph variants (U+F700..U+F71A) to standard Thai code points.
/// - Strips zero-width format characters and maps NBSP to ASCII space.
/// - Reorders and deduplicates consecutive Thai combining marks (vowel -> tone -> nikhahit).
/// - Composes nikhahit + sara aa (with optional tone) into tone + sara am.
/// - Collapses repeated sara e (เเ -> แ) and sara am (ำำ -> ำ), normalizes spaces/line breaks.
[[nodiscard]] std::wstring CanonicalThai(std::wstring_view text);

/// Fixes common OCR script look-alike errors in Thai context:
/// - Lone 'W'/'w' adjacent to Thai text -> 'พ'.
/// - 'O'/'o'/'0' adjacent to Thai digits (๐-๙) -> '๐'.
[[nodiscard]] std::wstring FixScripts(std::wstring_view text);

} // namespace pikasnip::ocr
