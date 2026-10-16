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
// Minimal native chrome: quieter neutrals, restrained violet accent, high
// contrast labels. Keep web content, system theme and CEF profiles untouched.
// 0xAARRGGBB, deliberately opaque to avoid compositing over BrowserViews.
inline constexpr ThemePalette kLightPalette{
  0xFFF7F7FAu, // header
  0xFFEDECF2u, // inactive tab
  0xFFE5E3ECu, // hovered tab
  0xFFDCD7EFu, // selected tab
  0xFFF7F7FAu, // quiet toolbar surface
  0xFFEAE8F1u, // toolbar hover
  0xFFE8E4F6u, // control tint
  0xFF6247B9u, // accent
  0xFF51359Fu, // accent hover
  0xFF25232Bu, // text
  0xFF57515Fu, // secondary text
  0xFF2F2455u, // selected text
  0xFFF7F7FAu, // danger idle = quiet
  0xFFF8DFE5u, // danger hover
  0xFF981E3Du, // danger text
  0xFFFFFFFFu  // page background
};
inline constexpr ThemePalette kDarkPalette{
  0xFF1D1C22u, // header
  0xFF29272Fu, // inactive tab
  0xFF35313Fu, // hovered tab
  0xFF49405Fu, // selected tab
  0xFF1D1C22u, // quiet toolbar surface
  0xFF33303Cu, // toolbar hover
  0xFF383045u, // control tint
  0xFFA58CFFu, // accent
  0xFFC0AFFFu, // accent hover
  0xFFF4F1F9u, // text
  0xFFB9B2C3u, // secondary text
  0xFFFAF6FFu, // selected text
  0xFF1D1C22u, // danger idle = quiet
  0xFF593243u, // danger hover
  0xFFFFD2DAu, // danger text
  0xFF141317u  // page background
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
  0xFFF0EFF4u, 0xFFF0EFF4u, 0xFFE3E0ECu,
  0xFFDCD7EFu, 0xFF25232Bu, 0xFF57515Fu
};
inline constexpr SidebarPalette kDarkSidebar{
  0xFF17161Bu, 0xFF23212Au, 0xFF353140u,
  0xFF49405Fu, 0xFFF4F1F9u, 0xFFB9B2C3u
};
inline constexpr const SidebarPalette& Sidebar(ThemeMode mode) {
  return mode == ThemeMode::Dark ? kDarkSidebar : kLightSidebar;
}
inline constexpr const ThemePalette& Palette(ThemeMode mode) {
  return mode == ThemeMode::Dark ? kDarkPalette : kLightPalette;
}
}  // namespace rc_ui
