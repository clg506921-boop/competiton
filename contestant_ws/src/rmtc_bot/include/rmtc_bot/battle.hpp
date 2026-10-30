#pragma once
#include "rmtc_bot/types.hpp"
#include <functional>
namespace rmtc {
// A hypothetical simultaneous-combat scenario, not knowledge of enemy intent.
// Do NOT copy perception.defending into defending_this_tick as a guaranteed fact.
struct Combatant {
  std::string seat;
  Cell position;
  double hp{};
  bool protected_now{},defending_this_tick{},vulnerable{};
  std::optional<double> vulnerability_multiplier{};
};
struct CombatParameters {
  double normal_damage{20},normal_defended_damage{10};
  double unrivaled_damage{50},unrivaled_defended_damage{10};
  double reflection_damage{20},vulnerability_multiplier{2};
  int unrivaled_size{5};
};
struct AttackTarget {std::string seat;double nominal_damage{};};
struct AttackEstimate {std::vector<AttackTarget> targets;double reflected_damage{};};
AttackEstimate estimate_attack(const Combatant& self,Dir direction,const std::vector<Combatant>& enemies,
    const CombatParameters&,bool unrivaled,bool reflection_active);
double apportioned_effective_damage(double hp,double own_nominal,double total_nominal);
double resource_points(double total_value);
double combat_points(double total_effective_damage);
enum class DecisionType { Balanced, Collect };
struct TacticalCandidate {
  ActionIntent intent;
  double score_points{},current_death_loss_points{},next_position_risk_points{};
  bool reliable_kill{},no_counter_under_confirmed_model{};
  double completed_mission_gain{},resource_value_gain{},effective_damage_gain{};
  double net_effective_damage_gain{}; // Signed forecast, never modifies referee score.
  double navigation_progress{};
  double surviving_hp{};
};
double candidate_utility(const TacticalCandidate&,int common_horizon);
std::optional<std::size_t> choose_tactical_candidate(const std::vector<TacticalCandidate>&,
    DecisionType,int common_horizon);
std::optional<Mission> nearest_unfinished_reach(const Observation&,
    const std::function<std::optional<int>(Cell)>& path_steps,Tick horizon);
} // namespace rmtc
