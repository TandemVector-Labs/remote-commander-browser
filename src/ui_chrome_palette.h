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
// A calm, layered native shell around the unmodified ChatGPT page.
// Opaque CEF Views surfaces are intentional: no CSS injection, HTML chrome,
// alpha compositing, backdrop blur or additional BrowserViews.  Light uses
// porcelain white / pale blue; Dark uses deep navy / restrained sky accents.
inline constexpr ThemePalette kLightPalette{
  0xFFF8FAFEu, // header: warm porcelain white
  0xFFF0F4FBu, // inactive tab
  0xFFE3EDFDu, // hovered tab
  0xFFDCEAFEu, // active tab
  0xFFF5F7FCu, // quiet native controls
  0xFFEAF1FCu, // hover surface
  0xFFE7F0FFu, // soft blue accent / quiet actions
  0xFF2359BEu, // primary (horizontal Commander action)
  0xFF174994u, // hover primary
  0xFF1B2940u, // strong text
  0xFF53647Du, // muted text
  0xFF1F4487u, // active text
  0xFFF8FAFEu, // close idle
  0xFFFBE3EAu, // destructive hover
  0xFF9F254Bu, // destructive text
  0xFFFFFFFFu  // light content / CTA text
};
inline constexpr ThemePalette kDarkPalette{
  0xFF101B31u, // header: deep midnight navy
  0xFF17263Eu, // inactive tab
  0xFF223957u, // hovered tab
  0xFF284F80u, // selected tab
  0xFF17263Eu, // quiet native controls
  0xFF2A4366u, // hover surface
  0xFF203859u, // soft accent
  0xFF7CB3FFu, // primary
  0xFFB3D5FFu, // hover primary
  0xFFEEF5FFu, // strong text
  0xFFC5D2E6u, // muted text
  0xFFF8FAFFu, // active text
  0xFF101B31u, // close idle
  0xFF50283Du, // destructive hover
  0xFFFFD6E1u, // destructive text
  0xFF101B31u  // dark content / CTA text (dark on bright primary)
};
struct SidebarPalette {
  std::uint32_t background;
  std::uint32_t tab;
  std::uint32_t hover;
  std::uint32_t active;
  std::uint32_t text;
  std::uint32_t muted;
};
// Distinct from the page-owned ChatGPT navigation: these are native tabs
// and local Commander actions, with readable inactive and active states.
inline constexpr SidebarPalette kLightSidebar{
  0xFFF7F9FDu, 0xFFECF2FBu, 0xFFDFEAFDu,
  0xFFD3E4FDu, 0xFF1B2A45u, 0xFF4D607Bu
};
inline constexpr SidebarPalette kDarkSidebar{
  0xFF0C1729u, 0xFF17263Du, 0xFF263D5Cu,
  0xFF2B517Cu, 0xFFEEF5FFu, 0xFFBDCEE3u
};
inline constexpr const SidebarPalette& Sidebar(ThemeMode mode) {
  return mode == ThemeMode::Dark ? kDarkSidebar : kLightSidebar;
}
inline constexpr const ThemePalette& Palette(ThemeMode mode) {
  return mode == ThemeMode::Dark ? kDarkPalette : kLightPalette;
}
}  // namespace rc_ui
