#include "rmtc_bot/core.hpp"
#include "rmtc_bot/protocol.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>

int main(int argc,char** argv){
  try {
    if(argc<2||argc>3){std::cerr<<"Usage: replay_tool perception.jsonl [A|B|C|D]\n";return 2;}
    std::string seat=argc==3?argv[2]:"A";if(!rmtc::valid_seat(seat))return 2;
    std::ifstream file(argv[1]);if(!file){std::cerr<<"Cannot open replay input\n";return 2;}
    rmtc::BotCore core;std::string line;
    auto now=std::chrono::steady_clock::now();std::string previous_session;rmtc::Tick previous_tick=-1;
    std::size_t accepted=0,rejected=0;
    while(std::getline(file,line)){
      auto row=nlohmann::json::parse(line,nullptr,false);if(row.is_discarded()){++rejected;continue;}
      if(row.is_object()&&row.contains("kind")){
        if(row.value("kind",std::string{})!="perception"||row.value("seat",seat)!=seat)continue;
        if(!row.contains("data")){++rejected;continue;}row=row["data"];
      }
      auto parsed=rmtc::parse_observation(row.dump(),seat);
      if(!std::holds_alternative<rmtc::Observation>(parsed)){++rejected;continue;}
      const auto& o=std::get<rmtc::Observation>(parsed);
      // Synthetic scheduling for functional replay only, never used for latency claims.
      now+=std::chrono::milliseconds(o.session_id!=previous_session?1000:
          std::max<rmtc::Tick>(0,std::min<rmtc::Tick>(1000,o.tick-previous_tick))*100);
      previous_session=o.session_id;previous_tick=o.tick;
      auto decision=core.process(o,now);if(!decision)continue;
      std::cout<<rmtc::encode_action(decision->action)<<'\n';++accepted;
    }
    std::cerr<<"accepted="<<accepted<<" rejected="<<rejected<<" (functional replay, no network)\n";
    return rejected?1:0;
  }catch(const std::exception&){std::cerr<<"Replay failed\n";return 1;}
  catch(...){return 1;}
}
