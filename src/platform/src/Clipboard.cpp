#include "pikasnip/platform/Clipboard.h"
#include "pikasnip/platform/AppEnvironment.h"

#include <objbase.h>
#include <wrl/client.h>

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace pikasnip::platform {

namespace {

class ScopedClipboard {
public:
    explicit ScopedClipboard(HWND owner) noexcept {
        for (int attempt = 0; attempt < 4; ++attempt) {
            if (::OpenClipboard(owner)) {
                opened_ = true;
                return;
            }
            if (::GetLastError() != ERROR_ACCESS_DENIED) {
                return;
            }
            ::Sleep(5);
        }
    }

    ~ScopedClipboard() {
        if (opened_) {
            ::CloseClipboard();
        }
    }

    ScopedClipboard(const ScopedClipboard&) = delete;
    ScopedClipboard& operator=(const ScopedClipboard&) = delete;

    [[nodiscard]] bool isOpen() const noexcept { return opened_; }

private:
    bool opened_ = false;
};

class ScopedHGlobal {
public:
    ScopedHGlobal() noexcept = default;
    explicit ScopedHGlobal(SIZE_T sizeBytes) noexcept {
        if (sizeBytes > 0) {
            handle_ = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeBytes);
        }
    }

    ~ScopedHGlobal() {
        if (handle_ != nullptr) {
            ::GlobalFree(handle_);
        }
    }

    ScopedHGlobal(const ScopedHGlobal&) = delete;
    ScopedHGlobal& operator=(const ScopedHGlobal&) = delete;

    ScopedHGlobal(ScopedHGlobal&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    ScopedHGlobal& operator=(ScopedHGlobal&& other) noexcept {
        if (this != &other) {
            if (handle_ != nullptr) {
                ::GlobalFree(handle_);
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] HGLOBAL get() const noexcept { return handle_; }

    [[nodiscard]] HGLOBAL release() noexcept {
        return std::exchange(handle_, nullptr);
    }

private:
    HGLOBAL handle_ = nullptr;
};

[[nodiscard]] ScopedHGlobal AllocateHGlobalWithBytes(const void* data, size_t sizeBytes) noexcept {
    if (data == nullptr || sizeBytes == 0) {
        return {};
    }
    ScopedHGlobal mem(sizeBytes);
    if (mem.get() == nullptr) {
        return {};
    }
    void* locked = ::GlobalLock(mem.get());
    if (locked == nullptr) {
        return {};
    }
    std::memcpy(locked, data, sizeBytes);
    ::GlobalUnlock(mem.get());
    return mem;
}

[[nodiscard]] bool ValidateBgraInputs(
    const uint8_t* bgra,
    int32_t width,
    int32_t height,
    int32_t strideBytes) noexcept {
    if (bgra == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    const uint64_t minRowBytes = static_cast<uint64_t>(width) * 4ULL;
    if (minRowBytes > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
        return false;
    }
    if (strideBytes < static_cast<int32_t>(minRowBytes)) {
        return false;
    }
    const uint64_t totalDstBytes = minRowBytes * static_cast<uint64_t>(height);
    const uint64_t totalSrcBytes = static_cast<uint64_t>(strideBytes) * static_cast<uint64_t>(height);
    return totalDstBytes <= static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) &&
           totalSrcBytes <= static_cast<uint64_t>(std::numeric_limits<uint32_t>::max());
}

constexpr int32_t kMaxClipboardImageDim = 16384;

class ScopedGlobalLock {
public:
    explicit ScopedGlobalLock(HGLOBAL handle) noexcept : handle_(handle) {
        if (handle_ != nullptr) {
            ptr_ = ::GlobalLock(handle_);
        }
    }

    ~ScopedGlobalLock() {
        if (ptr_ != nullptr && handle_ != nullptr) {
            ::GlobalUnlock(handle_);
        }
    }

    ScopedGlobalLock(const ScopedGlobalLock&) = delete;
    ScopedGlobalLock& operator=(const ScopedGlobalLock&) = delete;

    [[nodiscard]] const void* data() const noexcept { return ptr_; }
    [[nodiscard]] size_t size() const noexcept {
        return (ptr_ != nullptr && handle_ != nullptr) ? static_cast<size_t>(::GlobalSize(handle_)) : 0U;
    }

private:
    HGLOBAL handle_ = nullptr;
    const void* ptr_ = nullptr;
};

class ScopedScreenDc {
public:
    ScopedScreenDc() noexcept : hdc_(::GetDC(nullptr)) {}
    ~ScopedScreenDc() {
        if (hdc_ != nullptr) {
            ::ReleaseDC(nullptr, hdc_);
        }
    }

    ScopedScreenDc(const ScopedScreenDc&) = delete;
    ScopedScreenDc& operator=(const ScopedScreenDc&) = delete;

    [[nodiscard]] HDC get() const noexcept { return hdc_; }

private:
    HDC hdc_ = nullptr;
};

[[nodiscard]] uint8_t ExtractBitfieldChannel(uint32_t pixel, uint32_t mask) noexcept {
    if (mask == 0U) {
        return 0U;
    }
    const unsigned shift = static_cast<unsigned>(std::countr_zero(mask));
    const uint32_t maxVal = mask >> shift;
    if (maxVal == 0U) {
        return 0U;
    }
    const uint32_t shifted = (pixel & mask) >> shift;
    if (maxVal == 255U) {
        return static_cast<uint8_t>(shifted);
    }
    return static_cast<uint8_t>((static_cast<uint64_t>(shifted) * 255ULL + (maxVal / 2U)) / maxVal);
}

void NormalizeAllZeroAlphaToOpaque(std::vector<uint8_t>& bgraPixels) noexcept {
    const size_t count = bgraPixels.size();
    for (size_t i = 3; i < count; i += 4) {
        if (bgraPixels[i] != 0U) {
            return;
        }
    }
    for (size_t i = 3; i < count; i += 4) {
        bgraPixels[i] = 255U;
    }
}

} // namespace

bool ClipboardImage::isValid() const noexcept {
    if (width <= 0 || height <= 0 || width > kMaxClipboardImageDim || height > kMaxClipboardImageDim) {
        return false;
    }
    const uint64_t minRowBytes = static_cast<uint64_t>(width) * 4ULL;
    if (strideBytes <= 0 || static_cast<uint64_t>(strideBytes) < minRowBytes) {
        return false;
    }
    const uint64_t required = static_cast<uint64_t>(strideBytes) * static_cast<uint64_t>(height);
    return bgraPixels.size() >= required;
}

std::vector<uint8_t> BuildDibV5PayloadFromBgra(
    const uint8_t* bgra,
    int32_t width,
    int32_t height,
    int32_t strideBytes) {
    if (!ValidateBgraInputs(bgra, width, height, strideBytes)) {
        return {};
    }

    const size_t rowBytes = static_cast<size_t>(width) * 4U;
    const size_t imageBytes = rowBytes * static_cast<size_t>(height);
    const size_t totalBytes = sizeof(BITMAPV5HEADER) + imageBytes;

    std::vector<uint8_t> payload(totalBytes, 0);

    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(BITMAPV5HEADER);
    header.bV5Width = width;
    header.bV5Height = -height; // Top-down DIB
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5SizeImage = static_cast<DWORD>(imageBytes);
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;
    header.bV5CSType = LCS_sRGB;
    header.bV5Intent = LCS_GM_IMAGES;

    std::memcpy(payload.data(), &header, sizeof(BITMAPV5HEADER));

    uint8_t* dst = payload.data() + sizeof(BITMAPV5HEADER);
    for (int32_t y = 0; y < height; ++y) {
        const uint8_t* srcRow = bgra + static_cast<size_t>(y) * static_cast<size_t>(strideBytes);
        std::memcpy(dst + static_cast<size_t>(y) * rowBytes, srcRow, rowBytes);
    }

    return payload;
}

std::vector<uint8_t> EncodePngBytesFromBgra(
    IWICImagingFactory* wic,
    const uint8_t* bgra,
    int32_t width,
    int32_t height,
    int32_t strideBytes) {
    if (wic == nullptr || !ValidateBgraInputs(bgra, width, height, strideBytes)) {
        return {};
    }

    Microsoft::WRL::ComPtr<IStream> stream;
    HRESULT hr = ::CreateStreamOnHGlobal(nullptr, TRUE, stream.ReleaseAndGetAddressOf());
    if (FAILED(hr) || !stream) {
        return {};
    }

    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.ReleaseAndGetAddressOf());
    if (FAILED(hr) || !encoder) {
        return {};
    }

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        return {};
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    hr = encoder->CreateNewFrame(frame.ReleaseAndGetAddressOf(), nullptr);
    if (FAILED(hr) || !frame) {
        return {};
    }

    hr = frame->Initialize(nullptr);
    if (FAILED(hr)) {
        return {};
    }

    hr = frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height));
    if (FAILED(hr)) {
        return {};
    }

    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&pixelFormat);
    if (FAILED(hr) || !::IsEqualGUID(pixelFormat, GUID_WICPixelFormat32bppBGRA)) {
        return {};
    }

    const UINT bufferSize = static_cast<UINT>(strideBytes) * static_cast<UINT>(height);
    hr = frame->WritePixels(
        static_cast<UINT>(height),
        static_cast<UINT>(strideBytes),
        bufferSize,
        const_cast<BYTE*>(bgra));
    if (FAILED(hr)) {
        return {};
    }

    hr = frame->Commit();
    if (FAILED(hr)) {
        return {};
    }

    hr = encoder->Commit();
    if (FAILED(hr)) {
        return {};
    }

    STATSTG stat{};
    hr = stream->Stat(&stat, STATFLAG_NONAME);
    if (FAILED(hr) || stat.cbSize.QuadPart == 0 ||
        stat.cbSize.QuadPart > static_cast<ULONGLONG>(std::numeric_limits<size_t>::max())) {
        return {};
    }

    HGLOBAL hMem = nullptr;
    hr = ::GetHGlobalFromStream(stream.Get(), &hMem);
    if (FAILED(hr) || hMem == nullptr) {
        return {};
    }

    const size_t pngSize = static_cast<size_t>(stat.cbSize.QuadPart);
    const void* locked = ::GlobalLock(hMem);
    if (locked == nullptr) {
        return {};
    }

    std::vector<uint8_t> pngBytes(pngSize);
    std::memcpy(pngBytes.data(), locked, pngSize);
    ::GlobalUnlock(hMem);
    return pngBytes;
}

bool CopyImageToClipboard(
    HWND owner,
    IWICImagingFactory* wic,
    const uint8_t* bgra,
    int32_t width,
    int32_t height,
    int32_t strideBytes) {
    const std::vector<uint8_t> dibBytes = BuildDibV5PayloadFromBgra(bgra, width, height, strideBytes);
    if (dibBytes.empty()) {
        return false;
    }

    const std::vector<uint8_t> pngBytes = EncodePngBytesFromBgra(wic, bgra, width, height, strideBytes);
    const UINT pngFormat = (!pngBytes.empty()) ? ::RegisterClipboardFormatW(L"PNG") : 0U;

    ScopedHGlobal dibMem = AllocateHGlobalWithBytes(dibBytes.data(), dibBytes.size());
    if (dibMem.get() == nullptr) {
        return false;
    }

    ScopedHGlobal pngMem;
    if (pngFormat != 0U) {
        pngMem = AllocateHGlobalWithBytes(pngBytes.data(), pngBytes.size());
    }

    ScopedClipboard clipboard(owner);
    if (!clipboard.isOpen()) {
        return false;
    }

    if (!::EmptyClipboard()) {
        return false;
    }

    bool publishedAny = false;

    if (pngFormat != 0U && pngMem.get() != nullptr) {
        if (::SetClipboardData(pngFormat, pngMem.get()) != nullptr) {
            (void)pngMem.release();
            publishedAny = true;
        }
    }

    if (::SetClipboardData(CF_DIBV5, dibMem.get()) != nullptr) {
        (void)dibMem.release();
        publishedAny = true;
    }

    return publishedAny;
}

bool CopyUnicodeTextToClipboard(
    HWND owner,
    std::wstring_view text) {
    const size_t byteCount = (text.size() + 1U) * sizeof(wchar_t);
    ScopedHGlobal mem(byteCount);
    if (mem.get() == nullptr) {
        return false;
    }

    void* locked = ::GlobalLock(mem.get());
    if (locked == nullptr) {
        return false;
    }
    if (!text.empty()) {
        std::memcpy(locked, text.data(), text.size() * sizeof(wchar_t));
    }
    static_cast<wchar_t*>(locked)[text.size()] = L'\0';
    ::GlobalUnlock(mem.get());

    ScopedClipboard clipboard(owner);
    if (!clipboard.isOpen()) {
        return false;
    }
    if (!::EmptyClipboard()) {
        return false;
    }
    if (::SetClipboardData(CF_UNICODETEXT, mem.get()) == nullptr) {
        return false;
    }
    (void)mem.release();
    return true;
}

bool SavePngToFile(
    IWICImagingFactory* wic,
    const std::filesystem::path& filePath,
    const uint8_t* bgra,
    int32_t width,
    int32_t height,
    int32_t strideBytes) {
    if (filePath.empty()) {
        return false;
    }

    const std::vector<uint8_t> pngBytes =
        EncodePngBytesFromBgra(wic, bgra, width, height, strideBytes);
    if (pngBytes.empty()) {
        return false;
    }

    std::error_code ec;
    const std::filesystem::path parent = filePath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return false;
        }
    }

    const std::filesystem::path tmpPath(filePath.wstring() + L".tmp");
    HANDLE hFile = ::CreateFileW(
        tmpPath.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    size_t offset = 0;
    bool writeOk = true;
    while (offset < pngBytes.size()) {
        const size_t remaining = pngBytes.size() - offset;
        const DWORD chunk = static_cast<DWORD>(
            remaining > static_cast<size_t>(std::numeric_limits<DWORD>::max())
                ? std::numeric_limits<DWORD>::max()
                : remaining);
        DWORD written = 0;
        if (!::WriteFile(hFile, pngBytes.data() + offset, chunk, &written, nullptr) ||
            written != chunk) {
            writeOk = false;
            break;
        }
        offset += written;
    }

    const BOOL flushOk = writeOk ? ::FlushFileBuffers(hFile) : FALSE;
    ::CloseHandle(hFile);

    if (!writeOk || !flushOk) {
        ::DeleteFileW(tmpPath.c_str());
        return false;
    }

    if (!::MoveFileExW(
            tmpPath.c_str(),
            filePath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ::DeleteFileW(tmpPath.c_str());
        return false;
    }

    return true;
}

std::filesystem::path SaveTimestampedPngToDefaultDir(
    IWICImagingFactory* wic,
    const uint8_t* bgra,
    int32_t width,
    int32_t height,
    int32_t strideBytes) {
    const std::filesystem::path dir = GetDefaultSaveDirectory();
    if (dir.empty()) {
        return {};
    }

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return {};
    }

    SYSTEMTIME st{};
    ::GetLocalTime(&st);

    wchar_t baseName[64]{};
    std::swprintf(
        baseName,
        std::size(baseName),
        L"PikaSnip_%04u%02u%02u_%02u%02u%02u",
        static_cast<unsigned int>(st.wYear),
        static_cast<unsigned int>(st.wMonth),
        static_cast<unsigned int>(st.wDay),
        static_cast<unsigned int>(st.wHour),
        static_cast<unsigned int>(st.wMinute),
        static_cast<unsigned int>(st.wSecond));

    std::filesystem::path candidate = dir / (std::wstring(baseName) + L".png");
    if (std::filesystem::exists(candidate, ec)) {
        bool foundUnique = false;
        for (int suffix = 1; suffix <= 999; ++suffix) {
            std::filesystem::path numbered =
                dir / (std::wstring(baseName) + L"_" + std::to_wstring(suffix) + L".png");
            if (!std::filesystem::exists(numbered, ec)) {
                candidate = std::move(numbered);
                foundUnique = true;
                break;
            }
        }
        if (!foundUnique) {
            return {};
        }
    }

    if (!SavePngToFile(wic, candidate, bgra, width, height, strideBytes)) {
        return {};
    }
    return candidate;
}

bool DecodePngBytesToBgra(
    IWICImagingFactory* wic,
    const uint8_t* pngData,
    size_t pngSize,
    ClipboardImage& outImage) {
    outImage = {};
    if (wic == nullptr || pngData == nullptr || pngSize == 0U ||
        pngSize > static_cast<size_t>(MAXDWORD)) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICStream> stream;
    HRESULT hr = wic->CreateStream(stream.ReleaseAndGetAddressOf());
    if (FAILED(hr) || !stream) {
        return false;
    }

    hr = stream->InitializeFromMemory(
        const_cast<BYTE*>(pngData),
        static_cast<DWORD>(pngSize));
    if (FAILED(hr)) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    hr = wic->CreateDecoderFromStream(
        stream.Get(),
        nullptr,
        WICDecodeMetadataCacheOnDemand,
        decoder.ReleaseAndGetAddressOf());
    if (FAILED(hr) || !decoder) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.ReleaseAndGetAddressOf());
    if (FAILED(hr) || !frame) {
        return false;
    }

    UINT w = 0;
    UINT h = 0;
    hr = frame->GetSize(&w, &h);
    if (FAILED(hr) || w == 0U || h == 0U ||
        w > static_cast<UINT>(kMaxClipboardImageDim) ||
        h > static_cast<UINT>(kMaxClipboardImageDim)) {
        return false;
    }

    const uint64_t stride64 = static_cast<uint64_t>(w) * 4ULL;
    const uint64_t totalBytes64 = stride64 * static_cast<uint64_t>(h);
    if (stride64 > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()) ||
        totalBytes64 > static_cast<uint64_t>(std::numeric_limits<UINT>::max()) ||
        totalBytes64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    hr = wic->CreateFormatConverter(converter.ReleaseAndGetAddressOf());
    if (FAILED(hr) || !converter) {
        return false;
    }

    hr = converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        return false;
    }

    ClipboardImage decoded;
    decoded.width = static_cast<int32_t>(w);
    decoded.height = static_cast<int32_t>(h);
    decoded.strideBytes = static_cast<int32_t>(stride64);
    decoded.bgraPixels.resize(static_cast<size_t>(totalBytes64));

    hr = converter->CopyPixels(
        nullptr,
        static_cast<UINT>(stride64),
        static_cast<UINT>(totalBytes64),
        decoded.bgraPixels.data());
    if (FAILED(hr)) {
        return false;
    }

    outImage = std::move(decoded);
    return true;
}

bool DecodeDibPayloadToBgra(
    const uint8_t* dibData,
    size_t dibSize,
    ClipboardImage& outImage) {
    outImage = {};
    if (dibData == nullptr || dibSize < sizeof(BITMAPINFOHEADER)) {
        return false;
    }

    BITMAPINFOHEADER bih{};
    std::memcpy(&bih, dibData, sizeof(BITMAPINFOHEADER));

    if (bih.biSize < sizeof(BITMAPINFOHEADER) || bih.biSize > dibSize) {
        return false;
    }
    if (bih.biPlanes != 1 || bih.biWidth < 1 || bih.biWidth > kMaxClipboardImageDim) {
        return false;
    }
    if (bih.biHeight == 0 || bih.biHeight == std::numeric_limits<int32_t>::min()) {
        return false;
    }

    const int32_t width = bih.biWidth;
    const bool bottomUp = (bih.biHeight > 0);
    const int32_t absHeight = bottomUp ? bih.biHeight : -bih.biHeight;
    if (absHeight < 1 || absHeight > kMaxClipboardImageDim) {
        return false;
    }

    const bool is32bpp =
        (bih.biBitCount == 32) &&
        (bih.biCompression == BI_RGB || bih.biCompression == BI_BITFIELDS);
    const bool is24bpp =
        (bih.biBitCount == 24) &&
        (bih.biCompression == BI_RGB);
    if (!is32bpp && !is24bpp) {
        return false;
    }

    uint64_t pixelOffset = static_cast<uint64_t>(bih.biSize);
    uint32_t redMask = 0x00FF0000U;
    uint32_t greenMask = 0x0000FF00U;
    uint32_t blueMask = 0x000000FFU;
    uint32_t alphaMask = 0x00000000U;

    if (bih.biCompression == BI_BITFIELDS) {
        if (bih.biSize == sizeof(BITMAPINFOHEADER)) {
            if (pixelOffset > dibSize || dibSize - pixelOffset < 12ULL) {
                return false;
            }
            DWORD masks[3]{};
            std::memcpy(masks, dibData + pixelOffset, sizeof(masks));
            redMask = masks[0];
            greenMask = masks[1];
            blueMask = masks[2];
            alphaMask = 0U;
            pixelOffset += 12ULL;
        } else if (bih.biSize >= sizeof(BITMAPV4HEADER)) {
            BITMAPV4HEADER v4{};
            std::memcpy(&v4, dibData, sizeof(BITMAPV4HEADER));
            redMask = v4.bV4RedMask;
            greenMask = v4.bV4GreenMask;
            blueMask = v4.bV4BlueMask;
            alphaMask = v4.bV4AlphaMask;
        } else {
            return false;
        }
    }

    const uint64_t clrUsedBytes = static_cast<uint64_t>(bih.biClrUsed) * sizeof(RGBQUAD);
    if (clrUsedBytes > dibSize || pixelOffset > dibSize - clrUsedBytes) {
        return false;
    }
    pixelOffset += clrUsedBytes;

    const uint64_t srcStride =
        ((static_cast<uint64_t>(width) * static_cast<uint64_t>(bih.biBitCount) + 31ULL) / 32ULL) * 4ULL;
    const uint64_t requiredSrcBytes = srcStride * static_cast<uint64_t>(absHeight);
    if (requiredSrcBytes > dibSize || pixelOffset > dibSize - requiredSrcBytes) {
        return false;
    }

    const uint64_t dstStride64 = static_cast<uint64_t>(width) * 4ULL;
    const uint64_t dstTotalBytes64 = dstStride64 * static_cast<uint64_t>(absHeight);
    if (dstStride64 > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()) ||
        dstTotalBytes64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return false;
    }

    ClipboardImage decoded;
    decoded.width = width;
    decoded.height = absHeight;
    decoded.strideBytes = static_cast<int32_t>(dstStride64);
    decoded.bgraPixels.resize(static_cast<size_t>(dstTotalBytes64));

    const uint8_t* srcBase = dibData + static_cast<size_t>(pixelOffset);
    bool anyNonZeroAlpha = false;
    const bool standardBgraMasks =
        (redMask == 0x00FF0000U && greenMask == 0x0000FF00U && blueMask == 0x000000FFU);

    for (int32_t y = 0; y < absHeight; ++y) {
        const int32_t srcY = bottomUp ? (absHeight - 1 - y) : y;
        const uint8_t* srcRow = srcBase + static_cast<size_t>(srcY) * static_cast<size_t>(srcStride);
        uint8_t* dstRow = decoded.bgraPixels.data() + static_cast<size_t>(y) * static_cast<size_t>(dstStride64);

        if (is24bpp) {
            for (int32_t x = 0; x < width; ++x) {
                const size_t s = static_cast<size_t>(x) * 3U;
                const size_t d = static_cast<size_t>(x) * 4U;
                dstRow[d + 0] = srcRow[s + 0];
                dstRow[d + 1] = srcRow[s + 1];
                dstRow[d + 2] = srcRow[s + 2];
                dstRow[d + 3] = 255U;
            }
        } else if (bih.biCompression == BI_RGB || alphaMask == 0U) {
            if (standardBgraMasks) {
                for (int32_t x = 0; x < width; ++x) {
                    const size_t off = static_cast<size_t>(x) * 4U;
                    dstRow[off + 0] = srcRow[off + 0];
                    dstRow[off + 1] = srcRow[off + 1];
                    dstRow[off + 2] = srcRow[off + 2];
                    dstRow[off + 3] = 255U;
                }
            } else {
                for (int32_t x = 0; x < width; ++x) {
                    uint32_t px = 0;
                    std::memcpy(&px, srcRow + static_cast<size_t>(x) * 4U, sizeof(uint32_t));
                    const size_t d = static_cast<size_t>(x) * 4U;
                    dstRow[d + 0] = ExtractBitfieldChannel(px, blueMask);
                    dstRow[d + 1] = ExtractBitfieldChannel(px, greenMask);
                    dstRow[d + 2] = ExtractBitfieldChannel(px, redMask);
                    dstRow[d + 3] = 255U;
                }
            }
        } else if (standardBgraMasks && alphaMask == 0xFF000000U) {
            std::memcpy(dstRow, srcRow, static_cast<size_t>(dstStride64));
            if (!anyNonZeroAlpha) {
                for (int32_t x = 0; x < width; ++x) {
                    if (dstRow[static_cast<size_t>(x) * 4U + 3U] != 0U) {
                        anyNonZeroAlpha = true;
                        break;
                    }
                }
            }
        } else {
            for (int32_t x = 0; x < width; ++x) {
                uint32_t px = 0;
                std::memcpy(&px, srcRow + static_cast<size_t>(x) * 4U, sizeof(uint32_t));
                const uint8_t a = ExtractBitfieldChannel(px, alphaMask);
                if (a != 0U) {
                    anyNonZeroAlpha = true;
                }
                const size_t d = static_cast<size_t>(x) * 4U;
                dstRow[d + 0] = ExtractBitfieldChannel(px, blueMask);
                dstRow[d + 1] = ExtractBitfieldChannel(px, greenMask);
                dstRow[d + 2] = ExtractBitfieldChannel(px, redMask);
                dstRow[d + 3] = a;
            }
        }
    }

    if (is32bpp && bih.biCompression == BI_BITFIELDS && alphaMask != 0U && !anyNonZeroAlpha) {
        NormalizeAllZeroAlphaToOpaque(decoded.bgraPixels);
    }

    outImage = std::move(decoded);
    return true;
}

bool ReadImageFromClipboard(
    HWND owner,
    IWICImagingFactory* wic,
    ClipboardImage& outImage) {
    outImage = {};

    ScopedClipboard clipboard(owner);
    if (!clipboard.isOpen()) {
        return false;
    }

    if (wic != nullptr) {
        const UINT pngFormat = ::RegisterClipboardFormatW(L"PNG");
        if (pngFormat != 0U && ::IsClipboardFormatAvailable(pngFormat)) {
            const HANDLE hData = ::GetClipboardData(pngFormat);
            if (hData != nullptr) {
                const ScopedGlobalLock lock(static_cast<HGLOBAL>(hData));
                if (lock.data() != nullptr &&
                    DecodePngBytesToBgra(
                        wic,
                        static_cast<const uint8_t*>(lock.data()),
                        lock.size(),
                        outImage)) {
                    return true;
                }
            }
        }
    }

    for (const UINT dibFmt : {static_cast<UINT>(CF_DIBV5), static_cast<UINT>(CF_DIB)}) {
        if (!::IsClipboardFormatAvailable(dibFmt)) {
            continue;
        }
        const HANDLE hData = ::GetClipboardData(dibFmt);
        if (hData == nullptr) {
            continue;
        }
        const ScopedGlobalLock lock(static_cast<HGLOBAL>(hData));
        if (lock.data() != nullptr &&
            DecodeDibPayloadToBgra(
                static_cast<const uint8_t*>(lock.data()),
                lock.size(),
                outImage)) {
            return true;
        }
    }

    if (::IsClipboardFormatAvailable(CF_BITMAP)) {
        const auto hBitmap = static_cast<HBITMAP>(::GetClipboardData(CF_BITMAP));
        if (hBitmap != nullptr) {
            BITMAP bm{};
            if (::GetObjectW(hBitmap, sizeof(bm), &bm) == sizeof(bm) &&
                bm.bmWidth >= 1 && bm.bmWidth <= kMaxClipboardImageDim &&
                bm.bmHeight >= 1 && bm.bmHeight <= kMaxClipboardImageDim) {
                const ScopedScreenDc dc;
                if (dc.get() != nullptr) {
                    BITMAPINFO bmi{};
                    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bmi.bmiHeader.biWidth = bm.bmWidth;
                    bmi.bmiHeader.biHeight = -bm.bmHeight; // Top-down 32bpp
                    bmi.bmiHeader.biPlanes = 1;
                    bmi.bmiHeader.biBitCount = 32;
                    bmi.bmiHeader.biCompression = BI_RGB;

                    const uint64_t stride64 = static_cast<uint64_t>(bm.bmWidth) * 4ULL;
                    const uint64_t totalBytes64 = stride64 * static_cast<uint64_t>(bm.bmHeight);
                    if (totalBytes64 <= static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
                        ClipboardImage decoded;
                        decoded.width = bm.bmWidth;
                        decoded.height = bm.bmHeight;
                        decoded.strideBytes = static_cast<int32_t>(stride64);
                        decoded.bgraPixels.resize(static_cast<size_t>(totalBytes64));

                        const int lines = ::GetDIBits(
                            dc.get(),
                            hBitmap,
                            0U,
                            static_cast<UINT>(bm.bmHeight),
                            decoded.bgraPixels.data(),
                            &bmi,
                            DIB_RGB_COLORS);
                        if (lines == bm.bmHeight) {
                            NormalizeAllZeroAlphaToOpaque(decoded.bgraPixels);
                            outImage = std::move(decoded);
                            return true;
                        }
                    }
                }
            }
        }
    }

    return false;
}

} // namespace pikasnip::platform
