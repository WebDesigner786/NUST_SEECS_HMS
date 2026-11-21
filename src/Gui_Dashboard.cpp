#include "GuiRenderer.h"
#include "implot.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace hms {

namespace {

constexpr float kPiDash = 3.1415926535f;

} // namespace

/*
 * Purpose: Routes the Dashboard screen to the role-specific dashboard (Admin, Receptionist, or Housekeeping).
 * Inputs: None (reads m_system.currentRole()).
 * Outputs: Renders the active role's dashboard.
 * Big-O: O(R + B + S) where R=rooms, B=bookings, S=stats.
 */
void GuiRenderer::renderDashboardScreen() {
    const Role role = m_system.currentRole();
    if (role == Role::Admin) {
        renderAdminDashboard();
    } else if (role == Role::Receptionist) {
        renderReceptionistDashboard();
    } else {
        renderHousekeepingDashboard();
    }
}

/*
 * Purpose: Draws the custom ImDrawList multi-arc Donut Chart for "Room status mix" (Section 9.1).
 * Inputs: statusCounts[5] (Available, Occupied, Reserved, Cleaning, Maintenance), totalRooms.
 * Outputs: Renders donut ring with centered room count and status legend.
 * Big-O: O(1) time (fixed 5 slices), O(1) space.
 */
void GuiRenderer::drawRoomStatusDonutChart(const int statusCounts[5], int totalRooms) {
    const ThemePalette& pal = m_theme.getActivePalette();
    const ImVec4 sliceColors[5] = {
        pal.statusAvailable,
        pal.statusOccupied,
        pal.statusReserved,
        pal.statusCleaning,
        pal.statusMaintenance
    };
    const char* sliceLabels[5] = {
        "Available", "Occupied", "Reserved", "Cleaning", "Maintenance"
    };

    const float denom = static_cast<float>(std::max(1, totalRooms));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float radius = 64.0f;
    const float thickness = 20.0f;
    const ImVec2 center(origin.x + radius + 16.0f, origin.y + radius + 16.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircle(center, radius, ThemeEngine::vec4ToU32(pal.surface), 64, thickness);

    float angle = -kPiDash * 0.5f;
    for (int i = 0; i < 5; ++i) {
        if (statusCounts[i] <= 0) {
            continue;
        }
        const float sweep = (static_cast<float>(statusCounts[i]) / denom) * 2.0f * kPiDash;
        const float gap = (statusCounts[i] < totalRooms) ? 0.03f : 0.0f;
        const float a0 = angle + gap * 0.5f;
        const float a1 = angle + std::max(0.01f, sweep - gap * 0.5f);

        dl->PathArcTo(center, radius, a0, a1, 32);
        dl->PathStroke(ThemeEngine::vec4ToU32(sliceColors[i]), 0, thickness);
        angle += sweep;
    }

    char totalBuf[24]{};
    std::snprintf(totalBuf, sizeof(totalBuf), "%d", totalRooms);
    if (m_fonts.headingFont != nullptr) {
        ImGui::PushFont(m_fonts.headingFont);
    }
    const ImVec2 numSz = ImGui::CalcTextSize(totalBuf);
    dl->AddText(ImVec2(center.x - numSz.x * 0.5f, center.y - numSz.y * 0.5f - 6.0f), ThemeEngine::vec4ToU32(pal.text), totalBuf);
    if (m_fonts.headingFont != nullptr) {
        ImGui::PopFont();
    }

    if (m_fonts.captionFont != nullptr) {
        ImGui::PushFont(m_fonts.captionFont);
    }
    const ImVec2 lblSz = ImGui::CalcTextSize("Rooms");
    dl->AddText(ImVec2(center.x - lblSz.x * 0.5f, center.y + 12.0f), ThemeEngine::vec4ToU32(pal.dimText), "Rooms");
    if (m_fonts.captionFont != nullptr) {
        ImGui::PopFont();
    }

    // Legend column on the right of the donut
    ImGui::SetCursorScreenPos(ImVec2(center.x + radius + 34.0f, origin.y + 16.0f));
    ImGui::BeginGroup();
    for (int i = 0; i < 5; ++i) {
        drawStatusDot(sliceColors[i], 5.0f);
        ImGui::SameLine();
        const float pct = (100.0f * static_cast<float>(statusCounts[i])) / denom;
        ImGui::TextColored(pal.text, "%-12s %2d (%.0f%%)", sliceLabels[i], statusCounts[i], pct);
    }
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + radius * 2.0f + 36.0f));
}

/*
 * Purpose: Renders the Admin Dashboard per Section 9.1:
 *          - 4 animated KPI tiles (Occupancy %, Today's Revenue, ADR, RevPAR)
 *          - ImPlot line chart "Revenue — last 30 days" with dashed 7-day linear regression forecast
 *          - ImPlot stacked bar chart "Occupancy by floor"
 *          - Custom ImDrawList donut chart "Room status mix"
 *          - "Today's Arrivals / Departures" lists & Live Activity Feed from the Audit Log.
 * Inputs: None.
 * Outputs: Renders Admin Dashboard screen.
 * Big-O: O(R + B + S + A) time.
 */
void GuiRenderer::renderAdminDashboard() {
    const ThemePalette& pal = m_theme.getActivePalette();
    const HotelDatabase& db = m_system.db();
    const int32_t today = DateUtil::todayDayNumber();
    const YieldPricingResult yield = m_system.getTodayYieldPricing();

    drawSectionHeader(
        "Executive Admin Dashboard",
        "Real-time hotel occupancy, revenue telemetry, floor utilization & tamper-evident activity feed"
    );

    // Compute room status breakdown & floor counts
    int statusCounts[5] = {0, 0, 0, 0, 0};
    double floorOccupied[5] = {0, 0, 0, 0, 0};
    double floorAvailable[5] = {0, 0, 0, 0, 0};
    double floorOther[5] = {0, 0, 0, 0, 0};

    for (const Room& r : db.rooms) {
        const int sIdx = std::clamp(static_cast<int>(r.status), 0, 4);
        statusCounts[sIdx]++;
        const int fIdx = std::clamp(r.floor - 1, 0, 4);
        if (r.status == RoomStatus::Occupied || r.status == RoomStatus::Reserved) {
            floorOccupied[fIdx] += 1.0;
        } else if (r.status == RoomStatus::Available) {
            floorAvailable[fIdx] += 1.0;
        } else {
            floorOther[fIdx] += 1.0;
        }
    }

    const int totalRooms = std::max(1, static_cast<int>(db.rooms.size()));
    const int occupiedCount = statusCounts[static_cast<int>(RoomStatus::Occupied)];
    const double occPct = yield.occupancyRatio * 100.0;

    // Compute Today's Revenue, ADR (revenue / occupied room-nights), RevPAR (revenue / total rooms)
    double todayRevenue = 0.0;
    for (const Invoice& inv : db.invoices) {
        if (inv.issuedDay == today) {
            todayRevenue += inv.grandTotal;
        }
    }
    if (todayRevenue < 1.0) {
        // Fallback to active occupied room nightly run-rate when no invoice has been issued yet today
        for (const Booking& b : db.bookings) {
            if (b.status == BookingStatus::CheckedIn) {
                todayRevenue += roundToNearest10PKR(b.quotedRate * b.surgeMultiplier);
            }
        }
    }
    const double adr    = (occupiedCount > 0) ? (todayRevenue / static_cast<double>(occupiedCount)) : 0.0;
    const double revPar = todayRevenue / static_cast<double>(totalRooms);

    // Gather 30-day revenue history & sparklines
    std::vector<double> revHistory30;
    std::vector<float> revSpark;
    std::vector<float> occSpark;
    if (!db.stats.empty()) {
        const std::size_t start = (db.stats.size() > 30U) ? (db.stats.size() - 30U) : 0U;
        for (std::size_t i = start; i < db.stats.size(); ++i) {
            revHistory30.push_back(db.stats[i].revenue);
            revSpark.push_back(static_cast<float>(db.stats[i].revenue));
            occSpark.push_back(static_cast<float>(db.stats[i].occupancyPct));
        }
    } else {
        // Clean-state baseline sparkline when stats.dat has not been seeded yet
        revHistory30.assign(7U, todayRevenue);
        revSpark.assign(7U, static_cast<float>(todayRevenue));
        occSpark.assign(7U, static_cast<float>(occPct));
    }

    // 4 KPI Tiles Row
    if (ImGui::BeginTable("##AdminKpiRow", 4, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        char surgeChip[32]{};
        std::snprintf(surgeChip, sizeof(surgeChip), "Surge x%.2f", yield.surge);
        drawKpiTile("##KpiOcc", "OCCUPANCY RATE", occPct, "", "%", false, surgeChip, occPct >= 50.0, occSpark, pal.accent);

        ImGui::TableSetColumnIndex(1);
        drawKpiTile("##KpiRev", "TODAY'S REVENUE", todayRevenue, "", "", true, "Live Ledger", true, revSpark, pal.statusAvailable);

        ImGui::TableSetColumnIndex(2);
        drawKpiTile("##KpiAdr", "ADR (AVG DAILY RATE)", adr, "", "", true, "Per Occupied", true, revSpark, pal.secondaryAccent);

        ImGui::TableSetColumnIndex(3);
        drawKpiTile("##KpiRevPar", "RevPAR", revPar, "", "", true, "Per Total Room", true, occSpark, pal.goldHighlight);
        ImGui::EndTable();
    }

    ImGui::Spacing();

    // Charts Row: Revenue 30-day + 7-day forecast (left) & Occupancy by Floor (right)
    if (ImGui::BeginTable("##AdminChartsRow", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (beginCard("##RevChartCard", "Revenue — Last 30 Days + 7-Day Linear Forecast", ImVec2(0.0f, 275.0f))) {
            const LinearRegressionForecast fc = compute7DayForecast(revHistory30, "Dashboard Revenue");
            const int histN = static_cast<int>(revHistory30.size());
            std::vector<double> xsHist(static_cast<std::size_t>(histN));
            for (int i = 0; i < histN; ++i) {
                xsHist[static_cast<std::size_t>(i)] = static_cast<double>(i + 1);
            }

            std::vector<double> xsFc(8U);
            std::vector<double> ysFc(8U);
            xsFc[0] = static_cast<double>(histN);
            ysFc[0] = (histN > 0) ? revHistory30.back() : 0.0;
            for (int f = 0; f < 7; ++f) {
                xsFc[static_cast<std::size_t>(f + 1)] = static_cast<double>(histN + f + 1);
                ysFc[static_cast<std::size_t>(f + 1)] = fc.predicted7Days[static_cast<std::size_t>(f)];
            }

            if (ImPlot::BeginPlot("##RevLinePlot", ImVec2(-1.0f, 210.0f))) {
                ImPlot::SetupAxes("Day", "PKR");
                ImPlot::SetupAxisLimits(ImAxis_X1, 1.0, static_cast<double>(histN + 8), ImPlotCond_Always);
                ImPlot::SetNextLineStyle(pal.accent, 2.4f);
                ImPlot::PlotLine("Actual Revenue", xsHist.data(), revHistory30.data(), histN);

                ImPlot::SetNextLineStyle(pal.goldHighlight, 2.0f);
                ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 3.0f, pal.goldHighlight);
                ImPlot::PlotLine("7-Day Forecast", xsFc.data(), ysFc.data(), 8);
                ImPlot::EndPlot();
            }
        }
        endCard();

        ImGui::TableSetColumnIndex(1);
        if (beginCard("##FloorChartCard", "Occupancy by Floor (Floors 1–5)", ImVec2(0.0f, 275.0f))) {
            const double floorXs[5] = {1.0, 2.0, 3.0, 4.0, 5.0};
            if (ImPlot::BeginPlot("##FloorBarPlot", ImVec2(-1.0f, 210.0f))) {
                ImPlot::SetupAxes("Floor", "Rooms");
                ImPlot::SetupAxisLimits(ImAxis_X1, 0.3, 5.7, ImPlotCond_Always);
                ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 11.0, ImPlotCond_Always);

                ImPlot::SetNextFillStyle(pal.statusOccupied, 0.85f);
                ImPlot::PlotBars("Occupied/Res", floorXs, floorOccupied, 5, 0.24, 0, -0.25);

                ImPlot::SetNextFillStyle(pal.statusAvailable, 0.85f);
                ImPlot::PlotBars("Available", floorXs, floorAvailable, 5, 0.24, 0, 0.0);

                ImPlot::SetNextFillStyle(pal.statusCleaning, 0.85f);
                ImPlot::PlotBars("Clean/Maint", floorXs, floorOther, 5, 0.24, 0, 0.25);
                ImPlot::EndPlot();
            }
        }
        endCard();
        ImGui::EndTable();
    }

    ImGui::Spacing();

    // Bottom Row: Donut Chart + Today's Arrivals/Departures + Live Audit Activity Feed
    if (ImGui::BeginTable("##AdminBottomRow", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (beginCard("##DonutCard", "Room Status Mix", ImVec2(0.0f, 230.0f))) {
            drawRoomStatusDonutChart(statusCounts, totalRooms);
        }
        endCard();

        ImGui::TableSetColumnIndex(1);
        if (beginCard("##ArrivalsDepsCard", "Today's Arrivals & Departures", ImVec2(0.0f, 230.0f))) {
            int arrivals = 0;
            int departures = 0;
            for (const Booking& b : db.bookings) {
                if (b.checkInDay == today && (b.status == BookingStatus::Reserved || b.status == BookingStatus::CheckedIn)) {
                    ++arrivals;
                    const Guest* g = m_system.findGuestById(b.guestId);
                    const Room* r = m_system.findRoomById(b.roomId);
                    drawBadge("ARR", pal.statusAvailable);
                    ImGui::SameLine();
                    ImGui::Text("Rm %d · %s", r ? r->number : 0, g ? g->fullName.c_str() : "Guest");
                }
                if (b.checkOutDay == today && (b.status == BookingStatus::CheckedIn || b.status == BookingStatus::CheckedOut)) {
                    ++departures;
                    const Guest* g = m_system.findGuestById(b.guestId);
                    const Room* r = m_system.findRoomById(b.roomId);
                    drawBadge("DEP", pal.statusReserved);
                    ImGui::SameLine();
                    ImGui::Text("Rm %d · %s", r ? r->number : 0, g ? g->fullName.c_str() : "Guest");
                }
            }
            if (arrivals == 0 && departures == 0) {
                drawEmptyState("No Arrivals or Departures Today", "Create bookings or run Viva Seeder in Phase 8.");
            }
        }
        endCard();

        ImGui::TableSetColumnIndex(2);
        if (beginCard("##ActivityFeedCard", "Live Audit Activity Feed", ImVec2(0.0f, 230.0f))) {
            if (db.auditLog.empty()) {
                drawEmptyState("No Audit Activity", "Actions are automatically hash-chained here.");
            } else {
                const int startIdx = std::max(0, static_cast<int>(db.auditLog.size()) - 7);
                for (int i = static_cast<int>(db.auditLog.size()) - 1; i >= startIdx; --i) {
                    const AuditEntry& e = db.auditLog[static_cast<std::size_t>(i)];
                    drawBadge(e.action.c_str(), pal.accent);
                    ImGui::SameLine();
                    ImGui::TextColored(pal.text, "%s", e.username.c_str());
                    if (m_fonts.captionFont != nullptr) {
                        ImGui::PushFont(m_fonts.captionFont);
                    }
                    ImGui::TextColored(pal.dimText, "  %s", e.details.c_str());
                    if (m_fonts.captionFont != nullptr) {
                        ImGui::PopFont();
                    }
                }
            }
        }
        endCard();
        ImGui::EndTable();
    }
}

/*
 * Purpose: Renders the Receptionist Dashboard per Section 9.1:
 *          availability counter, quick actions (Smart Check-In, Room Matrix, Guests, Waitlist),
 *          and today's arrivals / departures tables.
 * Inputs: None.
 * Outputs: Renders Receptionist Dashboard.
 * Big-O: O(R + B) time.
 */
void GuiRenderer::renderReceptionistDashboard() {
    const ThemePalette& pal = m_theme.getActivePalette();
    const HotelDatabase& db = m_system.db();
    const int32_t today = DateUtil::todayDayNumber();

    drawSectionHeader(
        "Front Desk Reception Dashboard",
        "Today's arrivals, departures, real-time room availability & quick check-in actions"
    );

    int availCount = 0;
    int occupiedCount = 0;
    for (const Room& r : db.rooms) {
        if (r.status == RoomStatus::Available) { ++availCount; }
        else if (r.status == RoomStatus::Occupied) { ++occupiedCount; }
    }

    const std::vector<float> flatSpark = {static_cast<float>(availCount), static_cast<float>(availCount)};

    if (ImGui::BeginTable("##RecepKpis", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        drawKpiTile("##RKpiAvail", "AVAILABLE ROOMS", static_cast<double>(availCount), "", " Rooms", false, "Ready Now", true, flatSpark, pal.statusAvailable);
        ImGui::TableSetColumnIndex(1);
        drawKpiTile("##RKpiOcc", "OCCUPIED ROOMS", static_cast<double>(occupiedCount), "", " Rooms", false, "In-House", true, flatSpark, pal.statusOccupied);
        ImGui::TableSetColumnIndex(2);
        drawKpiTile("##RKpiWait", "WAITLIST QUEUE", static_cast<double>(db.waitlist.size()), "", " Guests", false, "FIFO Queue", true, flatSpark, pal.goldHighlight);
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (beginCard("##RecepQuickActions", "Quick Front Desk Actions", ImVec2(0.0f, 100.0f))) {
        if (primaryButton("Smart Check-In Wizard", ImVec2(210.0f, 40.0f))) {
            navigateTo(ScreenId::SmartCheckIn);
        }
        ImGui::SameLine();
        if (ghostButton("Open Room Matrix", ImVec2(180.0f, 40.0f))) {
            navigateTo(ScreenId::RoomMatrix);
        }
        ImGui::SameLine();
        if (ghostButton("Guest CRM Registry", ImVec2(180.0f, 40.0f))) {
            navigateTo(ScreenId::Guests);
        }
        ImGui::SameLine();
        if (ghostButton("View Waitlist", ImVec2(150.0f, 40.0f))) {
            navigateTo(ScreenId::Waitlist);
        }
    }
    endCard();

    ImGui::Spacing();
    if (ImGui::BeginTable("##RecepLists", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (beginCard("##RecepArrivals", "Today's Expected Arrivals", ImVec2(0.0f, 300.0f))) {
            int count = 0;
            for (const Booking& b : db.bookings) {
                if (b.checkInDay == today && b.status == BookingStatus::Reserved) {
                    ++count;
                    const Guest* g = m_system.findGuestById(b.guestId);
                    const Room* r = m_system.findRoomById(b.roomId);
                    ImGui::Text("Booking #%d · Room %d · %s", b.id, r ? r->number : 0, g ? g->fullName.c_str() : "Guest");
                }
            }
            if (count == 0) {
                drawEmptyState("All Arrivals Checked In", "No pending arrivals remaining for today.");
            }
        }
        endCard();

        ImGui::TableSetColumnIndex(1);
        if (beginCard("##RecepDepartures", "Today's Scheduled Departures", ImVec2(0.0f, 300.0f))) {
            int count = 0;
            for (const Booking& b : db.bookings) {
                if (b.checkOutDay == today && b.status == BookingStatus::CheckedIn) {
                    ++count;
                    const Guest* g = m_system.findGuestById(b.guestId);
                    const Room* r = m_system.findRoomById(b.roomId);
                    ImGui::Text("Booking #%d · Room %d · %s", b.id, r ? r->number : 0, g ? g->fullName.c_str() : "Guest");
                }
            }
            if (count == 0) {
                drawEmptyState("No Pending Departures", "No checked-in guests scheduled to depart today.");
            }
        }
        endCard();
        ImGui::EndTable();
    }
}

/*
 * Purpose: Renders the Housekeeping Dashboard per Section 9.1:
 *          queue length, average wait time, rooms ready today, and top min-heap priority preview.
 * Inputs: None.
 * Outputs: Renders Housekeeping Dashboard.
 * Big-O: O(T log T + R) time.
 */
void GuiRenderer::renderHousekeepingDashboard() {
    const ThemePalette& pal = m_theme.getActivePalette();
    const HotelDatabase& db = m_system.db();
    const std::vector<HousekeepingTask> queueOrder = m_system.getHousekeepingDispatchOrder();

    drawSectionHeader(
        "Housekeeping & Maintenance Dashboard",
        "Binary Min-Heap priority queue status, turnaround times & ready rooms"
    );

    int readyToday = 0;
    for (const Room& r : db.rooms) {
        if (r.status == RoomStatus::Available) {
            ++readyToday;
        }
    }

    const int32_t today = DateUtil::todayDayNumber();
    const int32_t nowMin = DateUtil::currentMinuteOfDay();
    const long long nowTotalMin = static_cast<long long>(today) * 1440LL + static_cast<long long>(nowMin);
    double totalWaitMins = 0.0;
    for (const HousekeepingTask& t : queueOrder) {
        const long long createdTotal = static_cast<long long>(t.createdDay) * 1440LL + static_cast<long long>(t.createdMinute);
        totalWaitMins += std::max(0.0, static_cast<double>(nowTotalMin - createdTotal));
    }
    const double avgWaitMins = queueOrder.empty() ? 0.0 : (totalWaitMins / static_cast<double>(queueOrder.size()));

    const std::vector<float> flatSpark = {static_cast<float>(queueOrder.size()), static_cast<float>(queueOrder.size())};

    if (ImGui::BeginTable("##HkKpis", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        drawKpiTile("##HkQueueLen", "DISPATCH QUEUE LENGTH", static_cast<double>(queueOrder.size()), "", " Tasks", false, "Min-Heap Active", true, flatSpark, pal.statusCleaning);
        ImGui::TableSetColumnIndex(1);
        drawKpiTile("##HkAvgWait", "AVG WAIT TIME", avgWaitMins, "", " min", false, "Live SLA", avgWaitMins < 45.0, flatSpark, pal.statusReserved);
        ImGui::TableSetColumnIndex(2);
        drawKpiTile("##HkReady", "ROOMS READY TODAY", static_cast<double>(readyToday), "", " / 50", false, "Available", true, flatSpark, pal.statusAvailable);
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (beginCard("##HkPriorityPreview", "Top Priority Tasks (Min-Heap Pop Order)", ImVec2(0.0f, 320.0f))) {
        if (queueOrder.empty()) {
            drawEmptyState("Dispatch Queue Empty", "All rooms are clean and operational.");
        } else {
            const std::size_t limit = std::min<std::size_t>(8U, queueOrder.size());
            for (std::size_t i = 0U; i < limit; ++i) {
                const HousekeepingTask& t = queueOrder[i];
                const Room* r = m_system.findRoomById(t.roomId);
                drawBadge(toString(t.type), t.type == TaskType::Maintenance ? pal.statusOccupied : pal.statusCleaning);
                ImGui::SameLine();
                ImGui::Text("Room %d · Priority P=%.1f · %s", r ? r->number : 0, t.priorityScore, t.note.c_str());
            }
        }
    }
    endCard();
}

} // namespace hms
