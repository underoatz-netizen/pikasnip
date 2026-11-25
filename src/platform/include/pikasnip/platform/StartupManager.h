#pragma once

namespace pikasnip::platform {

/// Returns true when PikaSnip is registered to start with Windows.
///
/// Packaged (MSIX): reads the declared StartupTask state.
/// Portable:        reads HKCU\...\Run value "PikaSnip".
[[nodiscard]] bool IsStartupEnabled();

/// Enables/disables "start with Windows". Returns false when the change was
/// rejected (policy, missing StartupTask declaration, registry failure).
[[nodiscard]] bool SetStartupEnabled(bool enable);

} // namespace pikasnip::platform
