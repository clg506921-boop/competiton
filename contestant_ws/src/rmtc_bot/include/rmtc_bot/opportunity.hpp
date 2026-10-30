#pragma once
#include "rmtc_bot/types.hpp"
namespace rmtc {
enum class OpportunityKind { Healing, Fortress };
// Typed data derived only from validated model output and structured own receipts.
struct PublicOpportunity {
  OpportunityKind kind{OpportunityKind::Healing};
  std::string event_id;
  Tick start{},until_exclusive{},entry_until_exclusive{},heal_ticks{};
  double heal_per_tick{};
  int required_dwell{},confirmed_dwell{};
  std::vector<Cell> targets;
};
struct HealingCoverage {Tick until_exclusive{};double per_tick{};};
struct OpportunityEstimate {
  Tick benefit_from{},until_exclusive{};
  int control_ticks{};
  double incremental_hp{},estimated_value{};
};
// Values are bounded policy estimates, never official score or predicted authoritative HP.
std::optional<OpportunityEstimate> estimate_healing_opportunity(const PublicOpportunity&,Tick now,
    int successful_moves,double observed_hp,std::optional<HealingCoverage> existing,Tick horizon);
std::optional<OpportunityEstimate> estimate_fortress_opportunity(const PublicOpportunity&,Tick now,
    int moves_to_area,double potential_damage_points,Tick horizon);
} // namespace rmtc
