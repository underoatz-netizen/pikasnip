#include "pikasnip/ocr/ThaiNorm.h"

#include <windows.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace pikasnip::ocr {

namespace {

[[nodiscard]] constexpr bool IsThaiChar(wchar_t ch) noexcept {
    return ch >= 0x0E00 && ch <= 0x0E7F;
}

[[nodiscard]] constexpr bool IsThaiDigit(wchar_t ch) noexcept {
    return ch >= 0x0E50 && ch <= 0x0E59;
}

[[nodiscard]] constexpr bool IsAsciiLetter(wchar_t ch) noexcept {
    return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z');
}

[[nodiscard]] constexpr bool IsVowelMark(wchar_t ch) noexcept {
    return ch == 0x0E31 || (ch >= 0x0E34 && ch <= 0x0E3A) || ch == 0x0E47;
}

[[nodiscard]] constexpr bool IsToneMark(wchar_t ch) noexcept {
    return (ch >= 0x0E48 && ch <= 0x0E4C) || ch == 0x0E4E;
}

[[nodiscard]] constexpr bool IsNikhahit(wchar_t ch) noexcept {
    return ch == 0x0E4D;
}

[[nodiscard]] constexpr bool IsReorderableMark(wchar_t ch) noexcept {
    return IsVowelMark(ch) || IsToneMark(ch) || IsNikhahit(ch);
}

[[nodiscard]] constexpr int MarkRank(wchar_t ch) noexcept {
    if (IsVowelMark(ch)) {
        return 0;
    }
    if (IsToneMark(ch)) {
        return 1;
    }
    return 2;
}

[[nodiscard]] constexpr bool IsDroppedFormatChar(wchar_t ch) noexcept {
    return ch == 0x200B || ch == 0x200C || ch == 0x200D ||
           ch == 0x2060 || ch == 0x00AD || ch == 0xFEFF;
}

[[nodiscard]] constexpr wchar_t MapLegacyThaiPua(wchar_t ch) noexcept {
    switch (ch) {
        case 0xF700: return 0x0E10;
        case 0xF701: return 0x0E34;
        case 0xF702: return 0x0E35;
        case 0xF703: return 0x0E36;
        case 0xF704: return 0x0E37;
        case 0xF705: return 0x0E48;
        case 0xF706: return 0x0E49;
        case 0xF707: return 0x0E4A;
        case 0xF708: return 0x0E4B;
        case 0xF709: return 0x0E4C;
        case 0xF70A: return 0x0E48;
        case 0xF70B: return 0x0E49;
        case 0xF70C: return 0x0E4A;
        case 0xF70D: return 0x0E4B;
        case 0xF70E: return 0x0E4C;
        case 0xF70F: return 0x0E0D;
        case 0xF710: return 0x0E31;
        case 0xF711: return 0x0E4D;
        case 0xF712: return 0x0E47;
        case 0xF713: return 0x0E48;
        case 0xF714: return 0x0E49;
        case 0xF715: return 0x0E4A;
        case 0xF716: return 0x0E4B;
        case 0xF717: return 0x0E4C;
        case 0xF718: return 0x0E38;
        case 0xF719: return 0x0E39;
        case 0xF71A: return 0x0E3A;
        default: return ch;
    }
}

[[nodiscard]] std::wstring NormalizeToNfc(std::wstring_view input) {
    if (input.empty()) {
        return {};
    }
    const int needed = ::NormalizeString(
        NormalizationC,
        input.data(),
        static_cast<int>(input.size()),
        nullptr,
        0);
    if (needed <= 0) {
        return std::wstring(input);
    }
    std::wstring out(static_cast<size_t>(needed), L'\0');
    const int written = ::NormalizeString(
        NormalizationC,
        input.data(),
        static_cast<int>(input.size()),
        out.data(),
        needed);
    if (written <= 0) {
        return std::wstring(input);
    }
    out.resize(static_cast<size_t>(written));
    return out;
}

[[nodiscard]] constexpr bool IsInlineSpace(wchar_t ch) noexcept {
    return ch == L' ' || ch == L'\t' || ch == L'\f' || ch == L'\v';
}

[[nodiscard]] std::wstring CollapseSpacesAndTrimLine(std::wstring_view line) {
    std::wstring collapsed;
    collapsed.reserve(line.size());
    bool inSpace = false;
    for (const wchar_t ch : line) {
        if (IsInlineSpace(ch)) {
            if (!collapsed.empty() && !inSpace) {
                collapsed.push_back(L' ');
            }
            inSpace = true;
        } else {
            collapsed.push_back(ch);
            inSpace = false;
        }
    }
    while (!collapsed.empty() && collapsed.back() == L' ') {
        collapsed.pop_back();
    }
    return collapsed;
}

} // namespace

std::wstring CanonicalThai(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }

    // 1. PUA mapping, zero-width stripping, NBSP -> space.
    std::wstring translated;
    translated.reserve(text.size());
    for (const wchar_t ch : text) {
        if (IsDroppedFormatChar(ch)) {
            continue;
        }
        if (ch == 0x00A0) {
            translated.push_back(L' ');
            continue;
        }
        translated.push_back(MapLegacyThaiPua(ch));
    }

    // 2. Unicode NFC (never NFKC, which decomposes sara am 0x0E33).
    std::wstring nfc = NormalizeToNfc(translated);

    // 3. Reorder consecutive Thai combining marks (vowel -> tone -> nikhahit)
    //    and collapse consecutive identical marks.
    std::wstring ordered;
    ordered.reserve(nfc.size());
    for (size_t i = 0; i < nfc.size();) {
        if (!IsReorderableMark(nfc[i])) {
            ordered.push_back(nfc[i]);
            ++i;
            continue;
        }
        size_t j = i + 1;
        while (j < nfc.size() && IsReorderableMark(nfc[j])) {
            ++j;
        }
        if (j - i == 1) {
            ordered.push_back(nfc[i]);
        } else {
            std::wstring run = nfc.substr(i, j - i);
            std::stable_sort(run.begin(), run.end(), [](wchar_t a, wchar_t b) {
                return MarkRank(a) < MarkRank(b);
            });
            wchar_t prev = 0;
            for (const wchar_t m : run) {
                if (m != prev) {
                    ordered.push_back(m);
                    prev = m;
                }
            }
        }
        i = j;
    }

    // 4. Compose ([่-๋]?)ํ([่-๋]?)า -> $1$2ำ
    std::wstring composed;
    composed.reserve(ordered.size());
    for (size_t i = 0; i < ordered.size();) {
        if (ordered[i] == 0x0E4D) {
            // Check optional preceding tone mark (0x0E48..0x0E4B) already pushed.
            // Check optional following tone mark (0x0E48..0x0E4B) then sara aa (0x0E32).
            size_t look = i + 1;
            wchar_t postTone = 0;
            if (look < ordered.size() && ordered[look] >= 0x0E48 && ordered[look] <= 0x0E4B) {
                postTone = ordered[look];
                ++look;
            }
            if (look < ordered.size() && ordered[look] == 0x0E32) {
                if (postTone != 0) {
                    composed.push_back(postTone);
                }
                composed.push_back(0x0E33);
                i = look + 1;
                continue;
            }
        }
        composed.push_back(ordered[i]);
        ++i;
    }

    // 5. Collapse เเ (0x0E40 0x0E40) -> แ (0x0E41) and ำำ (0x0E33 0x0E33) -> ำ (0x0E33).
    std::wstring collapsedChars;
    collapsedChars.reserve(composed.size());
    for (size_t i = 0; i < composed.size(); ++i) {
        if (i + 1 < composed.size()) {
            if (composed[i] == 0x0E40 && composed[i + 1] == 0x0E40) {
                collapsedChars.push_back(0x0E41);
                ++i;
                continue;
            }
            if (composed[i] == 0x0E33 && composed[i + 1] == 0x0E33) {
                collapsedChars.push_back(0x0E33);
                ++i;
                continue;
            }
        }
        collapsedChars.push_back(composed[i]);
    }

    // 6. Unify line endings (\r\n / \r -> \n), collapse inline spaces, trim lines & outer newlines.
    std::wstring unified;
    unified.reserve(collapsedChars.size());
    for (size_t i = 0; i < collapsedChars.size(); ++i) {
        const wchar_t ch = collapsedChars[i];
        if (ch == L'\r') {
            unified.push_back(L'\n');
            if (i + 1 < collapsedChars.size() && collapsedChars[i + 1] == L'\n') {
                ++i;
            }
        } else {
            unified.push_back(ch);
        }
    }

    std::wstring result;
    result.reserve(unified.size());
    size_t lineStart = 0;
    while (lineStart <= unified.size()) {
        const size_t nl = unified.find(L'\n', lineStart);
        const size_t lineEnd = (nl == std::wstring::npos) ? unified.size() : nl;
        const std::wstring cleanLine = CollapseSpacesAndTrimLine(
            std::wstring_view(unified).substr(lineStart, lineEnd - lineStart));
        if (!result.empty()) {
            result.push_back(L'\n');
        }
        result.append(cleanLine);
        if (nl == std::wstring::npos) {
            break;
        }
        lineStart = nl + 1;
    }

    while (!result.empty() && result.front() == L'\n') {
        result.erase(result.begin());
    }
    while (!result.empty() && result.back() == L'\n') {
        result.pop_back();
    }
    return result;
}

std::wstring FixScripts(std::wstring_view text) {
    std::wstring out(text);
    const size_t n = out.size();
    for (size_t i = 0; i < n; ++i) {
        const wchar_t ch = out[i];
        if (ch == L'W' || ch == L'w') {
            const bool prevLetter = (i > 0) && IsAsciiLetter(out[i - 1]);
            const bool nextLetter = (i + 1 < n) && IsAsciiLetter(out[i + 1]);
            if (!prevLetter && !nextLetter) {
                const bool prevThai = (i > 0) && IsThaiChar(out[i - 1]);
                const bool nextThai = (i + 1 < n) && IsThaiChar(out[i + 1]);
                const bool dotThai = (i + 2 < n) && out[i + 1] == L'.' && IsThaiChar(out[i + 2]);
                if (prevThai || nextThai || dotThai) {
                    out[i] = 0x0E1E; // พ
                }
            }
        } else if (ch == L'O' || ch == L'o' || ch == L'0') {
            const bool prevThaiDigit = (i > 0) && IsThaiDigit(out[i - 1]);
            const bool nextThaiDigit = (i + 1 < n) && IsThaiDigit(out[i + 1]);
            if (prevThaiDigit || nextThaiDigit) {
                out[i] = 0x0E50; // ๐
            }
        }
    }
    // Backward pass so leading runs like "00๑" normalize to "๐๐๑".
    for (size_t i = n; i > 0; --i) {
        const size_t idx = i - 1;
        const wchar_t ch = out[idx];
        if (ch == L'O' || ch == L'o' || ch == L'0') {
            if (idx + 1 < n && IsThaiDigit(out[idx + 1])) {
                out[idx] = 0x0E50; // ๐
            }
        }
    }
    return out;
}

} // namespace pikasnip::ocr
