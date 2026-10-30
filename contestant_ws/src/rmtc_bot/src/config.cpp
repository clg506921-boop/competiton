#include "rmtc_bot/config.hpp"
#include <limits>
#include <set>
#include <stdexcept>
namespace rmtc {
namespace {
using Json=nlohmann::json;
std::uint64_t unsigned_value(const Json& v,const char* field){
  if((!v.is_number_integer()&&!v.is_number_unsigned())||v.is_boolean()||
      (v.is_number_integer()&&!v.is_number_unsigned()&&v.get<std::int64_t>()<0))
    throw std::invalid_argument(std::string("Invalid configuration field: ")+field);
  return v.get<std::uint64_t>();
}
Json numeric_env(const std::string& v){
  auto out=Json::parse(v,nullptr,false);
  if(out.is_discarded()||!out.is_number_integer()||out.is_boolean())throw std::invalid_argument("Invalid numeric environment parameter");
  return out;
}
}
const char* stage_name(StageMode s){return s==StageMode::Collect?"1":s==StageMode::Battle?"2":s==StageMode::Diagnostic?"0":"auto";}
const char* decision_type_name(DecisionType t){return t==DecisionType::Collect?"collect":"balanced";}
RuntimeConfig parse_runtime_config(const Json& connection,const std::map<std::string,std::string>& env,
    const Json& decision,const std::string& policy,bool wait){
  if(!connection.is_object()||!decision.is_object())throw std::invalid_argument("Configuration must be an object");
  const std::set<std::string> allowed{"stage","max_ticks","tick_ms","ai_url","ai_token","ai_model","max_tokens","ai_quota","official_default_model_authorized"};
  for(auto it=connection.begin();it!=connection.end();++it)
    if(!allowed.count(it.key()))throw std::invalid_argument("Unknown connection configuration field");
  if(decision.size()!=1||!decision.contains("decision_type")||!decision["decision_type"].is_string())
    throw std::invalid_argument("decision_config must contain only decision_type");
  RuntimeConfig c;auto mode=decision["decision_type"].get<std::string>();
  if(mode=="collect")c.decision_type=DecisionType::Collect;
  else if(mode!="balanced")throw std::invalid_argument("Unregistered decision_type");
  Json j=connection;
  auto inputs=env;
  if(auto official=inputs.find("RMTC_TEAM_TOKEN");official!=inputs.end()){
    auto alias=inputs.find("RMTC_AI_TOKEN");
    if(alias!=inputs.end()&&alias->second!=official->second)throw std::invalid_argument("Conflicting token environment variables");
    inputs["RMTC_AI_TOKEN"]=official->second;
  }
  const std::map<std::string,std::string> fields{{"RMTC_STAGE","stage"},{"RMTC_MAX_TICKS","max_ticks"},{"RMTC_TICK_MS","tick_ms"},
    {"RMTC_AI_URL","ai_url"},{"RMTC_AI_TOKEN","ai_token"},{"RMTC_AI_MODEL","ai_model"},
    {"RMTC_AI_MAX_TOKENS","max_tokens"},{"RMTC_AI_QUOTA","ai_quota"},
    {"RMTC_AI_ALLOW_DEFAULT_MODEL","official_default_model_authorized"}};
  for(const auto& pair:fields){auto it=inputs.find(pair.first);if(it==inputs.end())continue;
    if(pair.second=="max_ticks"||pair.second=="tick_ms"||pair.second=="max_tokens"||pair.second=="ai_quota")j[pair.second]=numeric_env(it->second);
    else if(pair.second=="official_default_model_authorized"){
      if(it->second!="0"&&it->second!="1")throw std::invalid_argument("Invalid default-model authorization flag");
      j[pair.second]=it->second=="1";
    }else j[pair.second]=it->second;
  }
  auto text=[&](const char* field,std::string fallback=""){
    if(!j.contains(field))return fallback;
    if(!j[field].is_string())throw std::invalid_argument(std::string("Invalid configuration field: ")+field);
    return j[field].get<std::string>();
  };
  auto stage=text("stage","auto");
  if(stage=="1")c.core.stage=StageMode::Collect;else if(stage=="2")c.core.stage=StageMode::Battle;
  else if(stage=="0")c.core.stage=StageMode::Diagnostic;
  else if(stage!="auto")throw std::invalid_argument("Invalid stage");
  c.core.wait_only=wait||c.core.stage==StageMode::Diagnostic;
  if(j.contains("tick_ms")){auto n=unsigned_value(j["tick_ms"],"tick_ms");
    if(n==0||n>60000)throw std::invalid_argument("Invalid tick_ms");
    c.core.tick_ms=static_cast<double>(n);}
  if(policy=="lookahead")c.core.policy=CollectorPolicy::Lookahead;
  else if(policy=="frontier")c.core.policy=CollectorPolicy::Frontier;
  else if(policy!="greedy")throw std::invalid_argument("Invalid research policy");
  if(j.contains("max_ticks")){auto n=unsigned_value(j["max_ticks"],"max_ticks");
    if(n==0||n>static_cast<std::uint64_t>(std::numeric_limits<Tick>::max()))throw std::invalid_argument("Invalid horizon");
    c.core.horizon=static_cast<Tick>(n);
  }
  c.llm.url=text("ai_url");c.llm.token=text("ai_token");c.llm.model=text("ai_model");
  if(j.contains("max_tokens")){
    const auto& v=j["max_tokens"];
    if(!v.is_number_integer()||v.is_boolean())throw std::invalid_argument("Invalid max_tokens type");
    if(v.is_number_unsigned()){
      auto n=v.get<std::uint64_t>();if(n<=static_cast<std::uint64_t>(std::numeric_limits<int>::max()))c.llm.max_tokens=static_cast<int>(n);
    }else{auto n=v.get<std::int64_t>();if(n>0&&n<=std::numeric_limits<int>::max())c.llm.max_tokens=static_cast<int>(n);}
    // Missing/nonpositive/unrepresentable announced L disables AI only; control continues.
  }
  if(j.contains("ai_quota"))c.llm.call_quota=unsigned_value(j["ai_quota"],"ai_quota");
  if(j.contains("official_default_model_authorized")){
    if(!j["official_default_model_authorized"].is_boolean())throw std::invalid_argument("Invalid default-model authorization");
    c.llm.official_default_model_authorized=j["official_default_model_authorized"].get<bool>();
  }
  return c;
}
} // namespace rmtc
