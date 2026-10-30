#pragma once
#include "rmtc_bot/events.hpp"
#include "rmtc_bot/opportunity.hpp"
#include <map>
#include <deque>
namespace rmtc {
class World;
struct PreMoveHazard {double damage{};bool forced_respawn{};};
struct PersonalEffect {
  std::string type,event_id;
  std::optional<Tick> granted_tick;
  Tick known_from{},until_exclusive{};
};
struct MergeOutcome {bool applied{};std::string event_id,reason;};
// Owned by the single control thread. Accept ONLY events from validate_event_semantics.
class EventMemory {
 public:
  void reset(std::string session);
  MergeOutcome merge_validated(nlohmann::json event,Tick notice_observed_tick);
  void observe(const Observation&);
  nlohmann::json known_events(Tick now) const;
  bool public_active(const std::string& event_id,Tick tick) const;
  bool dangerous(Cell p,Tick tick,bool protected_now) const;
  std::optional<nlohmann::json> active_parameters(const std::string& type,Tick tick) const;
  bool split_wall(Cell p,Tick tick) const;
  double possible_chili_damage(Cell destination,Tick action_tick,bool protected_after_move,
      const std::vector<Entity>& visible) const;
  std::optional<PersonalEffect> self_effect(const std::string& type,Tick now) const;
  std::optional<PersonalEffect> known_effect(const std::string& seat,const std::string& type,Tick now)const;
  std::optional<nlohmann::json> effect_parameters(const PersonalEffect&)const;
  PreMoveHazard before_move(Cell,Tick,bool protected_now)const;
  bool wall_may_expire(Cell,Tick first_seen,Tick last_seen,Tick at)const;
  void reconcile_walls(World&,Tick now)const;
  std::vector<PublicOpportunity> opportunities(MapSize,Cell current,Tick now)const;
 private:
  struct Record {
    nlohmann::json data;
    std::optional<Tick> closed_observed;
    Tick derived_through{-1},last_dwell_tick{-1};
    int dwell{};
  };
  bool grant(std::string type,std::string event_id,std::optional<Tick> granted,Tick known_from,Tick until);
  bool grant_for(const std::string& seat,std::string type,std::string event_id,
      std::optional<Tick> granted,Tick known_from,Tick until);
  void derive(const std::string& id,Record&,const Observation&,const Observation* previous);
  bool split_contains(const nlohmann::json& area,Cell)const;
  std::optional<Cell> own_spawn_;
  std::string session_,seat_;std::size_t sequence_{};
  std::map<std::string,Record> records_;
  std::map<std::string,PersonalEffect> effects_;
  std::map<std::string,std::string> source_to_event_;
  std::deque<Observation> history_;
};
} // namespace rmtc
