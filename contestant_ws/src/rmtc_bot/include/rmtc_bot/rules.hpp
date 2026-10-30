#pragma once
namespace rmtc::rules {
// Frozen at build time. Update after an official revision; never a live weight selector.
inline constexpr const char* version="v1.1";
inline constexpr double combat_damage_cap=500.0;
inline constexpr double combat_points_cap=15.0;
inline constexpr double resource_value_cap=120.0;
inline constexpr double resource_points_cap=30.0;
inline constexpr double death_damage_penalty=60.0;
inline constexpr double kill_damage_pool=60.0;
inline constexpr int collect_ticks=100,battle_ticks=500;
inline constexpr double collect_tick_ms=100.0,battle_tick_ms=500.0;
} // namespace rmtc::rules
