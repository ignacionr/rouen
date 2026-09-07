// scripts/macros/plugin_automation.js

Rouen.log("[Macro] Plugin Automation script started");

// 1. Launch a card or react to events
Rouen.cards.create("js:scripts/user_cards/weather_card.js");

// 2. React when cards fire events
Rouen.on("card:hello:greeted", (evt) => {
    Rouen.log(`[Plugin Event] Hello card greeted user: ${evt.payload ? evt.payload.name : "unknown"}`);
});
