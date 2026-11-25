#include "pikasnip/platform/SettingsStore.h"

#include "pikasnip/platform/AppEnvironment.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace pikasnip::platform {

namespace {

constexpr std::streamoff kMaxSettingsBytes = 1024 * 1024;
constexpr size_t kMaxJsonDepth = 32;

// ---------------------------------------------------------------------------
// Minimal JSON reader (flat object; nested values are skipped)
// ---------------------------------------------------------------------------
struct JsonField {
    enum class Type { String, Number, Boolean, Skipped };

    Type type = Type::Skipped;
    std::string text;
    double number = 0.0;
    bool boolean = false;
};

class JsonReader {
public:
    explicit JsonReader(std::string_view text) noexcept : text_(text) {}

    bool ParseObject(std::vector<std::pair<std::string, JsonField>>& fields) {
        SkipWhitespace();
        if (!Consume('{')) {
            return false;
        }
        SkipWhitespace();
        if (Consume('}')) {
            return FinishDocument();
        }

        for (;;) {
            SkipWhitespace();
            std::string key;
            if (!ParseString(key)) {
                return false;
            }
            SkipWhitespace();
            if (!Consume(':')) {
                return false;
            }
            SkipWhitespace();

            JsonField field;
            if (!ParseValue(field)) {
                return false;
            }
            fields.emplace_back(std::move(key), std::move(field));

            SkipWhitespace();
            if (Consume(',')) {
                continue;
            }
            if (Consume('}')) {
                return FinishDocument();
            }
            return false;
        }
    }

private:
    bool FinishDocument() noexcept {
        SkipWhitespace();
        return pos_ == text_.size();
    }

    void SkipWhitespace() noexcept {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool Consume(char expected) noexcept {
        if (pos_ < text_.size() && text_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    static int HexValue(char c) noexcept {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    }

    bool ReadHex4(uint32_t& value) noexcept {
        if (pos_ + 4 > text_.size()) {
            return false;
        }
        uint32_t result = 0;
        for (size_t i = 0; i < 4; ++i) {
            const int digit = HexValue(text_[pos_ + i]);
            if (digit < 0) {
                return false;
            }
            result = (result << 4) | static_cast<uint32_t>(digit);
        }
        pos_ += 4;
        value = result;
        return true;
    }

    static void AppendUtf8(std::string& out, uint32_t codePoint) {
        if (codePoint <= 0x7F) {
            out.push_back(static_cast<char>(codePoint));
        } else if (codePoint <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else if (codePoint <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
    }

    bool ParseString(std::string& out) {
        if (!Consume('"')) {
            return false;
        }
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') {
                return true;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos_ >= text_.size()) {
                return false;
            }

            const char escaped = text_[pos_++];
            switch (escaped) {
            case '"':
                out.push_back('"');
                break;
            case '\\':
                out.push_back('\\');
                break;
            case '/':
                out.push_back('/');
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u': {
                uint32_t codePoint = 0;
                if (!ReadHex4(codePoint)) {
                    return false;
                }
                if (codePoint >= 0xD800 && codePoint <= 0xDBFF && pos_ + 1 < text_.size() &&
                    text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                    const size_t saved = pos_;
                    pos_ += 2;
                    uint32_t low = 0;
                    if (ReadHex4(low) && low >= 0xDC00 && low <= 0xDFFF) {
                        codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                    } else {
                        pos_ = saved;
                        codePoint = 0xFFFD;
                    }
                } else if (codePoint >= 0xD800 && codePoint <= 0xDFFF) {
                    codePoint = 0xFFFD;
                }
                AppendUtf8(out, codePoint);
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    bool ParseNumber(double& out) {
        const size_t start = pos_;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' ||
                c == 'E') {
                ++pos_;
            } else {
                break;
            }
        }
        if (start == pos_) {
            return false;
        }
        const char* first = text_.data() + start;
        const char* last = text_.data() + pos_;
        const auto result = std::from_chars(first, last, out);
        return result.ec == std::errc{} && result.ptr == last;
    }

    bool ConsumeLiteral(std::string_view literal) noexcept {
        if (text_.size() - pos_ < literal.size()) {
            return false;
        }
        if (text_.compare(pos_, literal.size(), literal) != 0) {
            return false;
        }
        pos_ += literal.size();
        return true;
    }

    bool SkipComposite() {
        std::array<char, kMaxJsonDepth> closers{};
        size_t depth = 0;

        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == '"') {
                std::string ignored;
                if (!ParseString(ignored)) {
                    return false;
                }
                continue;
            }
            ++pos_;
            if (c == '[' || c == '{') {
                if (depth >= closers.size()) {
                    return false;
                }
                closers[depth++] = (c == '[') ? ']' : '}';
            } else if (c == ']' || c == '}') {
                if (depth == 0 || closers[depth - 1] != c) {
                    return false;
                }
                --depth;
                if (depth == 0) {
                    return true;
                }
            }
        }
        return false;
    }

    bool ParseValue(JsonField& field) {
        if (pos_ >= text_.size()) {
            return false;
        }

        const char c = text_[pos_];
        if (c == '"') {
            field.type = JsonField::Type::String;
            return ParseString(field.text);
        }
        if (c == '{' || c == '[') {
            field.type = JsonField::Type::Skipped;
            return SkipComposite();
        }
        if (c == 't') {
            field.type = JsonField::Type::Boolean;
            field.boolean = true;
            return ConsumeLiteral("true");
        }
        if (c == 'f') {
            field.type = JsonField::Type::Boolean;
            field.boolean = false;
            return ConsumeLiteral("false");
        }
        if (c == 'n') {
            field.type = JsonField::Type::Skipped;
            return ConsumeLiteral("null");
        }

        field.type = JsonField::Type::Number;
        return ParseNumber(field.number);
    }

    std::string_view text_;
    size_t pos_ = 0;
};

// ---------------------------------------------------------------------------
// Value helpers
// ---------------------------------------------------------------------------
bool IsValidAccentHex(std::string_view value) noexcept {
    if (value.size() != 7 || value[0] != '#') {
        return false;
    }
    for (size_t i = 1; i < value.size(); ++i) {
        const char c = value[i];
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                         (c >= 'A' && c <= 'F');
        if (!hex) {
            return false;
        }
    }
    return true;
}

void SanitizeSettings(AppSettings& settings) noexcept {
    const AppSettings defaults;
    if (settings.defaultHotkey.empty()) {
        settings.defaultHotkey = defaults.defaultHotkey;
    }
    if (settings.altHotkey.empty()) {
        settings.altHotkey = defaults.altHotkey;
    }
    if (settings.ocrHotkey.empty()) {
        settings.ocrHotkey = defaults.ocrHotkey;
    }
    if (settings.pinHotkey.empty()) {
        settings.pinHotkey = defaults.pinHotkey;
    }

    settings.delaySeconds = std::clamp(settings.delaySeconds, 0, 60);
    if (!std::isfinite(settings.strokeThickness)) {
        settings.strokeThickness = defaults.strokeThickness;
    }
    settings.strokeThickness = std::clamp(settings.strokeThickness, 1.0f, 32.0f);

    if (!IsValidAccentHex(settings.accentHex)) {
        settings.accentHex = defaults.accentHex;
    }
}

void ApplyField(const std::string& key, const JsonField& field, AppSettings& settings) {
    if (field.type == JsonField::Type::String) {
        if (key == "defaultHotkey") {
            settings.defaultHotkey = field.text;
        } else if (key == "altHotkey") {
            settings.altHotkey = field.text;
        } else if (key == "ocrHotkey") {
            settings.ocrHotkey = field.text;
        } else if (key == "pinHotkey") {
            settings.pinHotkey = field.text;
        } else if (key == "accentHex") {
            settings.accentHex = field.text;
        }
    } else if (field.type == JsonField::Type::Number) {
        if (key == "delaySeconds" && std::isfinite(field.number)) {
            settings.delaySeconds = static_cast<int>(std::lround(field.number));
        } else if (key == "strokeThickness" && std::isfinite(field.number)) {
            settings.strokeThickness = static_cast<float>(field.number);
        }
    } else if (field.type == JsonField::Type::Boolean) {
        if (key == "runAtStartup") {
            settings.runAtStartup = field.boolean;
        }
    }
}

void AppendJsonString(std::string& out, std::string_view text) {
    static constexpr char kHex[] = "0123456789ABCDEF";

    out.push_back('"');
    for (const char raw : text) {
        const uint32_t c = static_cast<uint8_t>(raw);
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                out += "\\u00";
                out.push_back(kHex[(c >> 4) & 0xF]);
                out.push_back(kHex[c & 0xF]);
            } else {
                out.push_back(raw);
            }
            break;
        }
    }
    out.push_back('"');
}

std::string FormatFloat(float value) {
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (result.ec != std::errc{}) {
        return "0";
    }
    return std::string(buffer.data(), result.ptr);
}

bool TryLoad(const std::filesystem::path& path, AppSettings& outSettings) noexcept {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0 || size > kMaxSettingsBytes) {
        return false;
    }
    file.seekg(0, std::ios::beg);

    std::string text(static_cast<size_t>(size), '\0');
    if (size > 0) {
        file.read(text.data(), static_cast<std::streamsize>(size));
        if (!file) {
            return false;
        }
    }

    // Tolerate a UTF-8 BOM.
    if (text.size() >= 3 && static_cast<uint8_t>(text[0]) == 0xEF &&
        static_cast<uint8_t>(text[1]) == 0xBB && static_cast<uint8_t>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }

    std::vector<std::pair<std::string, JsonField>> fields;
    JsonReader reader(text);
    if (!reader.ParseObject(fields)) {
        return false;
    }

    AppSettings settings;
    for (const auto& [key, field] : fields) {
        ApplyField(key, field, settings);
    }
    SanitizeSettings(settings);
    outSettings = std::move(settings);
    return true;
}

} // namespace

AppSettings LoadSettings() {
    return LoadSettings(GetSettingsFilePath());
}

AppSettings LoadSettings(const std::filesystem::path& path) {
    AppSettings settings;
    if (!TryLoad(path, settings)) {
        settings = AppSettings{};
    }
    SanitizeSettings(settings);
    return settings;
}

bool SaveSettings(const AppSettings& settings) {
    return SaveSettings(GetSettingsFilePath(), settings);
}

bool SaveSettings(const std::filesystem::path& path, const AppSettings& settings) {
    const std::filesystem::path directory = path.parent_path();
    if (!directory.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        if (ec) {
            return false;
        }
    }

    AppSettings sanitized = settings;
    SanitizeSettings(sanitized);

    std::string json;
    json.reserve(640);
    json += "{\n";
    json += "  \"schemaVersion\": 1,\n";

    json += "  \"defaultHotkey\": ";
    AppendJsonString(json, sanitized.defaultHotkey);
    json += ",\n";

    json += "  \"altHotkey\": ";
    AppendJsonString(json, sanitized.altHotkey);
    json += ",\n";

    json += "  \"ocrHotkey\": ";
    AppendJsonString(json, sanitized.ocrHotkey);
    json += ",\n";

    json += "  \"pinHotkey\": ";
    AppendJsonString(json, sanitized.pinHotkey);
    json += ",\n";

    json += "  \"delaySeconds\": " + std::to_string(sanitized.delaySeconds) + ",\n";
    json += "  \"runAtStartup\": ";
    json += sanitized.runAtStartup ? "true" : "false";
    json += ",\n";

    json += "  \"strokeThickness\": " + FormatFloat(sanitized.strokeThickness) + ",\n";

    json += "  \"accentHex\": ";
    AppendJsonString(json, sanitized.accentHex);
    json += "\n";
    json += "}\n";

    const std::filesystem::path tempPath(
        path.native() + L"." +
        std::to_wstring(static_cast<unsigned long>(::GetCurrentProcessId())) + L".tmp");

    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file.write(json.data(), static_cast<std::streamsize>(json.size()));
        file.flush();
        if (!file) {
            file.close();
            std::error_code ec;
            std::filesystem::remove(tempPath, ec);
            return false;
        }
    }

    if (::MoveFileExW(tempPath.c_str(), path.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
        std::error_code ec;
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    return true;
}

} // namespace pikasnip::platform
