// scripts/user_cards/weather_card.js

let currentCity = "Rouen";
let currentTemp = "18°C";
let weatherCondition = "Partly Cloudy";

function onRender() {
    return {
        type: "AdaptiveCard",
        version: "1.5",
        body: [
            {
                type: "TextBlock",
                text: `☀️ Weather in ${currentCity}`,
                size: "Large",
                weight: "Bolder"
            },
            {
                type: "FactSet",
                facts: [
                    { title: "Temperature:", value: currentTemp },
                    { title: "Condition:", value: weatherCondition }
                ]
            },
            {
                type: "Input.Text",
                id: "new_city",
                placeholder: "Enter new city (e.g. Paris, London)"
            }
        ],
        actions: [
            {
                type: "Action.Submit",
                title: "Update Weather",
                data: { action: "refresh" }
            }
        ]
    };
}

function onSubmit(formData) {
    if (formData && formData.new_city) {
        currentCity = formData.new_city;
        weatherCondition = "Updated via QuickJS";
    }
    return onRender();
}
