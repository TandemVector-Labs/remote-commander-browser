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
// Preview-only, per-tab identity. Every tab ID has a deterministic hue;
// moving/activating tabs does not change their identity. Maximum 8 visible.
struct TabHue { std::uint32_t idle, active, hover, badge; };
inline constexpr TabHue kLightTabHues[8]{
  {0xFFEDF3FFu,0xFFD4E2FFu,0xFFE2ECFFu,0xFF325DAEu}, // ocean
  {0xFFF2EEFFu,0xFFE2D9FFu,0xFFEAE3FFu,0xFF7455B8u}, // iris
  {0xFFE8F7F4u,0xFFD0EEE7u,0xFFDCF4EEu,0xFF207F74u}, // teal
  {0xFFF0F6E9u,0xFFE1EDCEu,0xFFE8F2DAu,0xFF5B7830u}, // sage
  {0xFFFFF5E7u,0xFFFFE6C2u,0xFFFFEED5u,0xFF926022u}, // amber
  {0xFFFFF0EDu,0xFFFFDCD4u,0xFFFFE7E0u,0xFF9F5142u}, // coral
  {0xFFE9F4FFu,0xFFD2E7F8u,0xFFDFEEFCu,0xFF286D98u}, // sky
  {0xFFF9EDFAu,0xFFF0D7F1u,0xFFF5E5F6u,0xFF945B95u}  // berry
};
inline constexpr TabHue kDarkTabHues[8]{
  {0xFF203454u,0xFF345789u,0xFF2B4773u,0xFF6B9FECu},
  {0xFF30284Eu,0xFF534477u,0xFF423761u,0xFFB69AEAu},
  {0xFF193D3Bu,0xFF2A615Au,0xFF24504Bu,0xFF6FD0BCu},
  {0xFF2D3A27u,0xFF4B6042u,0xFF3E5036u,0xFF9EC67Eu},
  {0xFF433527u,0xFF715436u,0xFF59442Du,0xFFE9AD64u},
  {0xFF482B32u,0xFF704750u,0xFF5C3740u,0xFFE89C9Du},
  {0xFF233A4Cu,0xFF3F6580u,0xFF315269u,0xFF79B9DFu},
  {0xFF422D42u,0xFF654766u,0xFF533954u,0xFFD8A0D7u}
};
inline constexpr const TabHue& TabIdentityHue(ThemeMode mode, int tab_id) {
  const int index = ((tab_id - 1) % 8 + 8) % 8;
  return mode == ThemeMode::Dark ? kDarkTabHues[index] : kLightTabHues[index];
}
}  // namespace rc_ui
