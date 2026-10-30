#pragma once
#include "rmtc_bot/battle.hpp"
#include "rmtc_bot/llm.hpp"
#include <map>
namespace rmtc {
struct RuntimeConfig {
  CoreConfig core;
  DecisionType decision_type{DecisionType::Balanced};
  LlmConfig llm;
};
// Startup-only inputs. This function does no I/O and never returns credential values in errors.
RuntimeConfig parse_runtime_config(const nlohmann::json& connection,
    const std::map<std::string,std::string>& environment,const nlohmann::json& decision,
    const std::string& research_policy="greedy",bool wait_only=false);
const char* stage_name(StageMode);
const char* decision_type_name(DecisionType);
} // namespace rmtc
