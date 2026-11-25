#pragma once

namespace pikasnip::platform {

/// STA is the default for UI threads; MTA suits background capture/OCR work.
enum class ComApartment { SingleThreaded, MultiThreaded };

/// RAII COM + C++/WinRT apartment for the calling thread.
///
/// Uses winrt::init_apartment() when available and falls back to
/// CoInitializeEx(). The destructor balances only the initialisation this
/// object performed, so it is safe on a thread that already had COM set up.
class ComInitializer {
public:
    explicit ComInitializer(ComApartment apartment = ComApartment::SingleThreaded) noexcept;
    ~ComInitializer();

    ComInitializer(const ComInitializer&) = delete;
    ComInitializer& operator=(const ComInitializer&) = delete;
    ComInitializer(ComInitializer&&) = delete;
    ComInitializer& operator=(ComInitializer&&) = delete;

    [[nodiscard]] bool initialized() const noexcept { return m_mode != Mode::Uninitialized; }

    /// True when COM is usable on this thread (whether it was already set up or not).
    [[nodiscard]] bool ok() const noexcept { return initialized(); }

    /// True when this object owns the apartment and will balance it.
    [[nodiscard]] bool owns_apartment() const noexcept {
        return m_mode == Mode::WinRT || m_mode == Mode::Com;
    }

private:
    enum class Mode { Uninitialized, WinRT, Com, AlreadyInitialized };

    Mode m_mode = Mode::Uninitialized;
};

} // namespace pikasnip::platform
