#pragma once
#include "rmtc_bot/types.hpp"
#include <nlohmann/json.hpp>
#include <set>

namespace rmtc {
struct ModelOutput {
  nlohmann::json canonical;
  std::vector<std::string> dropped_fields;
};
// These functions inspect model output, never natural-language referee announcements.
Result<ModelOutput> normalize_model_output(std::string_view text) noexcept;
std::vector<std::string> event_schema_errors(const nlohmann::json& canonical);
struct EventContext {
  MapSize map;
  Tick observed_tick{};
  std::set<std::string> source_ids,known_event_ids;
};
enum class EventIssue { Envelope, Schema, Source, Link, Geometry, Time, Count };
struct EventRejection {
  EventIssue issue{EventIssue::Envelope};
  std::string field; // Schema-owned path only; never a model-supplied value/key.
};
struct InterpretedEvents {
  std::vector<nlohmann::json> accepted;
  std::vector<std::string> rejected;
  std::vector<EventRejection> details;
};
InterpretedEvents validate_event_semantics(const ModelOutput&,const EventContext&);
struct TimeInterval {
  Tick from{},until_exclusive{};
  bool contains(Tick t) const {return from<=t&&t<until_exclusive;}
};
std::optional<TimeInterval> healing_interval(Tick successful_entry,Tick heal_ticks);
std::optional<TimeInterval> vulnerability_interval(Tick successful_move,Tick start,
    Tick trigger_end,Tick effect_end);
nlohmann::json make_event_prompt(const std::string& source_id,const std::string& raw_announcement,
    Tick current_tick,MapSize map,const nlohmann::json& known_events);
// Frozen v0.9 contract retained for measured comparisons; no local semantic parsing.
nlohmann::json make_baseline_event_prompt(const std::string& source_id,const std::string& raw_announcement,
    Tick current_tick,MapSize map,const nlohmann::json& known_events);
} // namespace rmtc
