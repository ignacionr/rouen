#pragma once

#include "imgui_include.hpp"
#include <string>
#include <cmath>

namespace rouen::flags {

inline void draw_flag(ImDrawList* draw_list, ImVec2 pos, ImVec2 size, const std::string& team_code) {
    // Draw grey border box
    draw_list->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(80, 80, 80, 255), 0.0f, 0, 1.0f);
    
    float w = size.x;
    float h = size.y;
    ImVec2 end = ImVec2(pos.x + w, pos.y + h);

    if (team_code == "ALG") { // Algeria
        draw_list->AddRectFilled(pos, ImVec2(pos.x + w/2, end.y), IM_COL32(0, 98, 51, 255));
        draw_list->AddRectFilled(ImVec2(pos.x + w/2, pos.y), end, IM_COL32(255, 255, 255, 255));
        // Crescent & Star (Red)
        ImVec2 center = ImVec2(pos.x + w/2, pos.y + h/2);
        draw_list->AddCircleFilled(center, h/4, IM_COL32(210, 16, 52, 255));
        draw_list->AddCircleFilled(ImVec2(center.x + w/15, center.y), h/5, IM_COL32(255, 255, 255, 255));
        draw_list->AddTriangleFilled(
            ImVec2(center.x + w/15, center.y - h/12),
            ImVec2(center.x + w/15, center.y + h/12),
            ImVec2(center.x + w/6, center.y),
            IM_COL32(210, 16, 52, 255)
        );
    } else if (team_code == "ARG") { // Argentina
        draw_list->AddRectFilled(pos, ImVec2(end.x, pos.y + h/3), IM_COL32(116, 172, 223, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + h/3), ImVec2(end.x, pos.y + 2*h/3), IM_COL32(255, 255, 255, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + 2*h/3), end, IM_COL32(116, 172, 223, 255));
        // Sun of May (Gold dot)
        draw_list->AddCircleFilled(ImVec2(pos.x + w/2, pos.y + h/2), h/8, IM_COL32(246, 180, 14, 255));
    } else if (team_code == "AUS") { // Australia
        draw_list->AddRectFilled(pos, end, IM_COL32(0, 0, 139, 255));
        // Union Jack in canton (simplified white diagonals/crosses)
        float cw = w / 2.0f;
        float ch = h / 2.0f;
        draw_list->AddRectFilled(pos, ImVec2(pos.x + cw, pos.y + ch), IM_COL32(0, 35, 149, 255));
        draw_list->AddLine(pos, ImVec2(pos.x + cw, pos.y + ch), IM_COL32(255, 255, 255, 255), 1.5f);
        draw_list->AddLine(ImVec2(pos.x, pos.y + ch), ImVec2(pos.x + cw, pos.y), IM_COL32(255, 255, 255, 255), 1.5f);
        draw_list->AddLine(ImVec2(pos.x + cw/2, pos.y), ImVec2(pos.x + cw/2, pos.y + ch), IM_COL32(255, 0, 0, 255), 1.5f);
        draw_list->AddLine(ImVec2(pos.x, pos.y + ch/2), ImVec2(pos.x + cw, pos.y + ch/2), IM_COL32(255, 0, 0, 255), 1.5f);
        // Federation Star under canton
        draw_list->AddCircleFilled(ImVec2(pos.x + cw/2, pos.y + 3*ch/2), 2.0f, IM_COL32(255, 255, 255, 255));
        // Southern Cross stars
        draw_list->AddCircleFilled(ImVec2(pos.x + 3*cw/2, pos.y + ch/3), 1.0f, IM_COL32(255, 255, 255, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + 3*cw/2, pos.y + 5*ch/3), 1.0f, IM_COL32(255, 255, 255, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + 7*cw/4, pos.y + ch), 1.0f, IM_COL32(255, 255, 255, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + 5*cw/4, pos.y + 6*ch/5), 1.0f, IM_COL32(255, 255, 255, 255));
    } else if (team_code == "AUT") { // Austria
        draw_list->AddRectFilled(pos, ImVec2(end.x, pos.y + h/3), IM_COL32(239, 43, 45, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + h/3), ImVec2(end.x, pos.y + 2*h/3), IM_COL32(255, 255, 255, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + 2*h/3), end, IM_COL32(239, 43, 45, 255));
    } else if (team_code == "BEL") { // Belgium
        draw_list->AddRectFilled(pos, ImVec2(pos.x + w/3, end.y), IM_COL32(0, 0, 0, 255));
        draw_list->AddRectFilled(ImVec2(pos.x + w/3, pos.y), ImVec2(pos.x + 2*w/3, end.y), IM_COL32(253, 218, 36, 255));
        draw_list->AddRectFilled(ImVec2(pos.x + 2*w/3, pos.y), end, IM_COL32(239, 47, 45, 255));
    } else if (team_code == "BIH") { // Bosnia
        draw_list->AddRectFilled(pos, end, IM_COL32(0, 47, 108, 255));
        draw_list->AddTriangleFilled(
            ImVec2(pos.x + w/3, pos.y),
            ImVec2(pos.x + 5*w/6, pos.y),
            ImVec2(pos.x + 5*w/6, end.y),
            IM_COL32(255, 204, 0, 255)
        );
        // Row of stars
        for (int i = 0; i < 5; ++i) {
            float fi = static_cast<float>(i);
            draw_list->AddCircleFilled(ImVec2(pos.x + w/3.0f + fi*w/12.0f, pos.y + fi*h/5.0f), 1.0f, IM_COL32(255, 255, 255, 255));
        }
    } else if (team_code == "BRA") { // Brazil
        draw_list->AddRectFilled(pos, end, IM_COL32(0, 156, 59, 255));
        draw_list->AddTriangleFilled(ImVec2(pos.x + w/2, pos.y + 2.0f), ImVec2(pos.x + 2.0f, pos.y + h/2), ImVec2(pos.x + w/2, end.y - 2.0f), IM_COL32(255, 223, 0, 255));
        draw_list->AddTriangleFilled(ImVec2(pos.x + w/2, pos.y + 2.0f), ImVec2(end.x - 2.0f, pos.y + h/2), ImVec2(pos.x + w/2, end.y - 2.0f), IM_COL32(255, 223, 0, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + w/2, pos.y + h/2), h/5, IM_COL32(0, 34, 115, 255));
    } else if (team_code == "CAN") { // Canada
        draw_list->AddRectFilled(pos, ImVec2(pos.x + w/4, end.y), IM_COL32(255, 0, 0, 255));
        draw_list->AddRectFilled(ImVec2(pos.x + w/4, pos.y), ImVec2(pos.x + 3*w/4, end.y), IM_COL32(255, 255, 255, 255));
        draw_list->AddRectFilled(ImVec2(pos.x + 3*w/4, pos.y), end, IM_COL32(255, 0, 0, 255));
        // Simple Maple Leaf vector
        draw_list->AddTriangleFilled(
            ImVec2(pos.x + w/2, pos.y + h/3),
            ImVec2(pos.x + w/2 - w/12, pos.y + 2*h/3),
            ImVec2(pos.x + w/2 + w/12, pos.y + 2*h/3),
            IM_COL32(255, 0, 0, 255)
        );
    } else if (team_code == "CIV") { // Ivory Coast
        draw_list->AddRectFilled(pos, ImVec2(pos.x + w/3, end.y), IM_COL32(247, 127, 0, 255));
        draw_list->AddRectFilled(ImVec2(pos.x + w/3, pos.y), ImVec2(pos.x + 2*w/3, end.y), IM_COL32(255, 255, 255, 255));
        draw_list->AddRectFilled(ImVec2(pos.x + 2*w/3, pos.y), end, IM_COL32(0, 156, 59, 255));
    } else if (team_code == "COD") { // DR Congo
        draw_list->AddRectFilled(pos, end, IM_COL32(0, 127, 255, 255));
        // Yellow-bordered red diagonal
        draw_list->AddLine(ImVec2(pos.x, end.y), ImVec2(end.x, pos.y), IM_COL32(255, 223, 0, 255), 4.0f);
        draw_list->AddLine(ImVec2(pos.x, end.y), ImVec2(end.x, pos.y), IM_COL32(206, 17, 38, 255), 2.0f);
        // Canton Star
        draw_list->AddCircleFilled(ImVec2(pos.x + w/6, pos.y + h/4), 2.0f, IM_COL32(255, 223, 0, 255));
    } else if (team_code == "COL") { // Colombia
        draw_list->AddRectFilled(pos, ImVec2(end.x, pos.y + h/2), IM_COL32(255, 223, 0, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + h/2), ImVec2(end.x, pos.y + 3*h/4), IM_COL32(0, 56, 168, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + 3*h/4), end, IM_COL32(206, 17, 38, 255));
    } else if (team_code == "CPV") { // Cape Verde
        draw_list->AddRectFilled(pos, end, IM_COL32(0, 56, 168, 255));
        float sy = pos.y + 3*h/5;
        draw_list->AddRectFilled(ImVec2(pos.x, sy), ImVec2(end.x, sy + h/15), IM_COL32(255, 255, 255, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, sy + h/15), ImVec2(end.x, sy + 2*h/15), IM_COL32(206, 17, 38, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, sy + 2*h/15), ImVec2(end.x, sy + 3*h/15), IM_COL32(255, 255, 255, 255));
        // Star circle representation
        draw_list->AddCircle(ImVec2(pos.x + w/3, sy + h/10), 4.0f, IM_COL32(255, 223, 0, 255), 0, 1.0f);
    } else if (team_code == "CRO") { // Croatia
        draw_list->AddRectFilled(pos, ImVec2(end.x, pos.y + h/3), IM_COL32(206, 17, 38, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + h/3), ImVec2(end.x, pos.y + 2*h/3), IM_COL32(255, 255, 255, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + 2*h/3), end, IM_COL32(0, 47, 108, 255));
        // Simplified Checkered Shield
        ImVec2 sc = ImVec2(pos.x + w/2, pos.y + h/2);
        draw_list->AddRectFilled(ImVec2(sc.x - 4, sc.y - 4), ImVec2(sc.x + 4, sc.y + 4), IM_COL32(206, 17, 38, 255));
        draw_list->AddRect(ImVec2(sc.x - 4, sc.y - 4), ImVec2(sc.x + 4, sc.y + 4), IM_COL32(255, 255, 255, 255), 0.0f, 0, 1.0f);
    } else if (team_code == "CUW") { // Curacao
        draw_list->AddRectFilled(pos, end, IM_COL32(0, 47, 108, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + 3*h/4), ImVec2(end.x, pos.y + 3*h/4 + h/10), IM_COL32(255, 204, 0, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + w/6, pos.y + h/4), 2.0f, IM_COL32(255, 255, 255, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + w/3, pos.y + h/3), 1.0f, IM_COL32(255, 255, 255, 255));
    } else if (team_code == "CZE") { // Czech Republic
        draw_list->AddRectFilled(pos, ImVec2(end.x, pos.y + h/2), IM_COL32(255, 255, 255, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + h/2), end, IM_COL32(211, 12, 32, 255));
        draw_list->AddTriangleFilled(pos, ImVec2(pos.x + w/2, pos.y + h/2), ImVec2(pos.x, end.y), IM_COL32(17, 69, 126, 255));
    } else if (team_code == "ECU") { // Ecuador
        draw_list->AddRectFilled(pos, ImVec2(end.x, pos.y + h/2), IM_COL32(255, 223, 0, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + h/2), ImVec2(end.x, pos.y + 3*h/4), IM_COL32(0, 56, 168, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + 3*h/4), end, IM_COL32(206, 17, 38, 255));
        // Coat of arms
        draw_list->AddCircleFilled(ImVec2(pos.x + w/2, pos.y + 5*h/8), 2.0f, IM_COL32(139, 90, 43, 255));
    } else if (team_code == "EGY") { // Egypt
        draw_list->AddRectFilled(pos, ImVec2(end.x, pos.y + h/3), IM_COL32(206, 17, 38, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + h/3), ImVec2(end.x, pos.y + 2*h/3), IM_COL32(255, 255, 255, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + 2*h/3), end, IM_COL32(0, 0, 0, 255));
        // Eagle emblem
        draw_list->AddCircleFilled(ImVec2(pos.x + w/2, pos.y + h/2), 1.5f, IM_COL32(197, 160, 89, 255));
    } else if (team_code == "ENG") { // England
        draw_list->AddRectFilled(pos, end, IM_COL32(255, 255, 255, 255));
        draw_list->AddRectFilled(ImVec2(pos.x + w/2 - 2.0f, pos.y), ImVec2(pos.x + w/2 + 2.0f, end.y), IM_COL32(206, 17, 38, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + h/2 - 2.0f), ImVec2(end.x, pos.y + h/2 + 2.0f), IM_COL32(206, 17, 38, 255));
    } else if (team_code == "ESP") { // Spain
        draw_list->AddRectFilled(pos, ImVec2(end.x, pos.y + h/4), IM_COL32(198, 11, 30, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + h/4), ImVec2(end.x, pos.y + 3*h/4), IM_COL32(255, 196, 0, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + 3*h/4), end, IM_COL32(198, 11, 30, 255));
        // Coat of arms representation
        draw_list->AddRectFilled(ImVec2(pos.x + w/3, pos.y + h/3), ImVec2(pos.x + w/3 + 4.0f, pos.y + 2*h/3), IM_COL32(198, 11, 30, 255));
    } else if (team_code == "FRA") { // France
        draw_list->AddRectFilled(pos, ImVec2(pos.x + w/3, end.y), IM_COL32(0, 35, 149, 255));
        draw_list->AddRectFilled(ImVec2(pos.x + w/3, pos.y), ImVec2(pos.x + 2*w/3, end.y), IM_COL32(255, 255, 255, 255));
        draw_list->AddRectFilled(ImVec2(pos.x + 2*w/3, pos.y), end, IM_COL32(239, 65, 53, 255));
    } else if (team_code == "GER") { // Germany
        draw_list->AddRectFilled(pos, ImVec2(end.x, pos.y + h/3), IM_COL32(0, 0, 0, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + h/3), ImVec2(end.x, pos.y + 2*h/3), IM_COL32(255, 0, 0, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + 2*h/3), end, IM_COL32(255, 204, 0, 255));
    } else if (team_code == "GHA") { // Ghana
        draw_list->AddRectFilled(pos, ImVec2(end.x, pos.y + h/3), IM_COL32(206, 17, 38, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + h/3), ImVec2(end.x, pos.y + 2*h/3), IM_COL32(255, 204, 0, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + 2*h/3), end, IM_COL32(0, 107, 63, 255));
        // Black star
        draw_list->AddTriangleFilled(
            ImVec2(pos.x + w/2, pos.y + h/2 - 3),
            ImVec2(pos.x + w/2 - 3, pos.y + h/2 + 3),
            ImVec2(pos.x + w/2 + 3, pos.y + h/2 + 3),
            IM_COL32(0, 0, 0, 255)
        );
    } else if (team_code == "HAI") { // Haiti
        draw_list->AddRectFilled(pos, ImVec2(end.x, pos.y + h/2), IM_COL32(0, 32, 91, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + h/2), end, IM_COL32(210, 16, 52, 255));
        // Small White Coat of Arms Box
        draw_list->AddRectFilled(ImVec2(pos.x + w/2 - 4.0f, pos.y + h/2 - 3.0f), ImVec2(pos.x + w/2 + 4.0f, pos.y + h/2 + 3.0f), IM_COL32(255, 255, 255, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + w/2, pos.y + h/2), 1.5f, IM_COL32(0, 98, 51, 255));
    } else if (team_code == "IRN") { // Iran
        draw_list->AddRectFilled(pos, ImVec2(end.x, pos.y + h/3), IM_COL32(35, 159, 64, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + h/3), ImVec2(end.x, pos.y + 2*h/3), IM_COL32(255, 255, 255, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + 2*h/3), end, IM_COL32(218, 18, 26, 255));
        // Red crest center
        draw_list->AddCircleFilled(ImVec2(pos.x + w/2, pos.y + h/2), 2.0f, IM_COL32(218, 18, 26, 255));
    } else if (team_code == "IRQ") { // Iraq
        draw_list->AddRectFilled(pos, ImVec2(end.x, pos.y + h/3), IM_COL32(206, 17, 38, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + h/3), ImVec2(end.x, pos.y + 2*h/3), IM_COL32(255, 255, 255, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + 2*h/3), end, IM_COL32(0, 0, 0, 255));
        // Green script center
        draw_list->AddCircleFilled(ImVec2(pos.x + w/2, pos.y + h/2), 2.0f, IM_COL32(0, 122, 54, 255));
    } else if (team_code == "JOR") { // Jordan
        draw_list->AddRectFilled(pos, ImVec2(end.x, pos.y + h/3), IM_COL32(0, 0, 0, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + h/3), ImVec2(end.x, pos.y + 2*h/3), IM_COL32(255, 255, 255, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + 2*h/3), end, IM_COL32(0, 122, 54, 255));
        draw_list->AddTriangleFilled(pos, ImVec2(pos.x + w/2.2f, pos.y + h/2.0f), ImVec2(pos.x, end.y), IM_COL32(206, 17, 38, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + w/6, pos.y + h/2), 1.0f, IM_COL32(255, 255, 255, 255));
    } else if (team_code == "JPN") { // Japan
        draw_list->AddRectFilled(pos, end, IM_COL32(255, 255, 255, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + w/2, pos.y + h/2), h/4.5f, IM_COL32(188, 0, 45, 255));
    } else if (team_code == "KOR") { // South Korea
        draw_list->AddRectFilled(pos, end, IM_COL32(255, 255, 255, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + w/2, pos.y + h/2), h/4, IM_COL32(0, 71, 160, 255));
        draw_list->PathArcTo(ImVec2(pos.x + w/2, pos.y + h/2), h/4, 3.1415f, 0.0f);
        draw_list->PathFillConvex(IM_COL32(206, 17, 38, 255));
        // Black mini-dots for trigrams
        draw_list->AddCircleFilled(ImVec2(pos.x + w/4, pos.y + h/4), 1.0f, IM_COL32(0, 0, 0, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + 3*w/4, pos.y + h/4), 1.0f, IM_COL32(0, 0, 0, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + w/4, pos.y + 3*h/4), 1.0f, IM_COL32(0, 0, 0, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + 3*w/4, pos.y + 3*h/4), 1.0f, IM_COL32(0, 0, 0, 255));
    } else if (team_code == "KSA") { // Saudi Arabia
        draw_list->AddRectFilled(pos, end, IM_COL32(0, 108, 53, 255));
        // White sword line
        draw_list->AddLine(ImVec2(pos.x + w/4, pos.y + 2*h/3), ImVec2(pos.x + 3*w/4, pos.y + 2*h/3), IM_COL32(255, 255, 255, 255), 1.0f);
        draw_list->AddCircleFilled(ImVec2(pos.x + w/2, pos.y + h/2 - 2), 1.0f, IM_COL32(255, 255, 255, 255));
    } else if (team_code == "MAR") { // Morocco
        draw_list->AddRectFilled(pos, end, IM_COL32(193, 39, 45, 255));
        draw_list->AddCircle(ImVec2(pos.x + w/2, pos.y + h/2), h/6, IM_COL32(0, 98, 51, 255), 0, 1.5f);
    } else if (team_code == "MEX") { // Mexico
        draw_list->AddRectFilled(pos, ImVec2(pos.x + w/3, end.y), IM_COL32(0, 104, 71, 255));
        draw_list->AddRectFilled(ImVec2(pos.x + w/3, pos.y), ImVec2(pos.x + 2*w/3, end.y), IM_COL32(255, 255, 255, 255));
        draw_list->AddRectFilled(ImVec2(pos.x + 2*w/3, pos.y), end, IM_COL32(206, 17, 38, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + w/2, pos.y + h/2), h/6, IM_COL32(139, 90, 43, 255));
    } else if (team_code == "NED") { // Netherlands
        draw_list->AddRectFilled(pos, ImVec2(end.x, pos.y + h/3), IM_COL32(174, 28, 40, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + h/3), ImVec2(end.x, pos.y + 2*h/3), IM_COL32(255, 255, 255, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + 2*h/3), end, IM_COL32(33, 70, 139, 255));
    } else if (team_code == "NOR") { // Norway
        draw_list->AddRectFilled(pos, end, IM_COL32(239, 43, 45, 255));
        float cx = pos.x + w/3.5f;
        float cy = pos.y + h/2.0f;
        // White cross
        draw_list->AddRectFilled(ImVec2(pos.x, cy - 3), ImVec2(end.x, cy + 3), IM_COL32(255, 255, 255, 255));
        draw_list->AddRectFilled(ImVec2(cx - 3, pos.y), ImVec2(cx + 3, end.y), IM_COL32(255, 255, 255, 255));
        // Blue inner cross
        draw_list->AddRectFilled(ImVec2(pos.x, cy - 1), ImVec2(end.x, cy + 1), IM_COL32(0, 40, 104, 255));
        draw_list->AddRectFilled(ImVec2(cx - 1, pos.y), ImVec2(cx + 1, end.y), IM_COL32(0, 40, 104, 255));
    } else if (team_code == "NZL") { // New Zealand
        draw_list->AddRectFilled(pos, end, IM_COL32(0, 35, 149, 255));
        // Small Union Jack top left
        draw_list->AddRectFilled(pos, ImVec2(pos.x + w/2, pos.y + h/2), IM_COL32(0, 35, 149, 255));
        draw_list->AddLine(pos, ImVec2(pos.x + w/2, pos.y + h/2), IM_COL32(255, 255, 255, 255), 1.0f);
        draw_list->AddLine(ImVec2(pos.x, pos.y + h/2), ImVec2(pos.x + w/2, pos.y), IM_COL32(255, 255, 255, 255), 1.0f);
        // Small red dots on right
        draw_list->AddCircleFilled(ImVec2(pos.x + 3*w/4, pos.y + h/4), 1.0f, IM_COL32(206, 17, 38, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + 3*w/4, pos.y + 3*h/4), 1.0f, IM_COL32(206, 17, 38, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + 7*w/8, pos.y + h/2), 1.0f, IM_COL32(206, 17, 38, 255));
    } else if (team_code == "PAN") { // Panama
        draw_list->AddRectFilled(pos, ImVec2(pos.x + w/2, pos.y + h/2), IM_COL32(255, 255, 255, 255));
        draw_list->AddRectFilled(ImVec2(pos.x + w/2, pos.y), ImVec2(end.x, pos.y + h/2), IM_COL32(206, 17, 38, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + h/2), ImVec2(pos.x + w/2, end.y), IM_COL32(0, 56, 168, 255));
        draw_list->AddRectFilled(ImVec2(pos.x + w/2, pos.y + h/2), end, IM_COL32(255, 255, 255, 255));
        // Stars
        draw_list->AddCircleFilled(ImVec2(pos.x + w/4, pos.y + h/4), 1.5f, IM_COL32(0, 56, 168, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + 3*w/4, pos.y + 3*h/4), 1.5f, IM_COL32(206, 17, 38, 255));
    } else if (team_code == "PAR") { // Paraguay
        draw_list->AddRectFilled(pos, ImVec2(end.x, pos.y + h/3), IM_COL32(213, 43, 30, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + h/3), ImVec2(end.x, pos.y + 2*h/3), IM_COL32(255, 255, 255, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + 2*h/3), end, IM_COL32(0, 56, 168, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + w/2, pos.y + h/2), 1.5f, IM_COL32(0, 56, 168, 255));
    } else if (team_code == "POR") { // Portugal
        draw_list->AddRectFilled(pos, ImVec2(pos.x + w/2.5f, end.y), IM_COL32(0, 102, 0, 255));
        draw_list->AddRectFilled(ImVec2(pos.x + w/2.5f, pos.y), end, IM_COL32(255, 0, 0, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + w/2.5f, pos.y + h/2), h/6, IM_COL32(255, 204, 0, 255));
    } else if (team_code == "QAT") { // Qatar
        draw_list->AddRectFilled(pos, ImVec2(pos.x + w/3, end.y), IM_COL32(255, 255, 255, 255));
        draw_list->AddRectFilled(ImVec2(pos.x + w/3, pos.y), end, IM_COL32(141, 21, 58, 255));
    } else if (team_code == "RSA") { // South Africa
        draw_list->AddRectFilled(pos, ImVec2(end.x, pos.y + h/3), IM_COL32(224, 60, 49, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + h/3), ImVec2(end.x, pos.y + 2*h/3), IM_COL32(0, 122, 77, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + 2*h/3), end, IM_COL32(0, 35, 149, 255));
        draw_list->AddTriangleFilled(pos, ImVec2(pos.x + w/3, pos.y + h/2), ImVec2(pos.x, end.y), IM_COL32(0, 0, 0, 255));
    } else if (team_code == "SCO") { // Scotland
        draw_list->AddRectFilled(pos, end, IM_COL32(0, 101, 189, 255));
        draw_list->AddLine(pos, end, IM_COL32(255, 255, 255, 255), 1.5f);
        draw_list->AddLine(ImVec2(pos.x, end.y), ImVec2(end.x, pos.y), IM_COL32(255, 255, 255, 255), 1.5f);
    } else if (team_code == "SEN") { // Senegal
        draw_list->AddRectFilled(pos, ImVec2(pos.x + w/3, end.y), IM_COL32(0, 133, 66, 255));
        draw_list->AddRectFilled(ImVec2(pos.x + w/3, pos.y), ImVec2(pos.x + 2*w/3, end.y), IM_COL32(253, 239, 66, 255));
        draw_list->AddRectFilled(ImVec2(pos.x + 2*w/3, pos.y), end, IM_COL32(227, 27, 35, 255));
        // Green star
        draw_list->AddTriangleFilled(
            ImVec2(pos.x + w/2, pos.y + h/2 - 3),
            ImVec2(pos.x + w/2 - 3, pos.y + h/2 + 3),
            ImVec2(pos.x + w/2 + 3, pos.y + h/2 + 3),
            IM_COL32(0, 133, 66, 255)
        );
    } else if (team_code == "SUI") { // Switzerland
        draw_list->AddRectFilled(pos, end, IM_COL32(218, 41, 28, 255));
        draw_list->AddRectFilled(ImVec2(pos.x + w/2 - 2.0f, pos.y + h/4), ImVec2(pos.x + w/2 + 2.0f, end.y - h/4), IM_COL32(255, 255, 255, 255));
        draw_list->AddRectFilled(ImVec2(pos.x + w/4, pos.y + h/2 - 2.0f), ImVec2(end.x - w/4, pos.y + h/2 + 2.0f), IM_COL32(255, 255, 255, 255));
    } else if (team_code == "SWE") { // Sweden
        draw_list->AddRectFilled(pos, end, IM_COL32(0, 106, 167, 255));
        float cx = pos.x + w/3.5f;
        float cy = pos.y + h/2.0f;
        draw_list->AddRectFilled(ImVec2(pos.x, cy - 3), ImVec2(end.x, cy + 3), IM_COL32(254, 204, 2, 255));
        draw_list->AddRectFilled(ImVec2(cx - 3, pos.y), ImVec2(cx + 3, end.y), IM_COL32(254, 204, 2, 255));
    } else if (team_code == "TUN") { // Tunisia
        draw_list->AddRectFilled(pos, end, IM_COL32(224, 0, 27, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + w/2, pos.y + h/2), h/4, IM_COL32(255, 255, 255, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + w/2 + 1, pos.y + h/2), h/6, IM_COL32(224, 0, 27, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + w/2 + 3, pos.y + h/2), h/8, IM_COL32(255, 255, 255, 255));
    } else if (team_code == "TUR") { // Turkey
        draw_list->AddRectFilled(pos, end, IM_COL32(227, 10, 23, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + w/2.3f, pos.y + h/2), h/5, IM_COL32(255, 255, 255, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + w/2.1f, pos.y + h/2), h/6, IM_COL32(227, 10, 23, 255));
    } else if (team_code == "URU") { // Uruguay
        draw_list->AddRectFilled(pos, end, IM_COL32(255, 255, 255, 255));
        float sh = h / 9.0f;
        for (int i = 0; i < 9; ++i) {
            if (i % 2 == 1) {
                float fi = static_cast<float>(i);
                draw_list->AddRectFilled(ImVec2(pos.x, pos.y + fi*sh), ImVec2(end.x, pos.y + (fi+1.0f)*sh), IM_COL32(0, 56, 168, 255));
            }
        }
        draw_list->AddRectFilled(pos, ImVec2(pos.x + w/2.2f, pos.y + h/2.0f), IM_COL32(255, 255, 255, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + w/4.4f, pos.y + h/4.0f), 2.0f, IM_COL32(246, 180, 14, 255));
    } else if (team_code == "USA") { // United States
        float sh = h / 7.0f;
        for (int i = 0; i < 7; ++i) {
            ImU32 color = (i % 2 == 0) ? IM_COL32(191, 10, 48, 255) : IM_COL32(255, 255, 255, 255);
            float fi = static_cast<float>(i);
            draw_list->AddRectFilled(ImVec2(pos.x, pos.y + fi*sh), ImVec2(end.x, pos.y + (fi+1.0f)*sh), color);
        }
        draw_list->AddRectFilled(pos, ImVec2(pos.x + w/2.2f, pos.y + h/2.0f), IM_COL32(0, 40, 104, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + w/6, pos.y + h/4), 1.0f, IM_COL32(255, 255, 255, 255));
        draw_list->AddCircleFilled(ImVec2(pos.x + w/3, pos.y + h/4), 1.0f, IM_COL32(255, 255, 255, 255));
    } else if (team_code == "UZB") { // Uzbekistan
        draw_list->AddRectFilled(pos, ImVec2(end.x, pos.y + h/3), IM_COL32(0, 150, 214, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + h/3), ImVec2(end.x, pos.y + 2*h/3), IM_COL32(255, 255, 255, 255));
        draw_list->AddRectFilled(ImVec2(pos.x, pos.y + 2*h/3), end, IM_COL32(0, 153, 76, 255));
        draw_list->AddLine(ImVec2(pos.x, pos.y + h/3), ImVec2(end.x, pos.y + h/3), IM_COL32(206, 17, 38, 255), 1.0f);
        draw_list->AddLine(ImVec2(pos.x, pos.y + 2*h/3), ImVec2(end.x, pos.y + 2*h/3), IM_COL32(206, 17, 38, 255), 1.0f);
        // Crescent top left
        draw_list->AddCircleFilled(ImVec2(pos.x + w/6, pos.y + h/6), 1.5f, IM_COL32(255, 255, 255, 255));
    } else {
        // Fallback: Neutral Gray Flag
        draw_list->AddRectFilled(pos, end, IM_COL32(150, 150, 150, 255));
    }
}

} // namespace rouen::flags
