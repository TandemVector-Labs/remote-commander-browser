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
  0xFFF8FAFCu,
  0xFFF8FAFCu,
  0xFFEEF2F7u,
  0xFFFFFFFFu,
  0xFFF8FAFCu,
  0xFFEEF2F7u,
  0xFFEFF6FFu,
  0xFF2563EBu,
  0xFF1D4ED8u,
  0xFF0F172Au,
  0xFF64748Bu,
  0xFF1D4ED8u,
  0xFFF8FAFCu,
  0xFFFEE2E2u,
  0xFFDC2626u,
  0xFFFFFFFFu
};
inline constexpr ThemePalette kDarkPalette{
  0xFF0F172Au,
  0xFF111E30u,
  0xFF263449u,
  0xFF27364Eu,
  0xFF0F172Au,
  0xFF27364Eu,
  0xFF1B3658u,
  0xFF3B82F6u,
  0xFF60A5FAu,
  0xFFF8FAFCu,
  0xFFCBD5E1u,
  0xFFBFDBFEu,
  0xFF0F172Au,
  0xFF7F1D1Du,
  0xFFFECACAu,
  0xFF0B1220u
};
inline constexpr const ThemePalette& Palette(ThemeMode mode) {
  return mode == ThemeMode::Dark ? kDarkPalette : kLightPalette;
}
}  // namespace rc_ui
