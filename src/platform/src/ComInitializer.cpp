#include "pikasnip/platform/ComInitializer.h"

#include <winrt/base.h>

#include <objbase.h>

namespace pikasnip::platform {

ComInitializer::ComInitializer(ComApartment apartment) noexcept {
    try {
        winrt::init_apartment(apartment == ComApartment::SingleThreaded
                                  ? winrt::apartment_type::single_threaded
                                  : winrt::apartment_type::multi_threaded);
        m_mode = Mode::WinRT;
        return;
    } catch (...) {
        // COM may already be initialised on this thread with a different model
        // (RPC_E_CHANGED_MODE). Fall back to the plain COM entry points.
    }

    const DWORD flags =
        apartment == ComApartment::SingleThreaded ? COINIT_APARTMENTTHREADED : COINIT_MULTITHREADED;
    const HRESULT hr = ::CoInitializeEx(nullptr, flags);
    if (SUCCEEDED(hr)) {
        // S_FALSE means "already initialised on this thread" and still requires
        // a matching CoUninitialize().
        m_mode = Mode::Com;
    } else if (hr == RPC_E_CHANGED_MODE) {
        m_mode = Mode::AlreadyInitialized;
    }
}

ComInitializer::~ComInitializer() {
    switch (m_mode) {
    case Mode::WinRT:
        try {
            winrt::uninit_apartment();
        } catch (...) {
            // Destructors must not throw.
        }
        break;
    case Mode::Com:
        ::CoUninitialize();
        break;
    case Mode::AlreadyInitialized:
    case Mode::Uninitialized:
        break;
    }
}

} // namespace pikasnip::platform
