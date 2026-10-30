#pragma once
#include "rmtc_bot/types.hpp"
#include <functional>
#include <unordered_map>
namespace rmtc {
struct NavigationObjective {
  Cell position;
  int steps{};
  double immediate_points{},mission_points{},plan_rate{};
  bool resource{},reach{};
  double completed_missions{},resource_value{};
};
using PathSteps=std::function<std::optional<int>(Cell)>;
using BetweenSteps=std::function<std::optional<int>(Cell,Cell)>;
// Two-stop planning over legally remembered resources and public mission positions.
// Counts are confirmed lower bounds; a forecast never changes observed mission completion.
std::optional<NavigationObjective> select_navigation_objective(const Observation&,
    const std::unordered_map<int,Entity>& resources,int confirmed_normal,int confirmed_high,Tick horizon,
    const PathSteps&,const BetweenSteps&);
} // namespace rmtc
