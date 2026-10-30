#pragma once
#include "rmtc_bot/battle.hpp"
namespace rmtc {
// Hypothetical simultaneous hits derived solely from the current public state.
// Reflection contributes to kill sharing but grants no ordinary damage credit.
struct CombatHit {std::size_t source{},target{};double nominal{};bool ordinary{true};};
struct CombatLedger {
  std::vector<double> damage_taken,damage_credit,kill_credit;
  std::vector<bool> dead;
};
CombatLedger project_combat_ledger(const std::vector<Combatant>&,const std::vector<CombatHit>&);
double projected_effective_damage(double previous,double ordinary,double kill_credit,bool dead);
} // namespace rmtc
