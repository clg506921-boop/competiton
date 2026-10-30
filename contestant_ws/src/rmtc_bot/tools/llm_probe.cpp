#include "rmtc_bot/llm.hpp"
#include <iostream>
// Test-only executable. Always uses a fake token; refuses non-loopback URLs.
int main(int argc,char** argv){
  try{
    if(argc!=2)return 2;
    std::string url=argv[1];
    if(url.rfind("http://127.0.0.1:",0)!=0)return 2;
    auto slash=url.find('/',17);auto port=url.substr(17,slash==std::string::npos?std::string::npos:slash-17);
    if(port.empty()||port.find_first_not_of("0123456789")!=std::string::npos||std::stoul(port)<1||std::stoul(port)>65535)return 2;
    rmtc::CurlTransport transport;
    rmtc::LlmConfig config{url,"local-test-model","fake-token-for-local-test-only",256,false};
    rmtc::LlmJob job{"test-session","test-source",1,false,
      nlohmann::json::array({{{"role","user"},{"content","本地请求格式测试"}}}),0};
    auto reply=transport.send(config,job,[]{return false;});
    std::cout<<nlohmann::json{{"status",reply.status},{"error_code",reply.error_code},
      {"forwarded_success",reply.forwarded_success},{"content",reply.content},
      {"uncertain_inflight",reply.uncertain_inflight}}.dump()<<'\n';return 0;
  }catch(...){return 1;}
}
