#pragma once

struct card {
    virtual ~card() = default;

    virtual bool render() = 0;

    ImVec4 first_color {0.0f, 0.0, 0.0f, 1.0f};
    ImVec4 second_color {0.0f, 0.0f, 0.0f, 0.5f};
    ImVec2 size {300.0f, 400.0f};
};
