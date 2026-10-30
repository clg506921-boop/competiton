#pragma once
#include "rmtc_bot/events.hpp"
namespace rmtc {
enum class EventPromptStyle { Baseline, Compact, Example };
const char* event_prompt_style_name(EventPromptStyle) noexcept;
nlohmann::json make_event_prompt_variant(EventPromptStyle,const std::string& source,
    const std::string& raw,Tick current,MapSize map,const nlohmann::json& known);
} // namespace rmtc
