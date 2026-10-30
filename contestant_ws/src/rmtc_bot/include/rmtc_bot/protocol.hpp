#pragma once
#include "rmtc_bot/types.hpp"
#include <string_view>
namespace rmtc {
Result<Cell> parse_relative_position(std::string_view text);
Result<Observation> parse_observation(std::string_view text, std::string_view expected_seat) noexcept;
std::string encode_action(const Action& action);
const char* direction_name(Dir d);
const char* action_name(ActionType a);
bool valid_seat(std::string_view seat);
bool valid_team_name(std::string_view name);
} // namespace rmtc
