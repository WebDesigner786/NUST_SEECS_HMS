#include "GuiRenderer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace hms {

namespace {

/*
 * Purpose: Case-insensitive substring match helper for room and guest filtering.
 * Inputs: haystack, needle.
 * Outputs: true if needle is empty or appears in haystack (case-insensitive).
 * Big-O: O(|haystack| * |needle|) worst case.
 */
bool containsIgnoreCase(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    std::string h = haystack;
    std::string n = needle;
    for (char& c : h) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    for (char& c : n) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    return (h.find(n) != std::string::npos);
}

/*
 * Purpose: Extracts the surname (last whitespace-separated token) of a guest's fullName for room tile captions.
 * Inputs: fullName string.
 * Outputs: Surname substring (capped to 10 chars for tile width).
 * Big-O: O(N) time, O(1) space.
 */
std::string extractSurname(const std::string& fullName) {
    const std::size_t pos = fullName.find_last_of(' ');
    std::string surname = (pos == std::string::npos) ? fullName : fullName.substr(pos + 1U);
    if (surname.size() > 10U) {
        surname = surname.substr(0U, 10U);
    }
    return surname;
}

/*
 * Purpose: Draws a small gold VIP crown vector glyph on a room tile using ImDrawList primitives.
 * Inputs: dl, topLeft position, color.
 * Outputs: Renders 3-peak crown icon.
 * Big-O: O(1) time, O(1) space.
 */
void drawVipCrownIcon(ImDrawList* dl, const ImVec2& pos, ImU32 color) {
    const ImVec2 pts[5] = {
        ImVec2(pos.x, pos.y + 9.0f),
        ImVec2(pos.x + 1.5f, pos.y + 2.0f),
        ImVec2(pos.x + 5.5f, pos.y + 6.0f),
        ImVec2(pos.x + 9.0f, pos.y + 0.0f),
        ImVec2(pos.x + 12.5f, pos.y + 6.0f)
    };
    dl->AddPolyline(pts, 5, color, 0, 1.8f);
    dl->AddLine(ImVec2(pos.x + 12.5f, pos.y + 6.0f), ImVec2(pos.x + 16.5f, pos.y + 2.0f), color, 1.8f);
    dl->AddLine(ImVec2(pos.x + 16.5f, pos.y + 2.0f), ImVec2(pos.x + 18.0f, pos.y + 9.0f), color, 1.8f);
    dl->AddLine(ImVec2(pos.x, pos.y + 9.5f), ImVec2(pos.x + 18.0f, pos.y + 9.5f), color, 2.0f);
}

/*
 * Purpose: Draws a compact horizontal feature progress bar used inside Smart Check-In Top-3 recommendation cards.
 * Inputs: label, score01 in [0..1], fillColor, pal.
 * Outputs: Renders labeled mini bar.
 * Big-O: O(1) time, O(1) space.
 */
void drawFeatureMiniBar(const char* label, double score01, const ImVec4& fillColor, const ThemePalette& pal) {
    const float val = static_cast<float>(std::clamp(score01, 0.0, 1.0));
    ImGui::TextColored(pal.dimText, "%-8s", label);
    ImGui::SameLine(72.0f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float barW = std::max(60.0f, ImGui::GetContentRegionAvail().x - 42.0f);
    const float barH = 10.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(
        ImVec2(pos.x, pos.y + 4.0f),
        ImVec2(pos.x + barW, pos.y + 4.0f + barH),
        ThemeEngine::vec4ToU32(pal.surface),
        4.0f
    );
    dl->AddRectFilled(
        ImVec2(pos.x, pos.y + 4.0f),
        ImVec2(pos.x + barW * val, pos.y + 4.0f + barH),
        ThemeEngine::vec4ToU32(fillColor),
        4.0f
    );
    ImGui::Dummy(ImVec2(barW + 6.0f, 16.0f));
    ImGui::SameLine();
    ImGui::TextColored(pal.text, "%3.0f%%", val * 100.0f);
}

} // namespace

// =============================================================================
// 1. LOYALTY PROMOTION GOLD CONFETTI BURST (1.5s, Section 9.4)
// =============================================================================
/*
 * Purpose: Spawns a 1.5-second gold confetti particle burst across the viewport on Loyalty promotion.
 * Inputs: None.
 * Outputs: Populates m_confetti and sets m_confettiRemaining = 1.5f.
 * Big-O: O(P) where P = 70 particles.
 */
void GuiRenderer::triggerLoyaltyConfetti() {
    m_confetti.clear();
    m_confetti.reserve(70U);
    m_confettiRemaining = 1.5f;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float originX = vp->Pos.x + vp->Size.x * 0.5f;
    const float originY = vp->Pos.y + vp->Size.y * 0.35f;

    std::mt19937 rng(static_cast<unsigned int>(ImGui::GetTime() * 1000.0));
    std::uniform_real_distribution<float> angleDist(0.0f, 6.28318f);
    std::uniform_real_distribution<float> speedDist(140.0f, 440.0f);
    std::uniform_real_distribution<float> sizeDist(4.0f, 9.0f);

    const ImVec4 palette[4] = {
        ThemeEngine::hexToVec4(0xFBBF24),
        ThemeEngine::hexToVec4(0xF5B942),
        ThemeEngine::hexToVec4(0xA78BFA),
        ThemeEngine::hexToVec4(0x22D3EE)
    };

    for (int i = 0; i < 70; ++i) {
        const float ang = angleDist(rng);
        const float spd = speedDist(rng);
        ConfettiParticle p{};
        p.x     = originX;
        p.y     = originY;
        p.vx    = std::cos(ang) * spd;
        p.vy    = std::sin(ang) * spd - 140.0f;
        p.size  = sizeDist(rng);
        p.color = palette[i % 4];
        m_confetti.push_back(p);
    }
}

/*
 * Purpose: Updates physics and renders active gold confetti particles on the foreground ImDrawList.
 * Inputs: deltaTime.
 * Outputs: Draws fading confetti squares over viewport.
 * Big-O: O(P) where P <= 70.
 */
void GuiRenderer::updateAndRenderConfetti(float deltaTime) {
    if (m_confettiRemaining <= 0.0f || m_confetti.empty()) {
        return;
    }
    m_confettiRemaining = std::max(0.0f, m_confettiRemaining - deltaTime);
    const float alpha = std::clamp(m_confettiRemaining / 1.5f, 0.0f, 1.0f);

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    for (ConfettiParticle& p : m_confetti) {
        p.vy += 520.0f * deltaTime; // gravity
        p.x  += p.vx * deltaTime;
        p.y  += p.vy * deltaTime;
        const ImVec4 col(p.color.x, p.color.y, p.color.z, alpha);
        dl->AddRectFilled(
            ImVec2(p.x, p.y),
            ImVec2(p.x + p.size, p.y + p.size * 0.7f),
            ThemeEngine::vec4ToU32(col),
            1.5f
        );
    }
}

/*
 * Purpose: Opens the Smart Check-In Wizard pre-targeted to a specific room or existing reservation.
 * Inputs: roomId, existingBookingId (-1 for walk-in).
 * Outputs: Configures m_wizardState and navigates to ScreenId::SmartCheckIn.
 * Big-O: O(B) time, O(1) space.
 */
void GuiRenderer::launchSmartCheckInForRoom(int roomId, int existingBookingId) {
    m_wizardState = SmartCheckInState{};
    m_wizardState.selectedRoomId = roomId;
    m_wizardState.preBookedBookingId = existingBookingId;

    if (existingBookingId != -1) {
        if (const Booking* b = m_system.findBookingById(existingBookingId)) {
            m_wizardState.selectedGuestId = b->guestId;
            m_wizardState.selectedRoomId  = b->roomId;
            m_wizardState.checkInOffsetDays = std::max(0, b->checkInDay - DateUtil::todayDayNumber());
            m_wizardState.nights = std::max(1, b->checkOutDay - b->checkInDay);
            m_wizardState.adults = b->adults;
            m_wizardState.children = b->children;
            m_wizardState.step = 3; // Jump straight to review & confirm for pre-booked guest
        }
    }
    navigateTo(ScreenId::SmartCheckIn);
}

// =============================================================================
// 2. INTERACTIVE FLOOR-PLAN ROOM MATRIX (Section 9.2)
// =============================================================================
/*
 * Purpose: Renders the 5-floor x 10-room interactive floor-plan matrix (Floor 5 top -> Floor 1 bottom)
 *          with elevator block between x05 and x06, stairs at both ends, 92x72 rounded tiles,
 *          VIP crown icons, pulsing urgent housekeeping rings, hover lift/glow + tooltip,
 *          status/type/floor/search filters, and status legend counts (Section 9.2).
 * Inputs: None.
 * Outputs: Renders Room Matrix screen and triggers Room NNN modal on tile click.
 * Big-O: O(R + T) where R = 50 rooms and T = open tasks.
 */
void GuiRenderer::renderRoomMatrixScreen() {
    const ThemePalette& pal = m_theme.getActivePalette();
    const HotelDatabase& db = m_system.db();
    const YieldPricingResult yield = m_system.getTodayYieldPricing();
    const float timeSec = static_cast<float>(ImGui::GetTime());

    drawSectionHeader(
        "Interactive Floor-Plan Room Matrix",
        "5 Floors · 50 Rooms (101–510) · Click any room tile for live accrued billing & operations"
    );

    // Count rooms per status for the legend & filter chips
    int counts[5] = {0, 0, 0, 0, 0};
    for (const Room& r : db.rooms) {
        counts[std::clamp(static_cast<int>(r.status), 0, 4)]++;
    }

    // Filters & Legend Bar Card
    if (beginCard("##RoomMatrixFilterBar", nullptr, ImVec2(0.0f, 108.0f))) {
        const char* statusNames[5] = {"Available", "Occupied", "Reserved", "Cleaning", "Maintenance"};
        const ImVec4 statusCols[5] = {
            pal.statusAvailable, pal.statusOccupied, pal.statusReserved, pal.statusCleaning, pal.statusMaintenance
        };

        for (int i = 0; i < 5; ++i) {
            if (i > 0) {
                ImGui::SameLine(0.0f, 8.0f);
            }
            const bool active = m_matrixState.statusFilter[i];
            char chipBuf[48]{};
            std::snprintf(chipBuf, sizeof(chipBuf), "%s (%d)##Chip%d", statusNames[i], counts[i], i);

            ImGui::PushStyleColor(
                ImGuiCol_Button,
                active ? ImVec4(statusCols[i].x, statusCols[i].y, statusCols[i].z, 0.28f) : pal.surface
            );
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(statusCols[i].x, statusCols[i].y, statusCols[i].z, 0.42f));
            ImGui::PushStyleColor(ImGuiCol_Border, active ? statusCols[i] : pal.border);
            ImGui::PushStyleColor(ImGuiCol_Text, active ? pal.text : pal.dimText);
            if (ImGui::Button(chipBuf)) {
                m_matrixState.statusFilter[i] = !m_matrixState.statusFilter[i];
            }
            ImGui::PopStyleColor(4);
        }

        ImGui::Spacing();
        const char* typeOptions[] = {"All Types", "Standard", "Deluxe", "Suite", "Presidential"};
        const char* floorOptions[] = {"All Floors", "Floor 5", "Floor 4", "Floor 3", "Floor 2", "Floor 1"};

        ImGui::SetNextItemWidth(150.0f);
        ImGui::Combo("##FilterType", &m_matrixState.typeFilterIndex, typeOptions, 5);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(135.0f);
        ImGui::Combo("##FilterFloor", &m_matrixState.floorFilterIndex, floorOptions, 6);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputTextWithHint("##FilterSearch", "Search room # or guest surname...", m_matrixState.searchBuf, sizeof(m_matrixState.searchBuf));
        ImGui::SameLine();
        if (ghostButton("Reset Filters", ImVec2(120.0f, 34.0f))) {
            for (int i = 0; i < 5; ++i) { m_matrixState.statusFilter[i] = true; }
            m_matrixState.typeFilterIndex = 0;
            m_matrixState.floorFilterIndex = 0;
            m_matrixState.searchBuf[0] = '\0';
        }
    }
    endCard();

    ImGui::Spacing();

    // Floor-plan grid card (Floor 5 at top down to Floor 1 at bottom)
    if (beginCard("##FloorPlanCanvas", nullptr, ImVec2(0.0f, 0.0f))) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        constexpr float kTileW = 92.0f;
        constexpr float kTileH = 72.0f;
        constexpr float kGap   = 8.0f;
        constexpr float kStairW = 46.0f;
        constexpr float kElevW  = 64.0f;

        for (int floor = Cfg::FLOORS; floor >= 1; --floor) {
            if (m_matrixState.floorFilterIndex != 0) {
                const int wantedFloor = 6 - m_matrixState.floorFilterIndex; // 1->Floor 5 .. 5->Floor 1
                if (floor != wantedFloor) {
                    continue;
                }
            }

            // Floor label
            if (m_fonts.captionFont != nullptr) {
                ImGui::PushFont(m_fonts.captionFont);
            }
            ImGui::TextColored(pal.dimText, "FLOOR %d", floor);
            if (m_fonts.captionFont != nullptr) {
                ImGui::PopFont();
            }

            const ImVec2 rowStart = ImGui::GetCursorScreenPos();
            float cursorX = rowStart.x;

            // Left Stairs block
            {
                const ImVec2 sMin(cursorX, rowStart.y);
                const ImVec2 sMax(cursorX + kStairW, rowStart.y + kTileH);
                dl->AddRectFilled(sMin, sMax, ThemeEngine::vec4ToU32(pal.surface), 8.0f);
                dl->AddRect(sMin, sMax, ThemeEngine::vec4ToU32(pal.border), 8.0f, 0, 1.0f);
                for (int st = 0; st < 4; ++st) {
                    const float sy = sMin.y + 16.0f + static_cast<float>(st) * 8.0f;
                    dl->AddLine(
                        ImVec2(sMin.x + 8.0f + static_cast<float>(st) * 5.0f, sy),
                        ImVec2(sMax.x - 8.0f, sy),
                        ThemeEngine::vec4ToU32(pal.dimText),
                        1.4f
                    );
                }
                if (m_fonts.captionFont != nullptr) {
                    dl->AddText(m_fonts.captionFont, 11.0f, ImVec2(sMin.x + 7.0f, sMax.y - 18.0f), ThemeEngine::vec4ToU32(pal.dimText), "STAIR");
                }
                cursorX += kStairW + kGap;
            }

            // 10 room tiles on this floor (with Elevator block between x05 and x06)
            for (int unit = 1; unit <= Cfg::ROOMS_PER_FLOOR; ++unit) {
                if (unit == 6) {
                    // Elevator block between rooms x05 and x06
                    const ImVec2 eMin(cursorX, rowStart.y);
                    const ImVec2 eMax(cursorX + kElevW, rowStart.y + kTileH);
                    dl->AddRectFilled(eMin, eMax, ThemeEngine::vec4ToU32(pal.surface), 8.0f);
                    dl->AddRect(eMin, eMax, ThemeEngine::vec4ToU32(pal.accent), 8.0f, 0, 1.2f);
                    const float mx = (eMin.x + eMax.x) * 0.5f;
                    // Up/Down arrows
                    dl->AddTriangleFilled(ImVec2(mx, eMin.y + 14.0f), ImVec2(mx - 6.0f, eMin.y + 23.0f), ImVec2(mx + 6.0f, eMin.y + 23.0f), ThemeEngine::vec4ToU32(pal.accent));
                    dl->AddTriangleFilled(ImVec2(mx, eMin.y + 39.0f), ImVec2(mx - 6.0f, eMin.y + 30.0f), ImVec2(mx + 6.0f, eMin.y + 30.0f), ThemeEngine::vec4ToU32(pal.secondaryAccent));
                    if (m_fonts.captionFont != nullptr) {
                        dl->AddText(m_fonts.captionFont, 11.0f, ImVec2(eMin.x + 15.0f, eMax.y - 19.0f), ThemeEngine::vec4ToU32(pal.dimText), "ELEV");
                    }
                    cursorX += kElevW + kGap;
                }

                const int roomNumber = floor * 100 + unit;
                const Room* room = m_system.findRoomByNumber(roomNumber);
                if (room == nullptr) {
                    cursorX += kTileW + kGap;
                    continue;
                }

                // Look up current guest if occupied/reserved
                const Booking* activeBooking = (room->currentBookingId != -1)
                                                   ? m_system.findBookingById(room->currentBookingId)
                                                   : nullptr;
                const Guest* currentGuest = (activeBooking != nullptr)
                                                ? m_system.findGuestById(activeBooking->guestId)
                                                : nullptr;

                // Check if room passes filters
                bool passesFilter = m_matrixState.statusFilter[std::clamp(static_cast<int>(room->status), 0, 4)];
                if (passesFilter && m_matrixState.typeFilterIndex != 0) {
                    passesFilter = (static_cast<int>(room->type) == (m_matrixState.typeFilterIndex - 1));
                }
                if (passesFilter && m_matrixState.searchBuf[0] != '\0') {
                    const std::string numStr = std::to_string(room->number);
                    const std::string gName = currentGuest ? currentGuest->fullName : "";
                    passesFilter = containsIgnoreCase(numStr, m_matrixState.searchBuf) ||
                                   containsIgnoreCase(gName, m_matrixState.searchBuf);
                }

                // Check if room has an urgent open housekeeping/maintenance task (priority >= 20 or Maintenance)
                bool hasUrgentTask = false;
                for (const HousekeepingTask& t : db.tasks) {
                    if (t.roomId == room->id && t.status != TaskStatus::Done &&
                        (t.priorityScore >= 18.0 || t.type == TaskType::Maintenance)) {
                        hasUrgentTask = true;
                        break;
                    }
                }

                ImGui::SetCursorScreenPos(ImVec2(cursorX, rowStart.y));
                char tileId[32]{};
                std::snprintf(tileId, sizeof(tileId), "##RoomTile_%d", room->number);
                const bool clicked = ImGui::InvisibleButton(tileId, ImVec2(kTileW, kTileH));
                const bool hovered = ImGui::IsItemHovered();

                const float liftY = hovered ? -3.0f : 0.0f; // Section 9.2: Hover lifts 3px with glow
                const ImVec2 tMin(cursorX, rowStart.y + liftY);
                const ImVec2 tMax(cursorX + kTileW, rowStart.y + kTileH + liftY);

                const ImVec4 baseCol = statusColor(room->status);
                const float tileAlpha = passesFilter ? 0.85f : 0.18f;
                const ImVec4 fillCol(baseCol.x, baseCol.y, baseCol.z, tileAlpha);

                if (hovered) {
                    dl->AddRectFilled(
                        ImVec2(tMin.x - 3.0f, tMin.y - 3.0f),
                        ImVec2(tMax.x + 3.0f, tMax.y + 3.0f),
                        ThemeEngine::vec4ToU32(ImVec4(baseCol.x, baseCol.y, baseCol.z, 0.35f)),
                        12.0f
                    );
                }

                dl->AddRectFilled(tMin, tMax, ThemeEngine::vec4ToU32(fillCol), 10.0f);
                dl->AddRect(
                    tMin,
                    tMax,
                    ThemeEngine::vec4ToU32(hovered ? pal.text : ImVec4(1.0f, 1.0f, 1.0f, passesFilter ? 0.25f : 0.08f)),
                    10.0f,
                    0,
                    hovered ? 2.0f : 1.0f
                );

                // Pulsing ring on rooms with an urgent housekeeping task
                if (hasUrgentTask && passesFilter) {
                    const float pulse = 0.5f + 0.5f * std::sin(timeSec * 5.0f);
                    dl->AddRect(
                        ImVec2(tMin.x - 2.0f, tMin.y - 2.0f),
                        ImVec2(tMax.x + 2.0f, tMax.y + 2.0f),
                        ThemeEngine::vec4ToU32(ImVec4(pal.goldHighlight.x, pal.goldHighlight.y, pal.goldHighlight.z, 0.35f + 0.65f * pulse)),
                        11.0f,
                        0,
                        2.2f
                    );
                }

                // VIP crown icon for Gold / Platinum guests
                if (currentGuest != nullptr &&
                    (currentGuest->tier == LoyaltyTier::Gold || currentGuest->tier == LoyaltyTier::Platinum) &&
                    passesFilter) {
                    drawVipCrownIcon(dl, ImVec2(tMax.x - 24.0f, tMin.y + 8.0f), ThemeEngine::vec4ToU32(tierColor(currentGuest->tier)));
                }

                // Room number in bold + type/guest-surname caption
                char numBuf[16]{};
                std::snprintf(numBuf, sizeof(numBuf), "%d", room->number);
                const ImU32 txtCol = IM_COL32(255, 255, 255, passesFilter ? 255 : 90);
                dl->AddText(ImVec2(tMin.x + 10.0f, tMin.y + 10.0f), txtCol, numBuf);
                dl->AddText(ImVec2(tMin.x + 10.6f, tMin.y + 10.0f), txtCol, numBuf); // subtle faux-bold

                const std::string caption = (currentGuest != nullptr)
                                                ? extractSurname(currentGuest->fullName)
                                                : std::string(toString(room->type));
                if (m_fonts.captionFont != nullptr) {
                    dl->AddText(m_fonts.captionFont, 12.0f, ImVec2(tMin.x + 10.0f, tMin.y + 34.0f), txtCol, caption.c_str());
                    const std::string rateShort = formatPKR(roundToNearest10PKR(room->baseRate * yield.surge));
                    dl->AddText(
                        m_fonts.captionFont,
                        11.0f,
                        ImVec2(tMin.x + 10.0f, tMin.y + 51.0f),
                        IM_COL32(255, 255, 255, passesFilter ? 210 : 70),
                        rateShort.c_str()
                    );
                }

                if (hovered) {
                    ImGui::BeginTooltip();
                    ImGui::TextColored(pal.accent, "Room %d — %s (%s)", room->number, toString(room->type), toString(room->status));
                    ImGui::Separator();
                    ImGui::Text("Rate Today : %s / night (Base %s)", formatPKR(roundToNearest10PKR(room->baseRate * yield.surge)).c_str(), formatPKR(room->baseRate).c_str());
                    ImGui::Text("View / Bed : %s View  |  %s Bed", toString(room->view), toString(room->bed));
                    ImGui::Text("Acoustics  : Noise %d/5  |  %d room(s) from elevator", room->noiseLevel, room->elevatorDistance);
                    if (currentGuest != nullptr) {
                        ImGui::Separator();
                        ImGui::TextColored(tierColor(currentGuest->tier), "Guest: %s (%s)", currentGuest->fullName.c_str(), toString(currentGuest->tier));
                    }
                    ImGui::EndTooltip();
                }

                if (clicked) {
                    m_matrixState.selectedRoomId   = room->id;
                    m_matrixState.openRoomModalReq = true;
                    m_matrixState.showSwapPicker   = false;
                    m_matrixState.showMaintForm    = false;
                    m_matrixState.showChargeForm   = false;
                    m_matrixState.showCheckoutForm = false;
                }

                cursorX += kTileW + kGap;
            }

            // Right Stairs block
            {
                const ImVec2 sMin(cursorX, rowStart.y);
                const ImVec2 sMax(cursorX + kStairW, rowStart.y + kTileH);
                dl->AddRectFilled(sMin, sMax, ThemeEngine::vec4ToU32(pal.surface), 8.0f);
                dl->AddRect(sMin, sMax, ThemeEngine::vec4ToU32(pal.border), 8.0f, 0, 1.0f);
                for (int st = 0; st < 4; ++st) {
                    const float sy = sMin.y + 16.0f + static_cast<float>(st) * 8.0f;
                    dl->AddLine(
                        ImVec2(sMin.x + 8.0f, sy),
                        ImVec2(sMax.x - 8.0f - static_cast<float>(st) * 5.0f, sy),
                        ThemeEngine::vec4ToU32(pal.dimText),
                        1.4f
                    );
                }
                if (m_fonts.captionFont != nullptr) {
                    dl->AddText(m_fonts.captionFont, 11.0f, ImVec2(sMin.x + 7.0f, sMax.y - 18.0f), ThemeEngine::vec4ToU32(pal.dimText), "STAIR");
                }
            }

            ImGui::SetCursorScreenPos(ImVec2(rowStart.x, rowStart.y + kTileH + 14.0f));
        }
    }
    endCard();

    renderRoomDetailModal();
}

// =============================================================================
// 3. MODAL "ROOM NNN" (640x520, Section 9.2)
// =============================================================================
/*
 * Purpose: Renders the 640x520 Room NNN detail modal with 3 columns:
 *          Left: Room specs (type, floor, view, bed, base rate, surge rate, noise, elevator distance)
 *          Middle: Current guest card & live accrued bill updating each frame
 *          Right: Role-guarded actions (Check-In, Check-Out, Room Swap with recommender score,
 *                 Dispatch Housekeeping, Flag Maintenance severity 1-5, Add Service Charge).
 * Inputs: None (uses m_matrixState.selectedRoomId).
 * Outputs: Executes room operations through HotelSystem and displays toasts.
 * Big-O: O(R log R) when Room Swap picker is opened; O(1) otherwise.
 */
void GuiRenderer::renderRoomDetailModal() {
    if (m_matrixState.openRoomModalReq) {
        ImGui::OpenPopup("##RoomDetailModal");
        m_matrixState.openRoomModalReq = false;
    }

    const Room* room = m_system.findRoomById(m_matrixState.selectedRoomId);
    if (room == nullptr) {
        return;
    }

    const ThemePalette& pal = m_theme.getActivePalette();
    const YieldPricingResult yield = m_system.getTodayYieldPricing();
    const Role role = m_system.currentRole();

    ImGui::SetNextWindowSize(ImVec2(720.0f, 540.0f), ImGuiCond_Appearing);
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("##RoomDetailModal", nullptr, ImGuiWindowFlags_NoResize)) {
        // Header with room number + status badge
        if (m_fonts.headingFont != nullptr) {
            ImGui::PushFont(m_fonts.headingFont);
        }
        ImGui::TextColored(pal.text, "Room %d", room->number);
        if (m_fonts.headingFont != nullptr) {
            ImGui::PopFont();
        }
        ImGui::SameLine(0.0f, 14.0f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0f);
        drawBadge(toString(room->status), statusColor(room->status));
        ImGui::SameLine(ImGui::GetWindowWidth() - 96.0f);
        if (ghostButton("Close", ImVec2(76.0f, 32.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        ImGui::Spacing();

        const Booking* activeBooking = (room->currentBookingId != -1)
                                           ? m_system.findBookingById(room->currentBookingId)
                                           : nullptr;
        const Guest* guest = (activeBooking != nullptr) ? m_system.findGuestById(activeBooking->guestId) : nullptr;

        if (ImGui::BeginTable("##RoomModalCols", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Specs", ImGuiTableColumnFlags_WidthFixed, 200.0f);
            ImGui::TableSetupColumn("Guest & Live Bill", ImGuiTableColumnFlags_WidthStretch, 1.15f);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 205.0f);
            ImGui::TableNextRow();

            // Column 1: Specs
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(pal.accent, "Room Specifications");
            ImGui::Spacing();
            ImGui::Text("Type      : %s", toString(room->type));
            ImGui::Text("Floor     : Floor %d", room->floor);
            ImGui::Text("View      : %s", toString(room->view));
            ImGui::Text("Bed       : %s", toString(room->bed));
            ImGui::Spacing();
            ImGui::Text("Base Rate : %s", formatPKR(room->baseRate).c_str());
            ImGui::TextColored(
                pal.goldHighlight,
                "Today Rate: %s",
                formatPKR(roundToNearest10PKR(room->baseRate * yield.surge)).c_str()
            );
            ImGui::Spacing();
            ImGui::Text("Noise Lvl : %d / 5", room->noiseLevel);
            ImGui::Text("Elevator  : %d rooms", room->elevatorDistance);
            if (!room->notes.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(pal.dimText, "Notes:");
                ImGui::TextWrapped("%s", room->notes.c_str());
            }

            // Column 2: Current Guest Card & Live Accrued Bill
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(pal.secondaryAccent, "Current Guest & Live Folio");
            ImGui::Spacing();
            if (activeBooking != nullptr && guest != nullptr) {
                ImGui::TextColored(pal.text, "%s", guest->fullName.c_str());
                drawBadge(toString(guest->tier), tierColor(guest->tier));
                ImGui::SameLine();
                ImGui::TextColored(pal.dimText, "  %s", guest->phone.c_str());
                ImGui::Spacing();

                const int32_t today = DateUtil::todayDayNumber();
                const int nightsSoFar = std::max(1, today - activeBooking->checkInDay);
                ImGui::Text(
                    "Stay   : %s -> %s",
                    DateUtil::formatDate(activeBooking->checkInDay).c_str(),
                    DateUtil::formatDate(activeBooking->checkOutDay).c_str()
                );
                ImGui::Text("Nights : %d booked (%d elapsed)", std::max(1, activeBooking->checkOutDay - activeBooking->checkInDay), nightsSoFar);
                ImGui::Separator();

                // Live accrued bill updating every frame
                const FolioBreakdown liveFolio = m_system.calculateFolio(
                    *activeBooking,
                    *guest,
                    std::max(activeBooking->checkOutDay, activeBooking->checkInDay + 1),
                    DateUtil::currentMinuteOfDay()
                );
                ImGui::Text("Room (%d nights) : %s", liveFolio.nights, formatPKR(liveFolio.roomSubtotal).c_str());
                ImGui::Text("Add-Ons (%zu)     : %s", activeBooking->addOns.size(), formatPKR(liveFolio.addOnSubtotal).c_str());
                if (liveFolio.lateCheckoutFee > 0.0) {
                    ImGui::TextColored(pal.statusReserved, "Late Checkout   : %s", formatPKR(liveFolio.lateCheckoutFee).c_str());
                }
                if (liveFolio.loyaltyDiscount > 0.0) {
                    ImGui::TextColored(pal.statusAvailable, "Loyalty Disc.   : -%s", formatPKR(liveFolio.loyaltyDiscount).c_str());
                }
                if (liveFolio.promoDiscount > 0.0) {
                    ImGui::TextColored(pal.statusAvailable, "Promo (%s) : -%s", liveFolio.appliedPromoCode.c_str(), formatPKR(liveFolio.promoDiscount).c_str());
                }
                ImGui::Text("GST (%.0f%%)       : %s", liveFolio.taxRate * 100.0, formatPKR(liveFolio.taxAmount).c_str());
                ImGui::Separator();
                ImGui::TextColored(pal.goldHighlight, "LIVE TOTAL      : %s", formatPKR(liveFolio.grandTotal).c_str());
            } else {
                drawEmptyState("No Active Guest", "Room is currently vacant.");
            }

            // Column 3: Action Buttons (role-guarded with explanatory tooltips)
            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(pal.accent, "Room Operations");
            ImGui::Spacing();

            const bool canCheckInRole = HotelSystem::can(role, Action::CheckInGuest);
            const bool canCheckInNow = canCheckInRole &&
                                       (room->status == RoomStatus::Available || room->status == RoomStatus::Reserved);
            if (primaryButton("Check-In", ImVec2(-1.0f, 34.0f), canCheckInNow)) {
                if (activeBooking != nullptr && activeBooking->status == BookingStatus::Reserved) {
                    m_system.checkInBooking(activeBooking->id);
                } else {
                    ImGui::CloseCurrentPopup();
                    launchSmartCheckInForRoom(room->id, -1);
                }
            }
            if (!canCheckInNow) {
                drawTooltip(!canCheckInRole ? "Role not permitted to check in guests." : "Only Available or Reserved rooms can check in.");
            }

            const bool canCheckOutRole = HotelSystem::can(role, Action::CheckOutGuest);
            const bool canCheckOutNow = canCheckOutRole && (room->status == RoomStatus::Occupied) && (activeBooking != nullptr);
            if (primaryButton("Check-Out", ImVec2(-1.0f, 34.0f), canCheckOutNow)) {
                m_matrixState.showCheckoutForm = !m_matrixState.showCheckoutForm;
                m_matrixState.showSwapPicker = false;
                m_matrixState.showMaintForm = false;
                m_matrixState.showChargeForm = false;
            }
            if (!canCheckOutNow) {
                drawTooltip(!canCheckOutRole ? "Role not permitted to check out guests." : "Room must be Occupied to check out.");
            }

            const bool canSwapRole = HotelSystem::can(role, Action::RoomSwap);
            const bool canSwapNow = canSwapRole && (activeBooking != nullptr);
            if (ghostButton("Room Swap", ImVec2(-1.0f, 34.0f), canSwapNow)) {
                m_matrixState.showSwapPicker = !m_matrixState.showSwapPicker;
                m_matrixState.showCheckoutForm = false;
                m_matrixState.showMaintForm = false;
                m_matrixState.showChargeForm = false;
            }
            if (!canSwapNow) {
                drawTooltip(!canSwapRole ? "Role not permitted to swap rooms." : "Requires an active booking.");
            }

            const bool canHk = HotelSystem::can(role, Action::ManageHousekeeping);
            if (ghostButton("Dispatch Housekeeping", ImVec2(-1.0f, 34.0f), canHk)) {
                m_system.dispatchHousekeepingTask(room->id, TaskType::Cleaning, 0, "Housekeeping Team", "Dispatched from Room Matrix");
                Toast::push(NoticeType::Info, "Housekeeping Dispatched", "Cleaning task queued for Room " + std::to_string(room->number));
            }
            if (!canHk) {
                drawTooltip("Role not permitted to dispatch housekeeping.");
            }

            if (dangerButton("Flag Maintenance", ImVec2(-1.0f, 34.0f), canHk)) {
                m_matrixState.showMaintForm = !m_matrixState.showMaintForm;
                m_matrixState.showCheckoutForm = false;
                m_matrixState.showSwapPicker = false;
                m_matrixState.showChargeForm = false;
            }

            const bool canCharge = HotelSystem::can(role, Action::AddServiceCharge) && (activeBooking != nullptr);
            if (ghostButton("Add Service Charge", ImVec2(-1.0f, 34.0f), canCharge)) {
                m_matrixState.showChargeForm = !m_matrixState.showChargeForm;
                m_matrixState.showCheckoutForm = false;
                m_matrixState.showSwapPicker = false;
                m_matrixState.showMaintForm = false;
            }
            if (!canCharge) {
                drawTooltip("Requires an active guest booking and Front Desk / Admin role.");
            }

            // Quick Mark Available for Housekeeping/Admin when CleaningRequired or Maintenance
            if (HotelSystem::can(role, Action::UpdateRoomStatus) &&
                (room->status == RoomStatus::CleaningRequired || room->status == RoomStatus::Maintenance)) {
                ImGui::Spacing();
                if (primaryButton("Mark Available", ImVec2(-1.0f, 34.0f))) {
                    m_system.setRoomStatus(room->id, RoomStatus::Available, true);
                }
            }

            ImGui::EndTable();
        }

        // Expandable Sub-Panels at bottom of Room Modal
        ImGui::Spacing();
        ImGui::Separator();

        if (m_matrixState.showCheckoutForm && activeBooking != nullptr) {
            const char* payMethods[] = {"Cash", "Card", "Bank Transfer"};
            ImGui::TextColored(pal.statusAvailable, "Confirm Check-Out & Settlement");
            ImGui::SetNextItemWidth(180.0f);
            ImGui::Combo("Payment Method", &m_matrixState.checkoutPayMethodIdx, payMethods, 3);
            ImGui::SameLine();
            if (primaryButton("Complete Check-Out", ImVec2(180.0f, 34.0f))) {
                const CheckOutResult co = m_system.checkOutBooking(
                    activeBooking->id,
                    DateUtil::currentMinuteOfDay(),
                    payMethods[m_matrixState.checkoutPayMethodIdx]
                );
                if (co.success) {
                    if (co.loyaltyPromoted) {
                        triggerLoyaltyConfetti();
                    }
                    m_matrixState.showCheckoutForm = false;
                    ImGui::CloseCurrentPopup();
                }
            }
        } else if (m_matrixState.showMaintForm) {
            ImGui::TextColored(pal.statusOccupied, "Flag Maintenance Task (Min-Heap Priority)");
            ImGui::SetNextItemWidth(180.0f);
            ImGui::SliderInt("Severity (1-5)", &m_matrixState.maintSeverity, 1, 5);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(260.0f);
            ImGui::InputTextWithHint("##MaintNote", "Issue description (e.g. AC leak)...", m_matrixState.maintNoteBuf, sizeof(m_matrixState.maintNoteBuf));
            ImGui::SameLine();
            if (dangerButton("Submit", ImVec2(100.0f, 34.0f))) {
                const std::string note = m_matrixState.maintNoteBuf[0] ? m_matrixState.maintNoteBuf : "Maintenance inspection required";
                m_system.dispatchHousekeepingTask(room->id, TaskType::Maintenance, m_matrixState.maintSeverity, "Engineering", note);
                m_matrixState.maintNoteBuf[0] = '\0';
                m_matrixState.showMaintForm = false;
            }
        } else if (m_matrixState.showChargeForm && activeBooking != nullptr) {
            const char* addOnNames[] = {"Minibar", "Laundry", "Dining", "Spa", "Transport"};
            ImGui::TextColored(pal.accent, "Post Service Charge to Folio");
            ImGui::SetNextItemWidth(115.0f);
            ImGui::Combo("##ChgType", &m_matrixState.chargeTypeIdx, addOnNames, 5);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(175.0f);
            ImGui::InputTextWithHint("##ChgDesc", "Item description...", m_matrixState.chargeDescBuf, sizeof(m_matrixState.chargeDescBuf));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(95.0f);
            ImGui::InputFloat("PKR##ChgP", &m_matrixState.chargeUnitPrice, 0.0f, 0.0f, "%.0f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70.0f);
            ImGui::InputInt("Qty##ChgQ", &m_matrixState.chargeQty);
            m_matrixState.chargeQty = std::max(1, m_matrixState.chargeQty);
            ImGui::SameLine();
            if (primaryButton("Post", ImVec2(76.0f, 34.0f))) {
                AddOn a{};
                a.type        = static_cast<AddOnType>(m_matrixState.chargeTypeIdx);
                a.description = m_matrixState.chargeDescBuf[0] ? m_matrixState.chargeDescBuf : addOnNames[m_matrixState.chargeTypeIdx];
                a.unitPrice   = std::max(0.0, static_cast<double>(m_matrixState.chargeUnitPrice));
                a.quantity    = m_matrixState.chargeQty;
                a.dayAdded    = DateUtil::todayDayNumber();
                if (m_system.addChargeToBooking(activeBooking->id, a, true)) {
                    m_matrixState.chargeDescBuf[0] = '\0';
                    m_matrixState.showChargeForm = false;
                }
            }
        } else if (m_matrixState.showSwapPicker && activeBooking != nullptr && guest != nullptr) {
            ImGui::TextColored(pal.secondaryAccent, "Recommended Rooms for Swap (Custom MergeSort Ranked)");
            GuestPreferences prefs{};
            prefs.budget = room->baseRate * yield.surge * 1.25;
            prefs.anyView = false;
            prefs.view = room->view;
            prefs.anyBed = false;
            prefs.bed = room->bed;

            const std::vector<RoomMatch> matches = recommendRooms(
                m_system.db().rooms,
                m_system.db().bookings,
                activeBooking->checkInDay,
                activeBooking->checkOutDay,
                prefs,
                guest->tier,
                yield.surge,
                m_system.recommenderWeights()
            );

            if (matches.empty()) {
                ImGui::TextColored(pal.dimText, "No alternative available rooms for these dates.");
            } else {
                for (const RoomMatch& m : matches) {
                    const Room* cand = m_system.findRoomById(m.roomId);
                    if (cand == nullptr || cand->id == room->id) {
                        continue;
                    }
                    char btnLbl[128]{};
                    std::snprintf(btnLbl, sizeof(btnLbl), "Swap to Room %d (%d%% Match) — %s", cand->number, m.percent, m.reasonText.c_str());
                    if (ghostButton(btnLbl, ImVec2(-1.0f, 32.0f))) {
                        if (m_system.swapBookingRoom(activeBooking->id, cand->id, true)) {
                            m_matrixState.selectedRoomId = cand->id;
                            m_matrixState.showSwapPicker = false;
                        }
                    }
                }
            }
        }

        ImGui::EndPopup();
    }
}

// =============================================================================
// 4. 4-STEP SMART CHECK-IN WIZARD (Section 9.3 & Section 9.7)
// =============================================================================
/*
 * Purpose: Renders the 4-step Smart Check-In Wizard for Receptionist & Admin:
 *          Step 1: Find or Register Guest (fuzzy Levenshtein search + CNIC binary search + validated [New Guest] form)
 *          Step 2: Stay Details (check-in/out dates, adults/children, promo code, walk-in vs reservation)
 *          Step 3: Guest Preferences & Top-3 Smart Room Recommender cards with percent-match badge,
 *                  6-feature mini bar breakdown, and [Add to Waitlist] fallback if no rooms available
 *          Step 4: Review folio estimate and confirm check-in / booking.
 * Inputs: None (operates on m_wizardState).
 * Outputs: Creates booking/check-in or waitlist entry through HotelSystem.
 * Big-O: O(G log G) in Step 1; O(R log R) in Step 3 recommender.
 */
void GuiRenderer::renderSmartCheckInScreen() {
    const ThemePalette& pal = m_theme.getActivePalette();
    const HotelDatabase& db = m_system.db();
    const int32_t today = DateUtil::todayDayNumber();
    const YieldPricingResult yield = m_system.getTodayYieldPricing();

    drawSectionHeader(
        "Smart Check-In Wizard",
        "4-step guided intake powered by Binary Search CNIC lookup, Custom Hash Table & Multi-Criteria Room Recommender"
    );

    // Horizontal 4-Step Stepper Indicator
    const char* stepLabels[4] = {"1. Guest Profile", "2. Stay Details", "3. Smart Match", "4. Review & Confirm"};
    if (beginCard("##WizardStepper", nullptr, ImVec2(0.0f, 68.0f))) {
        if (ImGui::BeginTable("##StepBar", 4, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextRow();
            for (int s = 0; s < 4; ++s) {
                ImGui::TableSetColumnIndex(s);
                const bool isCurrent = (m_wizardState.step == s);
                const bool isDone    = (m_wizardState.step > s);
                const ImVec4 col = isCurrent ? pal.accent : (isDone ? pal.statusAvailable : pal.surface);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(col.x, col.y, col.z, isCurrent ? 0.90f : 0.35f));
                ImGui::PushStyleColor(ImGuiCol_Text, (isCurrent || isDone) ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : pal.dimText);
                if (ImGui::Button(stepLabels[s], ImVec2(-1.0f, 34.0f))) {
                    if (s <= m_wizardState.step || (s == 1 && m_wizardState.selectedGuestId != -1)) {
                        m_wizardState.step = s;
                    }
                }
                ImGui::PopStyleColor(2);
            }
            ImGui::EndTable();
        }
    }
    endCard();
    ImGui::Spacing();

    const int32_t reqIn  = today + m_wizardState.checkInOffsetDays;
    const int32_t reqOut = reqIn + std::max(1, m_wizardState.nights);
    const char* promoOptions[4] = {"None", "WELCOME10", "SAVE500", "STAY15"};

    if (beginCard("##WizardBody", stepLabels[m_wizardState.step], ImVec2(0.0f, 0.0f))) {
        // ---------------------------------------------------------------------
        // STEP 1: Find or Register Guest (Fuzzy + Binary Search CNIC + Inline Form)
        // ---------------------------------------------------------------------
        if (m_wizardState.step == 0) {
            ImGui::SetNextItemWidth(280.0f);
            ImGui::InputTextWithHint("##WizFuzzy", "Fuzzy search guest by name / phone...", m_wizardState.guestSearchBuf, sizeof(m_wizardState.guestSearchBuf));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(210.0f);
            ImGui::InputTextWithHint("##WizCnicBin", "Exact CNIC (Binary Search)...", m_wizardState.cnicExactBuf, sizeof(m_wizardState.cnicExactBuf));
            ImGui::SameLine();
            if (ghostButton("Binary Search CNIC", ImVec2(165.0f, 34.0f))) {
                const Guest* found = m_system.findGuestByCnicBinarySearch(m_wizardState.cnicExactBuf);
                if (found != nullptr) {
                    m_wizardState.selectedGuestId = found->id;
                    Toast::push(NoticeType::Success, "CNIC Binary Search Hit", "Selected " + found->fullName);
                } else {
                    Toast::push(NoticeType::Warning, "CNIC Not Found", "No guest matches CNIC: " + std::string(m_wizardState.cnicExactBuf));
                }
            }
            ImGui::SameLine();
            if (primaryButton(m_wizardState.showNewGuestForm ? "Hide New Guest Form" : "+ New Guest", ImVec2(170.0f, 34.0f))) {
                m_wizardState.showNewGuestForm = !m_wizardState.showNewGuestForm;
            }

            ImGui::Spacing();

            if (m_wizardState.showNewGuestForm) {
                ImGui::BeginChild("##InlineNewGuest", ImVec2(0.0f, 270.0f), ImGuiChildFlags_Borders);
                ImGui::TextColored(pal.accent, "Register New Guest (Hand-Written Character Validation + CNIC Hash Table)");
                ImGui::Separator();

                ImGui::Checkbox("Foreign National (Use Passport instead of CNIC)", &m_wizardState.newIsForeign);

                std::string nameErr, idErr, phoneErr, emailErr;
                const bool nameOk = validatePersonName(m_wizardState.newNameBuf, &nameErr);
                bool idOk = false;
                bool phoneOk = true;
                if (!m_wizardState.newIsForeign) {
                    idOk = validateCnic(m_wizardState.newCnicBuf, &idErr);
                    if (idOk && m_system.isDuplicateCnic(m_wizardState.newCnicBuf)) {
                        idOk = false;
                        idErr = "Duplicate CNIC detected via Custom Hash Table!";
                    }
                    phoneOk = validatePakistaniPhone(m_wizardState.newPhoneBuf, nullptr, &phoneErr);
                } else {
                    idOk = validatePassport(m_wizardState.newPassportBuf, &idErr);
                }
                const bool emailOk = (m_wizardState.newEmailBuf[0] == '\0') || validateEmail(m_wizardState.newEmailBuf, &emailErr);

                if (ImGui::BeginTable("##NewGuestGrid", 2, ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::InputTextWithHint("Full Name", "e.g. Bilal Ahmed Khan", m_wizardState.newNameBuf, sizeof(m_wizardState.newNameBuf));
                    if (!nameOk) { ImGui::TextColored(pal.statusOccupied, "%s", nameErr.c_str()); }

                    if (!m_wizardState.newIsForeign) {
                        ImGui::InputTextWithHint("CNIC", "XXXXX-XXXXXXX-X", m_wizardState.newCnicBuf, sizeof(m_wizardState.newCnicBuf));
                    } else {
                        ImGui::InputTextWithHint("Passport", "AB1234567", m_wizardState.newPassportBuf, sizeof(m_wizardState.newPassportBuf));
                    }
                    if (!idOk) { ImGui::TextColored(pal.statusOccupied, "%s", idErr.c_str()); }

                    ImGui::InputTextWithHint("Address", "Sector H-12, Islamabad", m_wizardState.newAddressBuf, sizeof(m_wizardState.newAddressBuf));

                    ImGui::TableSetColumnIndex(1);
                    ImGui::InputTextWithHint("Phone", "03XX-XXXXXXX or +92 3XX XXXXXXX", m_wizardState.newPhoneBuf, sizeof(m_wizardState.newPhoneBuf));
                    if (!phoneOk) { ImGui::TextColored(pal.statusOccupied, "%s", phoneErr.c_str()); }

                    ImGui::InputTextWithHint("Email", "name@example.com", m_wizardState.newEmailBuf, sizeof(m_wizardState.newEmailBuf));
                    if (!emailOk) { ImGui::TextColored(pal.statusOccupied, "%s", emailErr.c_str()); }

                    ImGui::InputTextWithHint("Nationality", "Pakistan", m_wizardState.newNationalityBuf, sizeof(m_wizardState.newNationalityBuf));
                    ImGui::EndTable();
                }

                const bool allValid = nameOk && idOk && phoneOk && emailOk;
                if (primaryButton("Save & Select Guest", ImVec2(200.0f, 36.0f), allValid)) {
                    Guest ng{};
                    ng.fullName    = m_wizardState.newNameBuf;
                    ng.isForeign   = m_wizardState.newIsForeign;
                    ng.cnic        = ng.isForeign ? "" : m_wizardState.newCnicBuf;
                    ng.passport    = ng.isForeign ? m_wizardState.newPassportBuf : "";
                    ng.phone       = m_wizardState.newPhoneBuf;
                    ng.email       = m_wizardState.newEmailBuf;
                    ng.nationality = m_wizardState.newNationalityBuf[0] ? m_wizardState.newNationalityBuf : "Pakistan";
                    ng.address     = m_wizardState.newAddressBuf;
                    std::string regErr;
                    const int newId = m_system.registerGuest(ng, &regErr);
                    if (newId > 0) {
                        m_wizardState.selectedGuestId = newId;
                        m_wizardState.showNewGuestForm = false;
                    } else if (!regErr.empty()) {
                        Toast::push(NoticeType::Error, "Registration Failed", regErr);
                    }
                }
                ImGui::EndChild();
                ImGui::Spacing();
            }

            // Rank existing guests by fuzzy search score
            struct ScoredGuest { int idx; int score; };
            std::vector<ScoredGuest> list;
            for (std::size_t i = 0U; i < db.guests.size(); ++i) {
                const Guest& g = db.guests[i];
                const int sc = (m_wizardState.guestSearchBuf[0] == '\0')
                                   ? 100
                                   : std::max(
                                         computeFuzzyRankScore(m_wizardState.guestSearchBuf, g.fullName),
                                         computeFuzzyRankScore(m_wizardState.guestSearchBuf, g.cnic)
                                     );
                if (m_wizardState.guestSearchBuf[0] == '\0' || sc >= 110) {
                    list.push_back(ScoredGuest{static_cast<int>(i), sc});
                }
            }
            if (m_wizardState.guestSearchBuf[0] != '\0') {
                mergeSort(list, [](const ScoredGuest& a, const ScoredGuest& b) { return a.score > b.score; }, nullptr, nullptr, "Wizard Guest Fuzzy MergeSort");
            }

            if (list.empty()) {
                drawEmptyState("No Matching Guests", "Click '+ New Guest' above to register a walk-in guest.");
            } else if (ImGui::BeginTable("##WizGuestTable", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 240.0f))) {
                ImGui::TableSetupColumn("Select", ImGuiTableColumnFlags_WidthFixed, 85.0f);
                ImGui::TableSetupColumn("Full Name");
                ImGui::TableSetupColumn("CNIC / Passport");
                ImGui::TableSetupColumn("Phone");
                ImGui::TableSetupColumn("Tier", ImGuiTableColumnFlags_WidthFixed, 95.0f);
                ImGui::TableHeadersRow();
                for (const ScoredGuest& sg : list) {
                    const Guest& g = db.guests[static_cast<std::size_t>(sg.idx)];
                    const bool isSel = (m_wizardState.selectedGuestId == g.id);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    char selId[32]{};
                    std::snprintf(selId, sizeof(selId), "%s##SelG%d", isSel ? "Selected" : "Select", g.id);
                    if (isSel ? primaryButton(selId, ImVec2(76.0f, 26.0f)) : ghostButton(selId, ImVec2(76.0f, 26.0f))) {
                        m_wizardState.selectedGuestId = g.id;
                    }
                    ImGui::TableSetColumnIndex(1); ImGui::Text("%s", g.fullName.c_str());
                    ImGui::TableSetColumnIndex(2); ImGui::Text("%s", g.isForeign ? g.passport.c_str() : g.cnic.c_str());
                    ImGui::TableSetColumnIndex(3); ImGui::Text("%s", g.phone.c_str());
                    ImGui::TableSetColumnIndex(4); drawBadge(toString(g.tier), tierColor(g.tier));
                }
                ImGui::EndTable();
            }

            ImGui::Spacing();
            if (primaryButton("Next: Stay Details ->", ImVec2(210.0f, 38.0f), m_wizardState.selectedGuestId != -1)) {
                m_wizardState.step = 1;
            }
        }

        // ---------------------------------------------------------------------
        // STEP 2: Stay Details (Dates, Occupancy, Promo Code, Walk-In Toggle)
        // ---------------------------------------------------------------------
        else if (m_wizardState.step == 1) {
            const Guest* g = m_system.findGuestById(m_wizardState.selectedGuestId);
            if (g != nullptr) {
                ImGui::TextColored(pal.accent, "Guest: %s", g->fullName.c_str());
                ImGui::SameLine();
                drawBadge(toString(g->tier), tierColor(g->tier));
            }
            ImGui::Spacing();

            ImGui::SetNextItemWidth(220.0f);
            ImGui::SliderInt("Check-In Offset (days from today)", &m_wizardState.checkInOffsetDays, 0, 30);
            ImGui::SameLine();
            ImGui::TextColored(pal.secondaryAccent, "  Check-In: %s", DateUtil::formatDate(reqIn).c_str());

            ImGui::SetNextItemWidth(220.0f);
            ImGui::SliderInt("Length of Stay (Nights)", &m_wizardState.nights, 1, 21);
            ImGui::SameLine();
            ImGui::TextColored(pal.secondaryAccent, "  Check-Out: %s", DateUtil::formatDate(reqOut).c_str());

            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputInt("Adults", &m_wizardState.adults);
            m_wizardState.adults = std::clamp(m_wizardState.adults, 1, 6);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputInt("Children", &m_wizardState.children);
            m_wizardState.children = std::clamp(m_wizardState.children, 0, 6);

            ImGui::SetNextItemWidth(220.0f);
            ImGui::Combo("Promo Code", &m_wizardState.promoIndex, promoOptions, 4);
            if (m_wizardState.promoIndex == 3 && m_wizardState.nights < 3) {
                ImGui::SameLine();
                ImGui::TextColored(pal.statusReserved, "STAY15 requires 3+ nights.");
            }

            if (m_wizardState.checkInOffsetDays == 0) {
                ImGui::Checkbox("Immediate Walk-In Check-In (Mark Room Occupied right away)", &m_wizardState.immediateCheckIn);
            } else {
                m_wizardState.immediateCheckIn = false;
                ImGui::TextColored(pal.dimText, "Future check-in date will create a Reserved booking.");
            }

            ImGui::Spacing();
            if (ghostButton("<- Back", ImVec2(110.0f, 38.0f))) {
                m_wizardState.step = 0;
            }
            ImGui::SameLine();
            if (primaryButton("Next: Preferences & Recommendations ->", ImVec2(300.0f, 38.0f))) {
                m_wizardState.step = 2;
            }
        }

        // ---------------------------------------------------------------------
        // STEP 3: Preferences Form & Top-3 Smart Room Recommendation Cards
        // ---------------------------------------------------------------------
        else if (m_wizardState.step == 2) {
            const Guest* g = m_system.findGuestById(m_wizardState.selectedGuestId);
            const LoyaltyTier tier = g ? g->tier : LoyaltyTier::Standard;

            // Preferences bar
            if (ImGui::BeginTable("##PrefGrid", 3, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                float budgetF = static_cast<float>(m_wizardState.prefs.budget);
                if (ImGui::SliderFloat("Nightly Budget (PKR)", &budgetF, 8000.0f, 55000.0f, "PKR %.0f")) {
                    m_wizardState.prefs.budget = static_cast<double>(budgetF);
                }
                const char* floorPrefItems[] = {"No Preference", "Prefer Low Floor", "Prefer High Floor"};
                int floorIdx = m_wizardState.prefs.noFloorPref ? 0 : (m_wizardState.prefs.wantsLowFloor ? 1 : 2);
                if (ImGui::Combo("Floor Pref", &floorIdx, floorPrefItems, 3)) {
                    m_wizardState.prefs.noFloorPref = (floorIdx == 0);
                    m_wizardState.prefs.wantsLowFloor = (floorIdx == 1);
                }

                ImGui::TableSetColumnIndex(1);
                const char* viewItems[] = {"Any View", "City View", "Garden View", "Pool View"};
                int viewIdx = m_wizardState.prefs.anyView ? 0 : (static_cast<int>(m_wizardState.prefs.view) + 1);
                if (ImGui::Combo("View Pref", &viewIdx, viewItems, 4)) {
                    m_wizardState.prefs.anyView = (viewIdx == 0);
                    if (viewIdx > 0) { m_wizardState.prefs.view = static_cast<ViewType>(viewIdx - 1); }
                }
                const char* bedItems[] = {"Any Bed", "Single", "Double", "Twin", "King"};
                int bedIdx = m_wizardState.prefs.anyBed ? 0 : (static_cast<int>(m_wizardState.prefs.bed) + 1);
                if (ImGui::Combo("Bed Pref", &bedIdx, bedItems, 5)) {
                    m_wizardState.prefs.anyBed = (bedIdx == 0);
                    if (bedIdx > 0) { m_wizardState.prefs.bed = static_cast<BedConfig>(bedIdx - 1); }
                }

                ImGui::TableSetColumnIndex(2);
                const char* elevItems[] = {"Near Elevator", "Any Distance", "Far from Elevator"};
                ImGui::Combo("Elevator Pref", &m_wizardState.prefs.elevatorPref, elevItems, 3);
                ImGui::SliderInt("Noise Tolerance (1 quiet..5)", &m_wizardState.prefs.noiseTolerance, 1, 5);
                ImGui::EndTable();
            }

            ImGui::Separator();
            ImGui::Spacing();

            // Run Multi-Criteria Smart Room Recommender live
            m_wizardState.rankedBreakdowns.clear();
            const std::vector<RoomMatch> top3 = recommendRooms(
                db.rooms,
                db.bookings,
                reqIn,
                reqOut,
                m_wizardState.prefs,
                tier,
                yield.surge,
                m_system.recommenderWeights(),
                &m_wizardState.rankedBreakdowns,
                &m_wizardState.stlCrossCheckOk
            );

            if (top3.empty()) {
                drawEmptyState("No Available Rooms for Selected Dates", "You can place this guest on the FIFO Waitlist Queue.");
                ImGui::Spacing();
                if (primaryButton("Add to Waitlist", ImVec2(200.0f, 38.0f))) {
                    const RoomType wanted = (m_wizardState.prefs.budget >= 35000.0)
                                                ? RoomType::Presidential
                                                : ((m_wizardState.prefs.budget >= 20000.0)
                                                       ? RoomType::Suite
                                                       : ((m_wizardState.prefs.budget >= 12000.0) ? RoomType::Deluxe : RoomType::Standard));
                    if (m_system.addToWaitlist(m_wizardState.selectedGuestId, wanted, reqIn, reqOut, m_wizardState.prefs.budget) > 0) {
                        navigateTo(ScreenId::Dashboard);
                    }
                }
            } else {
                ImGui::TextColored(
                    pal.secondaryAccent,
                    "Top-3 Smart Recommendations (Custom MergeSort · std::sort Cross-Check: %s)",
                    m_wizardState.stlCrossCheckOk ? "VERIFIED" : "MISMATCH"
                );
                ImGui::Spacing();

                if (ImGui::BeginTable("##Top3RecCards", 3, ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextRow();
                    for (std::size_t i = 0U; i < top3.size(); ++i) {
                        ImGui::TableSetColumnIndex(static_cast<int>(i));
                        const RoomFeatureBreakdown& bd = m_wizardState.rankedBreakdowns[i];
                        const Room* r = m_system.findRoomById(bd.roomId);
                        if (r == nullptr) {
                            continue;
                        }
                        const bool isSel = (m_wizardState.selectedRoomId == r->id);
                        char cardId[32]{};
                        std::snprintf(cardId, sizeof(cardId), "##RecCard_%zu", i);

                        if (beginCard(cardId, nullptr, ImVec2(0.0f, 278.0f))) {
                            char matchBadge[32]{};
                            std::snprintf(matchBadge, sizeof(matchBadge), "%d%% Match", bd.percent);
                            ImGui::TextColored(pal.text, "#%zu  Room %d (%s)", i + 1U, r->number, toString(r->type));
                            ImGui::SameLine();
                            drawBadge(matchBadge, (bd.percent >= 85) ? pal.statusAvailable : pal.accent);

                            ImGui::TextColored(
                                pal.goldHighlight,
                                "%s / night",
                                formatPKR(roundToNearest10PKR(r->baseRate * yield.surge)).c_str()
                            );
                            if (m_fonts.captionFont != nullptr) { ImGui::PushFont(m_fonts.captionFont); }
                            ImGui::TextWrapped("%s", bd.reasonText.c_str());
                            if (m_fonts.captionFont != nullptr) { ImGui::PopFont(); }
                            ImGui::Separator();

                            drawFeatureMiniBar("Budget", bd.fBudget, pal.statusAvailable, pal);
                            drawFeatureMiniBar("Floor", bd.fFloor, pal.accent, pal);
                            drawFeatureMiniBar("View", bd.fView, pal.secondaryAccent, pal);
                            drawFeatureMiniBar("Elev", bd.fElevator, pal.goldHighlight, pal);
                            drawFeatureMiniBar("Noise", bd.fNoise, pal.statusCleaning, pal);
                            drawFeatureMiniBar("Bed", bd.fBed, pal.tierPlatinum, pal);

                            ImGui::Spacing();
                            char pickLbl[32]{};
                            std::snprintf(pickLbl, sizeof(pickLbl), "%s##PickRec%d", isSel ? "Selected Room" : "Select Room", r->id);
                            if (isSel ? primaryButton(pickLbl, ImVec2(-1.0f, 32.0f)) : ghostButton(pickLbl, ImVec2(-1.0f, 32.0f))) {
                                m_wizardState.selectedRoomId = r->id;
                            }
                        }
                        endCard();
                    }
                    ImGui::EndTable();
                }
            }

            ImGui::Spacing();
            if (ghostButton("<- Back", ImVec2(110.0f, 38.0f))) {
                m_wizardState.step = 1;
            }
            ImGui::SameLine();
            if (primaryButton("Next: Review & Confirm ->", ImVec2(230.0f, 38.0f), m_wizardState.selectedRoomId != -1)) {
                m_wizardState.step = 3;
            }
        }

        // ---------------------------------------------------------------------
        // STEP 4: Review & Confirm
        // ---------------------------------------------------------------------
        else {
            const Guest* g = m_system.findGuestById(m_wizardState.selectedGuestId);
            const Room* r = m_system.findRoomById(m_wizardState.selectedRoomId);
            if (g == nullptr || r == nullptr) {
                drawEmptyState("Incomplete Selection", "Please select both a guest and a room.");
            } else {
                const std::string promoStr = (m_wizardState.promoIndex == 0) ? "" : promoOptions[m_wizardState.promoIndex];
                Booking preview{};
                preview.guestId         = g->id;
                preview.roomId          = r->id;
                preview.checkInDay      = reqIn;
                preview.checkOutDay     = reqOut;
                preview.quotedRate      = r->baseRate;
                preview.surgeMultiplier = yield.surge;
                preview.promoCode       = promoStr;
                const FolioBreakdown est = m_system.calculateFolio(preview, *g, reqOut, 720);

                ImGui::TextColored(pal.accent, "Confirmation Summary");
                ImGui::Separator();
                ImGui::Text("Guest           : %s (%s)", g->fullName.c_str(), toString(g->tier));
                ImGui::Text("Room            : Room %d — %s (%s View, %s Bed)", r->number, toString(r->type), toString(r->view), toString(r->bed));
                ImGui::Text("Stay Dates      : %s to %s (%d nights)", DateUtil::formatDate(reqIn).c_str(), DateUtil::formatDate(reqOut).c_str(), est.nights);
                ImGui::Text("Occupancy       : %d Adults, %d Children", m_wizardState.adults, m_wizardState.children);
                ImGui::Text("Mode            : %s", m_wizardState.immediateCheckIn ? "Immediate Walk-In Check-In" : "Confirmed Reservation");
                ImGui::Separator();
                ImGui::Text("Room Subtotal   : %s (Surge x%.2f locked)", formatPKR(est.roomSubtotal).c_str(), yield.surge);
                if (est.loyaltyDiscount > 0.0) {
                    ImGui::TextColored(pal.statusAvailable, "Loyalty Discount: -%s", formatPKR(est.loyaltyDiscount).c_str());
                }
                if (est.promoDiscount > 0.0) {
                    ImGui::TextColored(pal.statusAvailable, "Promo (%s): -%s", est.appliedPromoCode.c_str(), formatPKR(est.promoDiscount).c_str());
                }
                ImGui::Text("GST (%.0f%%)       : %s", est.taxRate * 100.0, formatPKR(est.taxAmount).c_str());
                ImGui::TextColored(pal.goldHighlight, "ESTIMATED TOTAL : %s", formatPKR(est.grandTotal).c_str());

                ImGui::Spacing();
                if (ghostButton("<- Back", ImVec2(110.0f, 38.0f))) {
                    m_wizardState.step = 2;
                }
                ImGui::SameLine();
                const char* confirmLabel = m_wizardState.immediateCheckIn ? "Confirm & Check In Now" : "Confirm Reservation";
                if (primaryButton(confirmLabel, ImVec2(240.0f, 38.0f))) {
                    std::string err;
                    const int newBookingId = m_system.createBooking(
                        g->id,
                        r->id,
                        reqIn,
                        reqOut,
                        m_wizardState.adults,
                        m_wizardState.children,
                        promoStr,
                        m_wizardState.immediateCheckIn,
                        &err
                    );
                    if (newBookingId > 0) {
                        m_wizardState = SmartCheckInState{};
                        navigateTo(ScreenId::RoomMatrix);
                    } else if (!err.empty()) {
                        Toast::push(NoticeType::Error, "Booking Rejected", err);
                    }
                }
            }
        }
    }
    endCard();
}

} // namespace hms
