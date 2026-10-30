#pragma once
#include "rmtc_bot/types.hpp"
#include <functional>
#include <map>
namespace rmtc {
// User's clarification to rules 7 / 8.6. Prediction only, never overwrite self.position.
std::optional<Cell> choose_respawn(Cell initial_spawn,MapSize map,
    const std::function<bool(Cell)>& blocked,const std::vector<Cell>& occupied);
std::map<char,Cell> choose_simultaneous_respawns(const std::map<char,Cell>& initial_spawns,
    MapSize map,const std::function<bool(Cell)>& blocked,std::vector<Cell> other_living);
} // namespace rmtc
