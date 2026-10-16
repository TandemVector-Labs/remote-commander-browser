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
inline constexpr ThemePalette kLightPalette{
  0xFFF4F7FBu, // header
  0xFFECF1F8u, // tab
  0xFFE1EAF7u, // tab hover
  0xFFD7E6FCu, // selected tab
  0xFFF4F7FBu, // button
  0xFFE1EAF7u, // button hover
  0xFFDDEAFBu, // gentle primary tint
  0xFF245AD5u,
  0xFF1D4ED8u,
  0xFF17243Bu, // text
  0xFF52637Cu, // secondary text
  0xFF1E3A8Au, // text on tinted selection
  0xFFF4F7FBu,
  0xFFFDE6E7u,
  0xFF9C1D30u,
  0xFFFFFFFFu
};
inline constexpr ThemePalette kDarkPalette{
  0xFF161E2Bu,
  0xFF202B3Cu,
  0xFF283B53u,
  0xFF304966u,
  0xFF192536u,
  0xFF283B53u,
  0xFF244163u,
  0xFF3B82F6u,
  0xFF60A5FAu,
  0xFFF0F5FDu,
  0xFFC3D1E3u,
  0xFFEAF3FFu,
  0xFF161E2Bu,
  0xFF6B2436u,
  0xFFFFE9EBu,
  0xFF101824u
};
struct SidebarPalette {
  std::uint32_t background;
  std::uint32_t tab;
  std::uint32_t hover;
  std::uint32_t active;
  std::uint32_t text;
  std::uint32_t muted;
};
// Light and dark use identical semantic roles, not disconnected hardcoded colors.
inline constexpr SidebarPalette kLightSidebar{
  0xFFF2F5FAu, 0xFFF7F9FDu, 0xFFE8F0FAu,
  0xFFD9E8FAu, 0xFF1A2A43u, 0xFF536880u
};
inline constexpr SidebarPalette kDarkSidebar{
  0xFF151C28u, 0xFF1C2738u, 0xFF2A394Fu,
  0xFF344B6Bu, 0xFFF2F6FFu, 0xFFBECDE0u
};
inline constexpr const SidebarPalette& Sidebar(ThemeMode mode) {
  return mode == ThemeMode::Dark ? kDarkSidebar : kLightSidebar;
}
inline constexpr const ThemePalette& Palette(ThemeMode mode) {
  return mode == ThemeMode::Dark ? kDarkPalette : kLightPalette;
}
}  // namespace rc_ui
