#pragma once
#include "rmtc_bot/core.hpp"
#include <functional>
namespace rmtc {
struct ReconGoal {
  Cell position;
  int steps{},stale_cells{};
  double estimated_rate{};
};
using ReconDistance=std::function<std::optional<int>(Cell)>;
using ReconFeasible=std::function<bool(Cell,int)>;
// Information refresh only: no enemy positions, attack prediction or official score award.
std::optional<ReconGoal> select_combat_recon(const Observation&,const World&,Tick horizon,
    const ReconDistance&,const ReconFeasible&);
} // namespace rmtc
