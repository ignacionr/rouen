module;

#include "helpers/llm_config.hpp"
#include "cards/system/terminal.hpp"
#include "cards/system/settings.hpp"
#include "cards/system/sysinfo.hpp"
#include "cards/system/about.hpp"

export module rouen.cards.system;

export namespace rouen::cards::system {
    using rouen::cards::terminal;
    using rouen::cards::settings_card;
}
