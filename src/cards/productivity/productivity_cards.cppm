module;

#include "cards/productivity/calculator.hpp"
#include "cards/productivity/converter.hpp"
#include "cards/productivity/invoice_card.hpp"
#include "cards/productivity/theme_card.hpp"
#include "cards/productivity/alarm.hpp"
#include "cards/productivity/editor.hpp"
#include "cards/productivity/pomodoro.hpp"

export module rouen.cards.productivity;

export namespace rouen::cards::productivity {
    using rouen::cards::calculator;
    using rouen::cards::converter;
    using rouen::cards::invoice_card;
}
