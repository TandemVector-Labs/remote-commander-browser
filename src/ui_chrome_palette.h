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
// Lightweight 'frosted glass' visual illusion: one opaque native paint surface
// per EXISTING view, with layered slate/blue surfaces and high-contrast labels.
// Never use actual alpha, backdrop blur, shadows, CSS filters or extra BrowserViews:
// those would add compositing work and can break CEF overlay/sandbox geometry.
// Web content, authenticated profile, navigation and page CSS remain untouched.
inline constexpr ThemePalette kLightPalette{
  0xFFF4F8FFu, // header: daylight frosted glass
  0xFFE5EFFBu, // inactive tab
  0xFFD8E7FAu, // hovered tab
  0xFFC7DEF9u, // selected tab
  0xFFF1F6FEu, // quiet native toolbar
  0xFFDDEAFBu, // toolbar hover
  0xFFD8E7F9u, // subtle blue surface
  0xFF2259ABu, // Commander CTA
  0xFF18498Fu, // CTA hover
  0xFF16283Fu, // text
  0xFF485B73u, // secondary text
  0xFF1E4576u, // selected text
  0xFFF4F8FFu, // danger idle = quiet
  0xFFFBE1EAu, // danger hover
  0xFF9E2044u, // danger text
  0xFFFFFFFFu  // light canvas / CTA text
};
inline constexpr ThemePalette kDarkPalette{
  0xFF0D1A2Du, // header: deep navy frosted glass
  0xFF192A43u, // inactive tab
  0xFF293F5Fu, // hovered tab
  0xFF345278u, // selected tab
  0xFF14243Au, // quiet native toolbar
  0xFF283F60u, // toolbar hover
  0xFF243C5Fu, // subtle blue surface
  0xFF78BCFFu, // Commander CTA
  0xFFA0D3FFu, // CTA hover
  0xFFECF4FFu, // text
  0xFFBFCEE1u, // secondary text
  0xFFF8FBFFu, // selected text
  0xFF0D1A2Du, // danger idle = quiet
  0xFF542D45u, // danger hover
  0xFFFFCAD9u, // danger text
  0xFF091525u  // dark canvas / CTA text
};
struct SidebarPalette {
  std::uint32_t background;
  std::uint32_t tab;
  std::uint32_t hover;
  std::uint32_t active;
  std::uint32_t text;
  std::uint32_t muted;
};
// A second, darker layer makes the sidebar read like translucent glass
// without allocating a single additional compositor layer.
inline constexpr SidebarPalette kLightSidebar{
  0xFFEAF2FCu, 0xFFE3EDFAu, 0xFFD1E3F8u,
  0xFFC1D9F8u, 0xFF16283Fu, 0xFF485B73u
};
inline constexpr SidebarPalette kDarkSidebar{
  0xFF0B1729u, 0xFF14263Eu, 0xFF274262u,
  0xFF34577Eu, 0xFFECF4FFu, 0xFFBFCEE1u
};
inline constexpr const SidebarPalette& Sidebar(ThemeMode mode) {
  return mode == ThemeMode::Dark ? kDarkSidebar : kLightSidebar;
}
inline constexpr const ThemePalette& Palette(ThemeMode mode) {
  return mode == ThemeMode::Dark ? kDarkPalette : kLightPalette;
}
}  // namespace rc_ui
