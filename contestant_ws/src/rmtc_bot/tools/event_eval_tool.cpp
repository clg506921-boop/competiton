#include "rmtc_bot/prompt.hpp"
#include "rmtc_bot/llm.hpp"
#include <iostream>
// Offline stdin/stdout adapter. No network, ROS initialization or credentials.
int main(){
  try{
    std::string line;std::getline(std::cin,line);if(line.size()>2097152)return 2;
    auto in=nlohmann::json::parse(line,nullptr,false);if(in.is_discarded()||!in.is_object())return 2;
    const auto source=in.at("source_id").get<std::string>();
    const auto tick=in.at("current_tick").get<rmtc::Tick>();
    rmtc::MapSize map{in.at("width").get<int>(),in.at("height").get<int>()};
    if(in.value("operation","")=="prompt"){
      auto name=in.at("style").get<std::string>();
      auto style=name=="compact"?rmtc::EventPromptStyle::Compact:name=="example"?rmtc::EventPromptStyle::Example:rmtc::EventPromptStyle::Baseline;
      if(name=="production"){
        std::cout<<rmtc::make_event_prompt(source,in.at("announcement").get<std::string>(),tick,map,
          in.value("known_events",nlohmann::json::array())).dump()<<'\n';return 0;
      }
      if(name!="baseline"&&name!="compact"&&name!="example")return 2;
      std::cout<<rmtc::make_event_prompt_variant(style,source,in.at("announcement").get<std::string>(),tick,map,
        in.value("known_events",nlohmann::json::array())).dump()<<'\n';return 0;
    }
    if(in.value("operation","")!="evaluate")return 2;
    rmtc::LlmResult result;
    rmtc::EventContext context{map,tick,{source},{}};
    for(const auto& id:in.value("known_event_ids",nlohmann::json::array()))context.known_event_ids.insert(id.get<std::string>());
    result.job.event_context=context;
    result.reply=rmtc::decode_chat_completion(in.value("http_status",200L),in.at("response").dump());
    rmtc::inspect_model_reply(result);
    auto accepted=nlohmann::json::array();if(result.interpreted)for(const auto& e:result.interpreted->accepted)accepted.push_back(e);
    std::cout<<nlohmann::json{{"state",rmtc::model_output_state_name(result.diagnostics.state)},
      {"accepted",accepted},{"rejection_field",result.diagnostics.first_rejection_field},
      {"finish_reason",rmtc::completion_finish_name(result.reply.finish)},
      {"content_bytes",result.reply.content.size()},{"unresolved",result.diagnostics.unresolved}}.dump()<<'\n';return 0;
  }catch(...){std::cerr<<"Invalid experiment input\n";return 2;}
}
