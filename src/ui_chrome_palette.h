#pragma once
// R98: CEF-free native theme palette. 0xAARRGGBB representation.
#include <cstdint>
namespace rc_ui {
enum class ThemeMode { Light, Dark };
struct ThemePalette {
  std::uint32_t header;
  std::uint32_t tab;
  std::uint32_t tabHover;
  std::uint32_t tabActive;
  std::uint32_t button;
  std::uint32_t buttonHover;
  std::uint32_t softAccent;
  std::uint32_t primary;
  std::uint32_t primaryHover;
  std::uint32_t text;
  std::uint32_t muted;
  std::uint32_t activeText;
  std::uint32_t danger;
  std::uint32_t dangerHover;
  std::uint32_t dangerText;
  std::uint32_t content;
};
// Minimal native chrome: sky-blue light mode and deep navy dark mode, high
// contrast labels. Keep web content, system theme and CEF profiles untouched.
// 0xAARRGGBB, deliberately opaque to avoid compositing over BrowserViews.
inline constexpr ThemePalette kLightPalette{
  0xFFEAF5FFu, // header
  0xFFDDEDFBu, // inactive tab
  0xFFC8E1F8u, // hovered tab
  0xFFACD4F7u, // selected tab
  0xFFEAF5FFu, // quiet toolbar surface
  0xFFCFE5F9u, // toolbar hover
  0xFFD5EAFEu, // control tint
  0xFF1865B5u, // accent
  0xFF14518Du, // accent hover
  0xFF152A42u, // text
  0xFF39566Fu, // secondary text
  0xFF123B66u, // selected text
  0xFFEAF5FFu, // danger idle = quiet
  0xFFF5D5DAu, // danger hover
  0xFF9E243Au, // danger text
  0xFFF5FAFFu  // page background
};
inline constexpr ThemePalette kDarkPalette{
  0xFF111E32u, // header
  0xFF1B2B45u, // inactive tab
  0xFF274063u, // hovered tab
  0xFF345779u, // selected tab
  0xFF111E32u, // quiet toolbar surface
  0xFF263B58u, // toolbar hover
  0xFF223956u, // control tint
  0xFF72B8FFu, // accent
  0xFF9ACBFFu, // accent hover
  0xFFE8F3FFu, // text
  0xFFAABFD5u, // secondary text
  0xFFF5FBFFu, // selected text
  0xFF111E32u, // danger idle = quiet
  0xFF512739u, // danger hover
  0xFFFFC7D7u, // danger text
  0xFF0C1526u  // page background
};
struct SidebarPalette {
  std::uint32_t background;
  std::uint32_t tab;
  std::uint32_t hover;
  std::uint32_t active;
  std::uint32_t text;
  std::uint32_t muted;
};
// Sidebar is one visual depth below the top chrome; active row has an
// unmistakable contrast step, even in compact (icon-only) mode.
inline constexpr SidebarPalette kLightSidebar{
  0xFFE2F0FEu, 0xFFE2F0FEu, 0xFFCDE3F8u,
  0xFFADD3F5u, 0xFF152A42u, 0xFF3A5975u
};
inline constexpr SidebarPalette kDarkSidebar{
  0xFF101B2Eu, 0xFF18263Du, 0xFF243C58u,
  0xFF355B7Eu, 0xFFE8F3FFu, 0xFFB5C5D8u
};
inline constexpr const SidebarPalette& Sidebar(ThemeMode mode) {
  return mode == ThemeMode::Dark ? kDarkSidebar : kLightSidebar;
}
inline constexpr const ThemePalette& Palette(ThemeMode mode) {
  return mode == ThemeMode::Dark ? kDarkPalette : kLightPalette;
}
}  // namespace rc_ui
