#include "GuiRenderer.h"
#include "Sha256.h"
#include "implot.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace hms {

// =============================================================================
// 1. DYNAMIC YIELD MANAGEMENT & PRICING SIMULATOR (Section 9.8)
// =============================================================================
/*
 * Purpose: Renders the Admin Dynamic Yield Management screen & Pricing Simulator:
 *          occupancy (0-100%), season toggle, weekend toggle, elasticity epsilon (default -1.2),
 *          live surge multiplier, price per room type, ImPlot Revenue vs Surge (1.0-1.6) curve
 *          with current marker & revenue-maximizing point highlighted, plus audit-logged manual override.
 * Inputs: None (operates on m_yieldState & m_system).
 * Outputs: Renders Yield Management screen.
 * Big-O: O(S) where S = 61 curve sample points.
 */
void GuiRenderer::renderYieldManagementScreen() {
    const ThemePalette& pal = m_theme.getActivePalette();
    const YieldPricingResult liveToday = m_system.getTodayYieldPricing();

    drawSectionHeader(
        "Pricing and demand",
        "Preview occupancy-based room rates and apply temporary pricing overrides.",
        "Algorithm: Dynamic Yield Pricing (O(1)) + Capacity-Aware Yield Curve Simulation across 61 multipliers (O(S), S=61).\nFormula: D0 = occ * 50 * 1.35; D(s) = min(50, D0 * s^epsilon); R(s) = roundTo10(base * s) * D(s).\nSource: Algorithms.cpp::computeYieldPricing, simulateYieldCurve"
    );

    if (ImGui::BeginTable("##YieldSplit", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Controls", ImGuiTableColumnFlags_WidthFixed, 380.0f);
        ImGui::TableSetupColumn("Curve", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();

        // Left: Simulator Sliders + Live Price Table + Manual Override
        ImGui::TableSetColumnIndex(0);
        if (beginCard("##YieldSimControls", "Pricing simulator", ImVec2(0.0f, 0.0f), true)) {
            ImGui::TextColored(pal.dimText, "Simulated occupancy (%%)");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderFloat("##SimOcc", &m_yieldState.occSliderPct, 0.0f, 100.0f, "%.0f%%");
            ImGui::Spacing();
            ImGui::Checkbox("Peak season (Dec–Jan / Jun–Aug: x1.15)", &m_yieldState.peakSeason);
            ImGui::Checkbox("Weekend night (Fri / Sat: x1.10)", &m_yieldState.weekendNight);
            ImGui::Spacing();
            ImGui::TextColored(pal.dimText, "Demand sensitivity");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderFloat("##SimEps", &m_yieldState.elasticityEps, -2.5f, -0.2f, "%.2f");

            const int simOccRooms = static_cast<int>(std::llround((m_yieldState.occSliderPct / 100.0f) * static_cast<float>(Cfg::TOTAL_ROOMS)));
            const YieldPricingResult simRes = computeYieldPricing(
                simOccRooms,
                Cfg::TOTAL_ROOMS,
                m_yieldState.peakSeason,
                m_yieldState.weekendNight,
                true,
                0.0
            );

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextColored(pal.goldHighlight, "Simulated surge: x%.2f", simRes.surge);
            ImGui::TextColored(pal.dimText, "Occ x%.2f · Season x%.2f · Weekend x%.2f", simRes.occMult, simRes.seasonMult, simRes.weekendMult);
            ImGui::Spacing();
            ImGui::Text("Standard     : %s", formatPKR(simRes.standardPrice).c_str());
            ImGui::Text("Deluxe       : %s", formatPKR(simRes.deluxePrice).c_str());
            ImGui::Text("Suite        : %s", formatPKR(simRes.suitePrice).c_str());
            ImGui::Text("Presidential : %s", formatPKR(simRes.presPrice).c_str());
        }
        endCard();

        ImGui::Spacing();
        if (beginCard("##YieldOverrideCard", "Manual surge override", ImVec2(0.0f, 0.0f), true)) {
            ImGui::TextColored(pal.dimText, "Today's surge: x%.2f  |  Mode: %s",
                               liveToday.surge,
                               (m_system.db().settings.manualSurgeOverride > 0.0) ? "Manual" : "Automatic");
            ImGui::Spacing();
            ImGui::TextColored(pal.dimText, "Override multiplier");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderFloat("##OvMult", &m_yieldState.overrideMult, 1.00f, 1.60f, "x%.2f");
            ImGui::Spacing();
            ImGui::TextColored(pal.dimText, "Reason");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##OvReason", "Reason for override", m_yieldState.overrideReason, sizeof(m_yieldState.overrideReason));
            ImGui::Spacing();
            if (primaryButton("Apply override", ImVec2(0.0f, 34.0f))) {
                std::string err;
                if (!m_system.setManualSurgeOverride(static_cast<double>(m_yieldState.overrideMult), m_yieldState.overrideReason, &err)) {
                    Toast::push(NoticeType::Error, "Override rejected", err);
                }
            }
            ImGui::SameLine();
            if (ghostButton("Reset to automatic", ImVec2(0.0f, 34.0f))) {
                m_system.setManualSurgeOverride(0.0, "Restored automatic pricing");
            }
        }
        endCard();

        // Right: ImPlot Revenue vs Surge Curve (1.0 - 1.6)
        ImGui::TableSetColumnIndex(1);
        const float yieldCardH = std::max(520.0f, ImGui::GetContentRegionAvail().y - 4.0f);
        if (beginCard("##YieldCurveCard", nullptr, ImVec2(0.0f, yieldCardH))) {
            ImGui::TextColored(pal.text, "Expected nightly revenue by surge level");
            ImGui::SameLine(0.0f, 8.0f);
            drawInfoIcon(
                "Formula: D0 = simulatedOccupancy * 50 * 1.35; D(s) = min(50, D0 * s^epsilon); "
                "R(s) = roundTo10(base * s) * D(s).\nComplexity: O(S), S = 61 surge steps.\n"
                "Source: Algorithms.cpp::simulateYieldCurve"
            );
            ImGui::Separator();
            ImGui::Spacing();
            const int simOccRooms = static_cast<int>(std::llround((m_yieldState.occSliderPct / 100.0f) * static_cast<float>(Cfg::TOTAL_ROOMS)));
            const YieldPricingResult simRes = computeYieldPricing(
                simOccRooms, Cfg::TOTAL_ROOMS, m_yieldState.peakSeason, m_yieldState.weekendNight, true, 0.0
            );
            const double d0 = latentDemandForOccupancy(static_cast<double>(m_yieldState.occSliderPct) / 100.0);
            double optSurge = 1.0;
            double maxRev   = 0.0;
            const std::vector<YieldCurvePoint> curve = simulateYieldCurve(
                Cfg::RATE_DELUXE, d0, static_cast<double>(m_yieldState.elasticityEps), optSurge, maxRev
            );

            std::vector<double> xs, ys;
            xs.reserve(curve.size());
            ys.reserve(curve.size());
            double curRev = 0.0;
            for (const YieldCurvePoint& pt : curve) {
                xs.push_back(pt.surgeMultiplier);
                ys.push_back(pt.expectedRevenue);
                if (std::fabs(pt.surgeMultiplier - simRes.surge) < 0.006) {
                    curRev = pt.expectedRevenue;
                }
            }

            ImGui::TextColored(pal.secondaryAccent, "Current Point: x%.2f -> %s", simRes.surge, formatPKR(curRev).c_str());
            ImGui::SameLine(0.0f, 24.0f);
            ImGui::TextColored(pal.statusAvailable, "Revenue-Maximising Surge: x%.2f -> %s", optSurge, formatPKR(maxRev).c_str());
            if (std::fabs(optSurge - 1.0) < 1e-6) {
                ImGui::TextColored(pal.dimText, "Demand is below capacity; no surge needed.");
            }
            ImGui::Spacing();

            const float plotH = std::max(360.0f, ImGui::GetContentRegionAvail().y - 4.0f);
            if (ImPlot::BeginPlot("##YieldRevPlot", ImVec2(-1.0f, plotH))) {
                ImPlot::SetupLegend(ImPlotLocation_NorthEast, ImPlotLegendFlags_Outside);
                ImPlot::SetupAxes("Surge Multiplier", "Expected Nightly Revenue (PKR)");
                ImPlot::SetupAxisLimits(ImAxis_X1, 0.98, 1.62, ImPlotCond_Always);

                ImPlot::SetNextLineStyle(pal.accent, 2.6f);
                ImPlot::PlotLine("Expected Revenue R(s)", xs.data(), ys.data(), static_cast<int>(xs.size()));

                const double curX = simRes.surge;
                ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 7.0f, pal.goldHighlight, 2.0f);
                ImPlot::PlotScatter("Current Multiplier", &curX, &curRev, 1);

                ImPlot::SetNextMarkerStyle(ImPlotMarker_Diamond, 8.0f, pal.statusAvailable, 2.0f);
                ImPlot::PlotScatter("Optimal Revenue Peak", &optSurge, &maxRev, 1);
                ImPlot::EndPlot();
            }
        }
        endCard();
        ImGui::EndTable();
    }
}

// =============================================================================
// 2. ANALYTICS: 90-DAY OCCUPANCY HEATMAP & 7-DAY FORECAST WITH +/- 1-SIGMA BAND
// =============================================================================
/*
 * Purpose: Draws the GitHub-style 90-day Occupancy Heatmap Calendar (13 weeks x 7 days)
 *          using ImDrawList rectangles with hover tooltips showing date, occupancy %, and revenue (Section 9.10 #3).
 * Inputs: None (reads m_system.db().stats).
 * Outputs: Renders heatmap widget.
 * Big-O: O(90) time, O(1) space.
 */
void GuiRenderer::draw90DayOccupancyHeatmap() {
    const ThemePalette& pal = m_theme.getActivePalette();
    const HotelDatabase& db = m_system.db();
    const int32_t today = DateUtil::todayDayNumber();

    std::unordered_map<int32_t, DailyStat> statByDay;
    for (const DailyStat& s : db.stats) {
        statByDay[s.day] = s;
    }

    constexpr int kDays = 91; // 13 columns x 7 rows
    constexpr float kGap = 5.0f;
    const int32_t startDay = today - (kDays - 1);

    // Compute min/max occupancy in the displayed 91-day window for 5 relative colour buckets (L6)
    double minOcc = 100.0;
    double maxOcc = 0.0;
    bool hasAnyData = false;
    for (int i = 0; i < kDays; ++i) {
        auto it = statByDay.find(startDay + i);
        if (it != statByDay.end()) {
            minOcc = std::min(minOcc, it->second.occupancyPct);
            maxOcc = std::max(maxOcc, it->second.occupancyPct);
            hasAnyData = true;
        }
    }
    if (!hasAnyData) {
        minOcc = 0.0;
        maxOcc = 100.0;
    }
    const double occSpan = std::max(1.0, maxOcc - minOcc);

    // 5 distinct shades for the 5 relative buckets
    const auto bucketColor = [&](int bucket) -> ImVec4 {
        static constexpr float kAlphas[5] = {0.16f, 0.34f, 0.54f, 0.76f, 1.00f};
        const int b = std::clamp(bucket, 0, 4);
        return ImVec4(pal.statusAvailable.x, pal.statusAvailable.y, pal.statusAvailable.z, kAlphas[b]);
    };

    const float availW = ImGui::GetContentRegionAvail().x;
    const float lblColW = 46.0f;
    const float cellW = std::max(18.0f, (availW - lblColW - 12.0f * kGap) / 13.0f);
    const float cellH = std::clamp(cellW * 0.34f, 20.0f, 26.0f);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const char* dayLbls[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

    for (int row = 0; row < 7; ++row) {
        if (m_fonts.captionFont != nullptr) {
            dl->AddText(
                m_fonts.captionFont,
                14.0f,
                ImVec2(origin.x, origin.y + static_cast<float>(row) * (cellH + kGap) + (cellH - 14.0f) * 0.5f),
                ThemeEngine::vec4ToU32(pal.dimText),
                dayLbls[row]
            );
        }
    }

    const float gridLeft = origin.x + lblColW;
    for (int i = 0; i < kDays; ++i) {
        const int32_t dayNum = startDay + i;
        const int col = i / 7;
        const int row = i % 7;
        const float x = gridLeft + static_cast<float>(col) * (cellW + kGap);
        const float y = origin.y + static_cast<float>(row) * (cellH + kGap);

        double occ = 0.0;
        double rev = 0.0;
        bool hasStat = false;
        auto it = statByDay.find(dayNum);
        if (it != statByDay.end()) {
            occ = it->second.occupancyPct;
            rev = it->second.revenue;
            hasStat = true;
        }

        const int bucket = hasStat
                               ? std::clamp(static_cast<int>(std::llround(((occ - minOcc) / occSpan) * 4.0)), 0, 4)
                               : -1;
        const ImVec4 cellCol = (bucket < 0) ? pal.surface : bucketColor(bucket);

        ImGui::SetCursorScreenPos(ImVec2(x, y));
        char cellId[24]{};
        std::snprintf(cellId, sizeof(cellId), "##HmCell_%d", i);
        ImGui::InvisibleButton(cellId, ImVec2(cellW, cellH));
        const bool hovered = ImGui::IsItemHovered();

        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + cellW, y + cellH), ThemeEngine::vec4ToU32(cellCol), 4.0f);
        if (hovered || dayNum == today) {
            dl->AddRect(
                ImVec2(x, y),
                ImVec2(x + cellW, y + cellH),
                ThemeEngine::vec4ToU32(dayNum == today ? pal.goldHighlight : pal.text),
                4.0f,
                0,
                1.5f
            );
        }
        if (hovered) {
            ImGui::BeginTooltip();
            ImGui::TextColored(pal.accent, "%s", DateUtil::formatDateUI(dayNum).c_str());
            ImGui::Text("Occupancy: %.1f%%", occ);
            ImGui::Text("Revenue  : %s", formatPKR(rev).c_str());
            ImGui::EndTooltip();
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + 7.0f * (cellH + kGap) + 8.0f));
    ImGui::TextColored(pal.dimText, "Less (%.0f%%)", minOcc);
    for (int lvl = 0; lvl < 5; ++lvl) {
        ImGui::SameLine();
        drawStatusDot(bucketColor(lvl), 6.0f);
    }
    ImGui::SameLine();
    ImGui::TextColored(pal.dimText, "More (%.0f%%)  ·  Hover any day for exact occupancy & revenue", maxOcc);
}

/*
 * Purpose: Renders the Executive Analytics screen featuring the 90-day GitHub-style Occupancy Heatmap
 *          and 7-day least-squares linear regression Revenue & Occupancy forecasts with shaded +/- 1-sigma bands.
 * Inputs: None.
 * Outputs: Renders Analytics screen.
 * Big-O: O(N) over 30-day history.
 */
void GuiRenderer::renderAnalyticsScreen() {
    const ThemePalette& pal = m_theme.getActivePalette();
    const HotelDatabase& db = m_system.db();
    const int32_t today = DateUtil::todayDayNumber();

    drawSectionHeader(
        "Analytics",
        "Ninety-day occupancy trends and seven-day revenue and occupancy projections.",
        "Algorithm: Ordinary Least-Squares Linear Regression with +/- 1-sigma residual band (O(N), N=30) + 91-day relative bucket heatmap (O(D), D=91).\nSource: Algorithms.cpp::compute7DayForecast, Gui_Admin.cpp::draw90DayOccupancyHeatmap"
    );

    if (beginCard("##HeatmapCard", "90-day occupancy calendar", ImVec2(0.0f, 0.0f), true)) {
        draw90DayOccupancyHeatmap();
    }
    endCard();

    ImGui::Spacing();

    double todayInvRev = 0.0;
    for (const Invoice& inv : db.invoices) {
        if (inv.issuedDay == today) {
            todayInvRev += inv.grandTotal;
        }
    }

    std::vector<double> rev30;
    std::vector<double> occ30;
    const std::size_t start = (db.stats.size() > 30U) ? (db.stats.size() - 30U) : 0U;
    for (std::size_t i = start; i < db.stats.size(); ++i) {
        rev30.push_back(db.stats[i].revenue);
        occ30.push_back(db.stats[i].occupancyPct);
    }
    if (!db.stats.empty() && db.stats.back().day == today && todayInvRev >= 1.0 && !rev30.empty()) {
        rev30.back() = todayInvRev;
    }
    if (rev30.empty()) {
        rev30.assign(7U, 0.0);
        occ30.assign(7U, 0.0);
    }

    const LinearRegressionForecast revFc = compute7DayForecast(rev30, "Analytics Revenue");
    const LinearRegressionForecast occFc = compute7DayForecast(occ30, "Analytics Occupancy");

    const float forecastCardH = std::max(340.0f, ImGui::GetContentRegionAvail().y - 4.0f);
    if (ImGui::BeginTable("##ForecastCharts", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        auto plotForecastCard = [&](const char* cardId, const char* title, const std::vector<double>& hist,
                                    const LinearRegressionForecast& fc, const char* yLbl, const ImVec4& lineCol) {
            if (beginCard(cardId, title, ImVec2(0.0f, forecastCardH))) {
                ImGui::TextColored(pal.dimText, "Trend: %s", fc.formulaText.c_str());
                const int hn = static_cast<int>(hist.size());
                std::vector<double> xh(static_cast<std::size_t>(hn));
                for (int i = 0; i < hn; ++i) { xh[static_cast<std::size_t>(i)] = static_cast<double>(i + 1); }

                std::vector<double> xf(8U), yf(8U), yUp(8U), yLo(8U);
                xf[0]  = static_cast<double>(hn);
                yf[0]  = hist.back();
                yUp[0] = hist.back();
                yLo[0] = hist.back();
                for (int f = 0; f < 7; ++f) {
                    xf[static_cast<std::size_t>(f + 1)]  = static_cast<double>(hn + f + 1);
                    yf[static_cast<std::size_t>(f + 1)]  = fc.predicted7Days[static_cast<std::size_t>(f)];
                    yUp[static_cast<std::size_t>(f + 1)] = fc.upperBand7Days[static_cast<std::size_t>(f)];
                    yLo[static_cast<std::size_t>(f + 1)] = fc.lowerBand7Days[static_cast<std::size_t>(f)];
                }

                const float plotH = std::max(180.0f, ImGui::GetContentRegionAvail().y - 4.0f);
                if (ImPlot::BeginPlot((std::string("##Plot_") + cardId).c_str(), ImVec2(-1.0f, plotH))) {
                    ImPlot::SetupAxes("Day", yLbl);
                    ImPlot::SetNextFillStyle(pal.goldHighlight, 0.18f);
                    ImPlot::PlotShaded("+/- 1-sigma Band", xf.data(), yLo.data(), yUp.data(), 8);

                    ImPlot::SetNextLineStyle(lineCol, 2.4f);
                    ImPlot::PlotLine("Last 30 Days", xh.data(), hist.data(), hn);

                    ImPlot::SetNextLineStyle(pal.goldHighlight, 2.2f);
                    ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 3.0f, pal.goldHighlight);
                    ImPlot::PlotLine("7-Day Forecast", xf.data(), yf.data(), 8);
                    ImPlot::EndPlot();
                }
            }
            endCard();
        };

        ImGui::TableSetColumnIndex(0);
        plotForecastCard("##RevFcCard", "Revenue 30-Day + 7-Day Forecast (+/- 1-sigma)", rev30, revFc, "PKR", pal.accent);
        ImGui::TableSetColumnIndex(1);
        plotForecastCard("##OccFcCard", "Occupancy % 30-Day + 7-Day Forecast (+/- 1-sigma)", occ30, occFc, "Occupancy %", pal.secondaryAccent);
        ImGui::EndTable();
    }
}

// =============================================================================
// 3. HOUSEKEEPING & MAINTENANCE MIN-HEAP DISPATCH BOARD (Section 9.9)
// =============================================================================
/*
 * Purpose: Renders the Housekeeping & Maintenance Dispatch Board listing tasks in exact custom
 *          TaskMinHeap pop order (highest P = 3U + V + E + 2*sev first) with priority bars,
 *          urgency chips, and [Start], [Done], [Reassign] buttons (Section 9.9).
 * Inputs: None.
 * Outputs: Renders Dispatch Board and mutates tasks via HotelSystem.
 * Big-O: O(T log T) to copy and pop heap for display.
 */
void GuiRenderer::renderHousekeepingBoardScreen() {
    const ThemePalette& pal = m_theme.getActivePalette();
    const std::vector<HousekeepingTask> ordered = m_system.getHousekeepingDispatchOrder();

    drawSectionHeader(
        "Housekeeping",
        "Track room cleaning and maintenance tasks ordered by urgency.",
        "Algorithm: Custom array-backed Binary Min-Heap keyed on -P where P = 3U + V + E + 2*sev (recomputed every 30s).\nComplexity: O(log T) insert/pop, O(T log T) full dispatch order.\nSource: Algorithms.cpp::TaskMinHeap, HotelSystem.cpp::recomputeAllTaskPriorities"
    );

    if (beginCard("##HkToolbar", nullptr, ImVec2(0.0f, 0.0f), true)) {
        if (primaryButton("Refresh priorities", ImVec2(0.0f, 34.0f))) {
            m_system.recomputeAllTaskPriorities();
            Toast::push(NoticeType::Info, "Priorities updated", "All housekeeping and maintenance priorities were recalculated.");
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputTextWithHint("##HkAssignee", "Assignee name", m_hkReassignBuf, sizeof(m_hkReassignBuf));
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(pal.dimText, "Open tasks: %zu", ordered.size());
    }
    endCard();
    ImGui::Spacing();

    if (beginCard("##HkListCard", "Priority queue", ImVec2(0.0f, 0.0f))) {
        if (ordered.empty()) {
            drawEmptyState("All rooms ready", "There are no pending cleaning or maintenance tasks.");
        } else {
            for (std::size_t i = 0U; i < ordered.size(); ++i) {
                const HousekeepingTask& t = ordered[i];
                const Room* r = m_system.findRoomById(t.roomId);
                const float normP = static_cast<float>(std::clamp(t.priorityScore / 50.0, 0.05, 1.0));
                const ImVec4 urgCol = (t.priorityScore >= 30.0)
                                          ? pal.statusOccupied
                                          : ((t.priorityScore >= 16.0) ? pal.statusReserved : pal.statusAvailable);

                char cardId[32]{};
                std::snprintf(cardId, sizeof(cardId), "##HkTaskRow_%d", t.id);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.surface);
                ImGui::BeginChild(
                    cardId,
                    ImVec2(0.0f, 0.0f),
                    ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
                );

                ImGui::TextColored(pal.text, "#%zu  Room %d", i + 1U, r ? r->number : 0);
                ImGui::SameLine();
                drawBadge(toString(t.type), t.type == TaskType::Maintenance ? pal.statusOccupied : pal.statusCleaning);
                ImGui::SameLine();
                drawBadge(toString(t.status), t.status == TaskStatus::InProgress ? pal.accent : pal.statusReserved);
                ImGui::SameLine();
                char pBadge[32]{};
                std::snprintf(pBadge, sizeof(pBadge), "Priority P=%.1f", t.priorityScore);
                drawBadge(pBadge, urgCol);
                if (t.type == TaskType::Maintenance) {
                    ImGui::SameLine();
                    char sBadge[24]{};
                    std::snprintf(sBadge, sizeof(sBadge), "Sev %d/5", t.severity);
                    drawBadge(sBadge, pal.statusOccupied);
                }

                // Right-aligned action buttons sized from measured text width
                const float stW = visibleTextSize("Start").x + 24.0f;
                const float dnW = visibleTextSize("Done").x + 24.0f;
                const float reW = visibleTextSize("Reassign").x + 24.0f;
                const float sp  = ImGui::GetStyle().ItemSpacing.x;
                const float grpW = stW + dnW + reW + sp * 2.0f;
                const float rightX = ImGui::GetWindowContentRegionMax().x - grpW;
                if (rightX > ImGui::GetCursorPosX() + 12.0f) {
                    ImGui::SameLine(rightX);
                } else {
                    ImGui::SameLine();
                }
                char stId[32]{}; std::snprintf(stId, sizeof(stId), "Start##HkS%d", t.id);
                if (ghostButton(stId, ImVec2(stW, 28.0f), t.status == TaskStatus::Queued)) {
                    m_system.startHousekeepingTask(t.id);
                }
                if (t.status != TaskStatus::Queued && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Task is already in progress.");
                }
                ImGui::SameLine();
                char dnId[32]{}; std::snprintf(dnId, sizeof(dnId), "Done##HkD%d", t.id);
                if (primaryButton(dnId, ImVec2(dnW, 28.0f))) {
                    m_system.completeHousekeepingTask(t.id);
                }
                ImGui::SameLine();
                char reId[32]{}; std::snprintf(reId, sizeof(reId), "Reassign##HkR%d", t.id);
                if (ghostButton(reId, ImVec2(reW, 28.0f))) {
                    m_system.reassignHousekeepingTask(t.id, m_hkReassignBuf);
                    Toast::push(NoticeType::Info, "Task Reassigned", "Task #" + std::to_string(t.id) + " -> " + std::string(m_hkReassignBuf));
                }

                // Colored priority bar + note
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, urgCol);
                ImGui::ProgressBar(normP, ImVec2(200.0f, 10.0f), "");
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::TextColored(pal.dimText, "Assigned: %s  |  %s", t.assignedTo.c_str(), t.note.c_str());

                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
        }
    }
    endCard();
}

// =============================================================================
// 4. SERVICE ORDERS KANBAN (3 COLUMNS WITH DRAG-AND-DROP, Section 9.10 #6)
// =============================================================================
/*
 * Purpose: Renders the 3-column Service Orders Kanban (Ordered / Preparing / Delivered) with
 *          ImGui drag-and-drop cards. Dropping a card into Delivered automatically posts the
 *          charge to the guest's booking folio (Section 9.10 #6).
 * Inputs: None.
 * Outputs: Renders Kanban board.
 * Big-O: O(O) where O is number of service orders.
 */
void GuiRenderer::renderServiceOrdersKanbanScreen() {
    const ThemePalette& pal = m_theme.getActivePalette();
    const HotelDatabase& db = m_system.db();

    drawSectionHeader(
        "Service orders",
        "Move room service requests from ordered to delivered and bill them to the guest folio.",
        "Algorithm: Linear order stage tracking with automatic folio add-on posting.\nComplexity: O(O) render, O(B) booking charge post.\nSource: HotelSystem.cpp::moveServiceOrderStage, Gui_Admin.cpp::renderServiceOrdersKanbanScreen"
    );

    // Quick Create Order bar
    if (beginCard("##KanbanCreateBar", nullptr, ImVec2(0.0f, 0.0f), true)) {
        const char* addOnTypes[] = {"Minibar", "Laundry", "Dining", "Spa", "Transport"};
        const std::string bkLbl = (m_kanbanState.newOrderBookingId > 0)
                                      ? ("Booking #" + std::to_string(m_kanbanState.newOrderBookingId))
                                      : "Select Active Booking...";
        ImGui::SetNextItemWidth(190.0f);
        if (ImGui::BeginCombo("##KbBooking", bkLbl.c_str())) {
            for (const Booking& b : db.bookings) {
                if (b.status != BookingStatus::CheckedIn) { continue; }
                const Room* r = m_system.findRoomById(b.roomId);
                char opt[64]{};
                std::snprintf(opt, sizeof(opt), "Booking #%d (Room %d)", b.id, r ? r->number : 0);
                if (ImGui::Selectable(opt, m_kanbanState.newOrderBookingId == b.id)) {
                    m_kanbanState.newOrderBookingId = b.id;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(115.0f);
        ImGui::Combo("##KbType", &m_kanbanState.newOrderTypeIdx, addOnTypes, 5);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(185.0f);
        ImGui::InputTextWithHint("##KbItem", "Item name", m_kanbanState.newOrderItem, sizeof(m_kanbanState.newOrderItem));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(65.0f);
        ImGui::InputInt("Qty##KbQ", &m_kanbanState.newOrderQty);
        m_kanbanState.newOrderQty = std::max(1, m_kanbanState.newOrderQty);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(95.0f);
        ImGui::InputFloat("PKR##KbP", &m_kanbanState.newOrderPrice, 0.0f, 0.0f, "%.0f");
        ImGui::SameLine();
        if (primaryButton("+ Create Order", ImVec2(0.0f, 34.0f), m_kanbanState.newOrderBookingId > 0 && m_kanbanState.newOrderItem[0] != '\0')) {
            m_system.createServiceOrder(
                m_kanbanState.newOrderBookingId,
                static_cast<AddOnType>(m_kanbanState.newOrderTypeIdx),
                m_kanbanState.newOrderItem,
                m_kanbanState.newOrderQty,
                static_cast<double>(m_kanbanState.newOrderPrice)
            );
            m_kanbanState.newOrderItem[0] = '\0';
        }
    }
    endCard();
    ImGui::Spacing();

    const char* colTitles[3] = {"Ordered", "Preparing", "Delivered (Billed to Folio)"};
    const ImVec4 colAccents[3] = {pal.statusReserved, pal.accent, pal.statusAvailable};

    if (ImGui::BeginTable("##KanbanColumns", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        for (int stage = 0; stage < 3; ++stage) {
            ImGui::TableSetColumnIndex(stage);
            char colId[32]{};
            std::snprintf(colId, sizeof(colId), "##KanbanCol_%d", stage);
            if (beginCard(colId, colTitles[stage], ImVec2(0.0f, 0.0f))) {
                // Full column drop target
                const ImVec2 colStart = ImGui::GetCursorScreenPos();
                for (const ServiceOrder& o : db.orders) {
                    if (o.stage != stage) { continue; }
                    const Booking* b = m_system.findBookingById(o.bookingId);
                    const Room* r = (b != nullptr) ? m_system.findRoomById(b->roomId) : nullptr;

                    char cId[32]{};
                    std::snprintf(cId, sizeof(cId), "##KbOrder_%d", o.id);
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.surface);
                    ImGui::BeginChild(
                        cId,
                        ImVec2(0.0f, 0.0f),
                        ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
                    );
                    drawBadge(toString(o.type), colAccents[stage]);
                    ImGui::SameLine();
                    ImGui::TextColored(pal.text, "Room %d  (x%d)", r ? r->number : 0, o.qty);
                    ImGui::TextWrapped("%s", o.item.c_str());
                    ImGui::Spacing();
                    const std::string priceStr = formatPKR(o.price * o.qty);
                    const float priceW = ImGui::CalcTextSize(priceStr.c_str()).x;
                    const float advW   = visibleTextSize("Advance ->").x + 24.0f;
                    const float availW = ImGui::GetContentRegionAvail().x;
                    ImGui::TextColored(pal.goldHighlight, "%s", priceStr.c_str());
                    if (stage < 2) {
                        if (priceW + 12.0f + advW <= availW) {
                            ImGui::SameLine(ImGui::GetCursorPosX() + (availW - advW));
                        }
                        char advId[32]{}; std::snprintf(advId, sizeof(advId), "Advance ->##KbAdv%d", o.id);
                        if (ghostButton(advId, ImVec2(advW, 28.0f))) {
                            m_system.moveServiceOrderStage(o.id, stage + 1);
                        }
                    }
                    checkChildOverflow(cId);
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                    ImGui::Spacing();

                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                        ImGui::SetDragDropPayload("HMS_KANBAN_ORDER", &o.id, sizeof(int));
                        ImGui::Text("Move Order #%d: %s", o.id, o.item.c_str());
                        ImGui::EndDragDropSource();
                    }
                }

                // Invisible drop area filling remainder of column
                const ImVec2 avail = ImGui::GetContentRegionAvail();
                ImGui::SetCursorScreenPos(colStart);
                ImGui::InvisibleButton((std::string("##KbDropZone_") + std::to_string(stage)).c_str(),
                                       ImVec2(std::max(100.0f, avail.x), std::max(160.0f, avail.y)));
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HMS_KANBAN_ORDER")) {
                        const int orderId = *static_cast<const int*>(payload->Data);
                        m_system.moveServiceOrderStage(orderId, stage);
                    }
                    ImGui::EndDragDropTarget();
                }
            }
            endCard();
        }
        ImGui::EndTable();
    }
}

// =============================================================================
// 5. STAFF MANAGEMENT, AUDIT LOG (VERIFY CHAIN) & SETTINGS (Section 6, 7)
// =============================================================================
/*
 * Purpose: Renders Admin Staff & RBAC Accounts screen (create staff accounts & toggle Active status).
 * Inputs: None.
 * Outputs: Renders Staff screen.
 * Big-O: O(U) where U is number of staff users.
 */
void GuiRenderer::renderStaffScreen() {
    const ThemePalette& pal = m_theme.getActivePalette();
    const HotelDatabase& db = m_system.db();

    drawSectionHeader(
        "Staff accounts",
        "Create staff logins, assign roles, and enable or disable account access.",
        "Algorithm: Salted 1,000-round SHA-256 key stretching + server-side RBAC matrix check.\nComplexity: O(1) per login verification (fixed 1,000 rounds), O(U) staff listing.\nSource: Sha256.cpp::stretchPassword, HotelSystem.cpp::createUser, setUserActive"
    );

    if (beginCard("##StaffAddCard", "Create staff account", ImVec2(0.0f, 0.0f), true)) {
        const char* roles[] = {"Admin", "Receptionist", "Housekeeping"};
        bool enterCreate = false;
        ImGui::SetNextItemWidth(150.0f); enterCreate |= ImGui::InputTextWithHint("##StU", "Username", m_staffState.newUserBuf, sizeof(m_staffState.newUserBuf), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f); enterCreate |= ImGui::InputTextWithHint("##StP", "Password (6+ chars)", m_staffState.newPassBuf, sizeof(m_staffState.newPassBuf), ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(190.0f); enterCreate |= ImGui::InputTextWithHint("##StN", "Display name", m_staffState.newNameBuf, sizeof(m_staffState.newNameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f); ImGui::Combo("##StR", &m_staffState.newRoleIdx, roles, 3);
        ImGui::SameLine();
        if (primaryButton("Create account", ImVec2(0.0f, 34.0f)) || enterCreate) {
            std::string err;
            if (m_system.createUser(m_staffState.newUserBuf, m_staffState.newPassBuf, static_cast<Role>(m_staffState.newRoleIdx), m_staffState.newNameBuf, &err)) {
                m_staffState.newUserBuf[0] = '\0';
                m_staffState.newPassBuf[0] = '\0';
                m_staffState.newNameBuf[0] = '\0';
            } else {
                Toast::push(NoticeType::Error, "Could not create account", err);
            }
        }
    }
    endCard();
    ImGui::Spacing();

    int activeAdmins = 0;
    for (const User& cand : db.users) {
        if (cand.role == Role::Admin && cand.active) {
            ++activeAdmins;
        }
    }
    const User* signedIn = m_system.currentUser();
    const int myUserId = (signedIn != nullptr) ? signedIn->id : -1;
    const float actBtnW = std::max(visibleTextSize("Disable").x, visibleTextSize("Enable").x) + 26.0f;

    if (beginCard("##StaffTblCard", "Registered Staff", ImVec2(0.0f, 0.0f))) {
        if (ImGui::BeginTable("##StaffTbl", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 48.0f);
            ImGui::TableSetupColumn("Username");
            ImGui::TableSetupColumn("Display Name");
            ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 105.0f);
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, actBtnW + 16.0f);
            ImGui::TableHeadersRow();
            for (const User& u : db.users) {
                const bool isSelf = (u.id == myUserId);
                const bool isLastAdmin = (u.role == Role::Admin && u.active && activeAdmins <= 1);
                const bool canToggle = !(u.active && (isSelf || isLastAdmin));
                const char* blockTip = isSelf
                    ? "You cannot disable the account you are currently signed in with."
                    : (isLastAdmin ? "At least one active Admin account is required." : nullptr);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("#%d", u.id);
                ImGui::TableSetColumnIndex(1); ImGui::Text("%s", u.username.c_str());
                ImGui::TableSetColumnIndex(2); ImGui::Text("%s", u.displayName.c_str());
                ImGui::TableSetColumnIndex(3); drawBadge(toString(u.role), pal.accent);
                ImGui::TableSetColumnIndex(4); drawBadge(u.active ? "Active" : "Disabled", u.active ? pal.statusAvailable : pal.statusOccupied);
                ImGui::TableSetColumnIndex(5);
                char actId[32]{};
                std::snprintf(actId, sizeof(actId), "%s##StAct%d", u.active ? "Disable" : "Enable", u.id);
                const bool clicked = u.active
                    ? dangerButton(actId, ImVec2(actBtnW, 28.0f), canToggle)
                    : primaryButton(actId, ImVec2(actBtnW, 28.0f), true);
                if (!canToggle && blockTip != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("%s", blockTip);
                }
                if (clicked) {
                    std::string err;
                    if (!m_system.setUserActive(u.id, !u.active, &err)) {
                        Toast::push(NoticeType::Error, "Update Failed", err);
                    }
                }
            }
            ImGui::EndTable();
        }
    }
    endCard();
}

/*
 * Purpose: Renders the Tamper-Evident SHA-256 Audit Log screen with the live green
 *          "CHAIN VERIFIED (N entries)" or flashing red "TAMPERING DETECTED at entry #K" banner
 *          and [Verify Now] button (Section 6).
 * Inputs: None.
 * Outputs: Renders Audit Log screen.
 * Big-O: O(A) to display entries; O(A * L) on Verify Now.
 */
void GuiRenderer::renderAuditLogScreen() {
    const ThemePalette& pal = m_theme.getActivePalette();
    const HotelDatabase& db = m_system.db();
    const int badIdx = m_system.verifyAuditLogChain();

    drawSectionHeader(
        "Audit log",
        "Review recorded staff actions and verify that historical records have not been altered.",
        "Algorithm: Tamper-evident SHA-256 hash chain from 64-zero genesis hash.\nComplexity: O(A * L) full chain verification.\nSource: Sha256.cpp::computeAuditEntryHash, FileManager.cpp::verifyAuditChain"
    );

    const float flash = 0.55f + 0.45f * std::sin(static_cast<float>(ImGui::GetTime()) * 6.0f);
    const ImVec4 bannerCol = (badIdx == -1)
                                 ? pal.statusAvailable
                                 : ImVec4(pal.statusOccupied.x, pal.statusOccupied.y, pal.statusOccupied.z, flash);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(bannerCol.x, bannerCol.y, bannerCol.z, 0.20f));
    ImGui::PushStyleColor(ImGuiCol_Border, bannerCol);
    ImGui::BeginChild(
        "##AuditBanner",
        ImVec2(0.0f, 0.0f),
        ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );
    if (badIdx == -1) {
        ImGui::TextColored(pal.statusAvailable, "Log verified (%zu entries intact from first record to latest)", db.auditLog.size());
    } else {
        ImGui::TextColored(pal.statusOccupied, "Integrity mismatch at entry #%d. Inspect data/audit_log.dat.", badIdx);
    }
    ImGui::SameLine(ImGui::GetWindowWidth() - 145.0f);
    if (primaryButton("Verify now", ImVec2(0.0f, 32.0f))) {
        const int res = m_system.verifyAuditLogChain();
        if (res == -1) {
            Toast::push(NoticeType::Success, "Audit log verified", "All " + std::to_string(db.auditLog.size()) + " entries are intact.");
        } else {
            Toast::push(NoticeType::Error, "Integrity mismatch", "Audit log mismatch detected at entry #" + std::to_string(res) + ".");
        }
    }
    checkChildOverflow("##AuditBanner");
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::Spacing();

    if (beginCard("##AuditTblCard", "Audit entries (newest first)", ImVec2(0.0f, 0.0f))) {
        if (ImGui::BeginTable("##AuditTbl", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 45.0f);
            ImGui::TableSetupColumn("User", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 145.0f);
            ImGui::TableSetupColumn("Details");
            ImGui::TableSetupColumn("Previous", ImGuiTableColumnFlags_WidthFixed, 115.0f);
            ImGui::TableSetupColumn("Signature", ImGuiTableColumnFlags_WidthFixed, 115.0f);
            ImGui::TableHeadersRow();

            for (int i = static_cast<int>(db.auditLog.size()) - 1; i >= 0; --i) {
                const AuditEntry& e = db.auditLog[static_cast<std::size_t>(i)];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("#%d", e.index);
                ImGui::TableSetColumnIndex(1); ImGui::Text("%s", e.username.c_str());
                ImGui::TableSetColumnIndex(2); drawBadge(e.action.c_str(), (badIdx == e.index) ? pal.statusOccupied : pal.accent);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%s", e.details.c_str());
                ImGui::TableSetColumnIndex(4); ImGui::TextColored(pal.dimText, "%s...", e.prevHashHex.substr(0U, 10U).c_str());
                ImGui::TableSetColumnIndex(5); ImGui::TextColored(pal.secondaryAccent, "%s...", e.hashHex.substr(0U, 10U).c_str());
            }
            ImGui::EndTable();
        }
    }
    endCard();
}

/*
 * Purpose: Renders the Snapshot Backups & Night Audit Operations screen (Admin only, Section 5 & Section 9.10 #5):
 *          lists timestamped snapshots in backups/ (max 10 retained) with [Create Backup Now],
 *          [Restore] with confirmation modal, and [Run Night Audit] with live report summary.
 * Inputs: None.
 * Outputs: Renders Backups & Night Audit screen.
 * Big-O: O(S log S) where S <= 10 snapshots.
 */
void GuiRenderer::renderBackupsScreen() {
    const ThemePalette& pal = m_theme.getActivePalette();
    drawSectionHeader(
        "Backups and night audit",
        "Create or restore hotel backups and close out the business day.",
        "Algorithm: Atomic .tmp -> rename binary persistence with FNV-1a checksums and rolling 10-snapshot retention.\nComplexity: O(N) table serialization, O(S log S) snapshot listing (S <= 10).\nSource: FileManager.cpp::createBackupSnapshot, HotelSystem.cpp::runNightAudit"
    );

    if (ImGui::BeginTable("##BackupTopRow", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (beginCard("##BackupActionsCard", "Backup controls", ImVec2(0.0f, 0.0f), true)) {
            ImGui::PushStyleColor(ImGuiCol_Text, pal.dimText);
            ImGui::TextWrapped("Saves a timestamped backup of all hotel data files (up to 10 snapshots kept).");
            ImGui::PopStyleColor();
            ImGui::Spacing();
            if (primaryButton("Create backup now", ImVec2(0.0f, 38.0f))) {
                m_system.createBackupNow();
            }
        }
        endCard();

        ImGui::TableSetColumnIndex(1);
        if (beginCard("##NightAuditCard", "Night audit closeout", ImVec2(0.0f, 0.0f), true)) {
            ImGui::PushStyleColor(ImGuiCol_Text, pal.dimText);
            ImGui::TextWrapped("Records overdue no-shows, flags unpaid invoices, and writes the daily closeout report.");
            ImGui::PopStyleColor();
            ImGui::Spacing();
            if (primaryButton("Run night audit", ImVec2(0.0f, 38.0f))) {
                m_system.runNightAudit(&m_adminOpsState.lastNightAuditPath, &m_adminOpsState.lastNightAuditSummary);
            }
            if (!m_adminOpsState.lastNightAuditSummary.empty()) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, pal.statusAvailable);
                ImGui::TextWrapped("Report: %s", m_adminOpsState.lastNightAuditPath.c_str());
                ImGui::PopStyleColor();
            }
        }
        endCard();
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (beginCard("##BackupsListCard", "Available Backup Snapshots (Newest First, Max 10)", ImVec2(0.0f, 0.0f))) {
        const std::vector<std::string> snaps = m_system.listBackups();
        if (snaps.empty()) {
            drawEmptyState("No Backup Snapshots Yet", "Click 'Create Snapshot Backup Now' or sign out to generate a snapshot.");
        } else if (ImGui::BeginTable("##BackupsTbl", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 48.0f);
            ImGui::TableSetupColumn("Snapshot Folder (backups/YYYYMMDD_HHMMSS)");
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableHeadersRow();
            for (std::size_t i = 0U; i < snaps.size(); ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("#%zu", i + 1U);
                ImGui::TableSetColumnIndex(1); ImGui::TextColored(pal.accent, "%s", snaps[i].c_str());
                ImGui::TableSetColumnIndex(2);
                char rstId[48]{};
                std::snprintf(rstId, sizeof(rstId), "Restore##BkRst%zu", i);
                if (dangerButton(rstId, ImVec2(0.0f, 28.0f))) {
                    m_adminOpsState.pendingRestoreSnap = snaps[i];
                    m_adminOpsState.openRestoreModal = true;
                }
            }
            ImGui::EndTable();
        }
    }
    endCard();

    // Confirm Restore Modal
    if (m_adminOpsState.openRestoreModal) {
        ImGui::OpenPopup("##ConfirmRestoreModal");
        m_adminOpsState.openRestoreModal = false;
    }
    ImGui::SetNextWindowSize(ImVec2(440.0f, 185.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("##ConfirmRestoreModal", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextColored(pal.statusReserved, "Confirm Database Snapshot Restore");
        ImGui::Separator();
        ImGui::TextWrapped("Restore all 11 .dat tables from snapshot '%s'? Current unsaved changes will be replaced.",
                           m_adminOpsState.pendingRestoreSnap.c_str());
        ImGui::Spacing();
        if (dangerButton("Yes, Restore Snapshot", ImVec2(0.0f, 36.0f))) {
            std::string err;
            if (!m_system.restoreBackup(m_adminOpsState.pendingRestoreSnap, &err)) {
                Toast::push(NoticeType::Error, "Restore Failed", err);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ghostButton("Cancel", ImVec2(0.0f, 36.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

/*
 * Purpose: Renders the System Settings screen (theme choice, GST tax rate, surge on/off).
 * Inputs: None.
 * Outputs: Renders Settings screen and saves settings.dat.
 * Big-O: O(1) time.
 */
void GuiRenderer::renderSettingsScreen() {
    const ThemePalette& pal = m_theme.getActivePalette();
    AppSettings& settings = m_system.db().settings;

    drawSectionHeader(
        "Settings",
        "Configure hotel tax, dynamic pricing, text size, theme and project information.",
        "Storage: Atomic .tmp -> rename binary serialization to data/settings.dat with FNV-1a checksum.\nComplexity: O(1) read/write.\nSource: FileManager.cpp::saveSettings, loadSettings"
    );

    if (beginCard("##SettingsCard", "Hotel configuration and display", ImVec2(0.0f, 0.0f), true)) {
        bool surgeOn = settings.surgeEnabled;
        if (ImGui::Checkbox("Enable automatic demand pricing", &surgeOn)) {
            m_system.setSurgeEnabled(surgeOn);
        }
        ImGui::Spacing();
        ImGui::TextColored(pal.dimText, "GST / hotel tax rate (%%)");
        ImGui::SetNextItemWidth(std::min(420.0f, ImGui::GetContentRegionAvail().x));
        float taxPct = static_cast<float>(settings.taxRate * 100.0);
        if (ImGui::SliderFloat("##GstTaxRate", &taxPct, 0.0f, 30.0f, "%.1f%%")) {
            m_system.setTaxRate(static_cast<double>(taxPct) / 100.0);
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(pal.dimText, "Text size (Ctrl+= / Ctrl+-)");
        const char* scaleLabels[4] = {"90%", "100%", "115%", "130%"};
        const int curScaleIdx = std::clamp(settings.textScaleIdx, 0, 3);
        for (int i = 0; i < 4; ++i) {
            if (i > 0) { ImGui::SameLine(); }
            const bool active = (i == curScaleIdx);
            const bool clicked = active
                                     ? primaryButton(scaleLabels[i], ImVec2(0.0f, 34.0f))
                                     : ghostButton(scaleLabels[i], ImVec2(0.0f, 34.0f));
            if (clicked && !active) {
                settings.textScaleIdx = i;
                m_system.fileManager().saveSettings(settings);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        const char* themeSwitchLbl = (m_theme.getCurrentMode() == ThemeMode::MidnightObsidian)
                                         ? "Switch to light theme (Executive Pearl)"
                                         : "Switch to dark theme (Midnight Obsidian)";
        if (ghostButton(themeSwitchLbl, ImVec2(0.0f, 36.0f))) {
            m_theme.toggleTheme();
            settings.themeChoice = (m_theme.getCurrentMode() == ThemeMode::ExecutivePearl) ? 1 : 0;
            m_system.fileManager().saveSettings(settings);
        }
    }
    endCard();

    ImGui::Spacing();

    // About card (C2: only place where university name appears, plus version, build date & 3 saved team names)
    if (beginCard("##AboutCard", "About", ImVec2(0.0f, 0.0f), true)) {
        ImGui::TextColored(pal.text, "%s — %s", Cfg::HOTEL_NAME, Cfg::HOTEL_TAGLINE);
        ImGui::TextColored(pal.dimText, "Version %s  ·  Build date: %s", Cfg::APP_VERSION, __DATE__);
        ImGui::TextColored(pal.dimText, "Programming Fundamentals Project, NUST SEECS");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(pal.dimText, "Project team");

        static char teamBuf1[64]{};
        static char teamBuf2[64]{};
        static char teamBuf3[64]{};
        static bool teamInit = false;
        if (!teamInit) {
            std::snprintf(teamBuf1, sizeof(teamBuf1), "%s", settings.teamName1.c_str());
            std::snprintf(teamBuf2, sizeof(teamBuf2), "%s", settings.teamName2.c_str());
            std::snprintf(teamBuf3, sizeof(teamBuf3), "%s", settings.teamName3.c_str());
            teamInit = true;
        }
        bool teamChanged = false;
        const float fieldW = std::min(360.0f, ImGui::GetContentRegionAvail().x);
        ImGui::SetNextItemWidth(fieldW);
        teamChanged |= ImGui::InputTextWithHint("##Team1", "Team member 1", teamBuf1, sizeof(teamBuf1));
        ImGui::SetNextItemWidth(fieldW);
        teamChanged |= ImGui::InputTextWithHint("##Team2", "Team member 2", teamBuf2, sizeof(teamBuf2));
        ImGui::SetNextItemWidth(fieldW);
        teamChanged |= ImGui::InputTextWithHint("##Team3", "Team member 3", teamBuf3, sizeof(teamBuf3));
        if (teamChanged) {
            settings.teamName1 = teamBuf1;
            settings.teamName2 = teamBuf2;
            settings.teamName3 = teamBuf3;
            m_system.fileManager().saveSettings(settings);
        }
    }
    endCard();

    // Collapsible Developer & Evaluation Tools card (C3: Admin only)
    if (m_system.currentRole() == Role::Admin) {
        ImGui::Spacing();
        if (beginCard("##DevToolsSettingsCard", nullptr, ImVec2(0.0f, 0.0f), true)) {
            if (ImGui::CollapsingHeader("Developer & Evaluation Tools")) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, pal.dimText);
                ImGui::TextWrapped(
                    "Sample data loader, guided tour, algorithm telemetry, sorting benchmarks, visualiser and automated self-tests. Shortcut: Ctrl+Shift+D."
                );
                ImGui::PopStyleColor();
                ImGui::Spacing();
                if (primaryButton("Open developer tools", ImVec2(0.0f, 36.0f))) {
                    navigateTo(ScreenId::VivaTools);
                }
            }
        }
        endCard();
    }
}

// =============================================================================
// 6. DEVELOPER & EVALUATION TOOLS: X-RAY, MICRO-BENCHMARKS, VISUALISER & SELF-TEST
// =============================================================================
/*
 * Purpose: Renders the Developer & Evaluation Tools screen with 5 tabs:
 *          0. Sample Data & Guided Tour
 *          1. Algorithm X-Ray (live metrics table + Admin Recommender weight sliders)
 *          2. Micro-Benchmarks (MergeSort vs QuickSort vs std::sort on 100/1,000/10,000 ints + ImPlot bar chart)
 *          3. Step-by-Step Visualiser (animated MergeSort & Min-Heap bars)
 *          4. Automated Self-Test Suite (16 checks with PASS/FAIL status).
 * Inputs: None.
 * Outputs: Renders Developer & Evaluation Tools screen.
 * Big-O: O(1) per frame; O(N log N) when Benchmark button is clicked.
 */
void GuiRenderer::renderVivaToolsScreen() {
    const ThemePalette& pal = m_theme.getActivePalette();
    drawSectionHeader(
        "Developer & Evaluation Tools",
        "Sample data controls, algorithm telemetry, sorting benchmarks, visualiser and automated self-tests."
    );

    if (ImGui::BeginTabBar("##DevToolTabs")) {
        // TAB 0: Sample Data, Reset to Empty System, Night Audit & 90s Guided Tour
        if (ImGui::BeginTabItem("Sample Data & Guided Tour", nullptr, (m_forcedVivaTab == 0) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
            if (ImGui::BeginTable("##DevSeederGrid", 2, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (beginCard("##SeederCard", "Sample Data", ImVec2(0.0f, 0.0f), true)) {
                    ImGui::TextWrapped(
                        "Loads 50 rooms, 38 guests, 90 days of occupancy and invoice history, 11 housekeeping and maintenance tasks, "
                        "4 service orders, 2 waitlist entries and 67 verified audit log records."
                    );
                    ImGui::Spacing();
                    if (primaryButton("Load Sample Data", ImVec2(0.0f, 38.0f))) {
                        m_adminOpsState.openSeedConfirmModal = true;
                    }
                    ImGui::SameLine();
                    if (dangerButton("Reset to Empty System", ImVec2(0.0f, 38.0f))) {
                        m_adminOpsState.openResetStep1Modal = true;
                    }
                    ImGui::Spacing();
                    if (ghostButton("Run night audit", ImVec2(0.0f, 34.0f))) {
                        m_system.runNightAudit(&m_adminOpsState.lastNightAuditPath, &m_adminOpsState.lastNightAuditSummary);
                    }
                }
                endCard();

                ImGui::TableSetColumnIndex(1);
                if (beginCard("##GuidedDemoCard", "Guided Tour (90s)", ImVec2(0.0f, 0.0f), true)) {
                    ImGui::TextWrapped(
                        "Runs an automated 90-second walkthrough across the Dashboard, Room Matrix, Smart Check-In, "
                        "Pricing Simulator, Housekeeping Queue and Audit Log."
                    );
                    ImGui::Spacing();
                    if (!m_demoState.active) {
                        if (primaryButton("Start Guided Tour", ImVec2(0.0f, 40.0f))) {
                            startGuidedDemo();
                        }
                    } else {
                        if (dangerButton("Stop Guided Tour", ImVec2(0.0f, 40.0f))) {
                            m_demoState.active = false;
                            Toast::push(NoticeType::Info, "Guided tour stopped", "Returned to manual control.");
                        }
                        ImGui::SameLine();
                        ImGui::TextColored(pal.goldHighlight, "Elapsed: %.0fs / 90s", m_demoState.elapsed);
                    }
                }
                endCard();
                ImGui::EndTable();
            }

            // Confirm Seed Modal
            if (m_adminOpsState.openSeedConfirmModal) {
                ImGui::OpenPopup("##ConfirmSeedModal");
                m_adminOpsState.openSeedConfirmModal = false;
            }
            ImGui::SetNextWindowSize(ImVec2(450.0f, 0.0f), ImGuiCond_Appearing);
            if (ImGui::BeginPopupModal("##ConfirmSeedModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextColored(pal.accent, "Load Sample Data");
                ImGui::Separator();
                ImGui::TextWrapped("A backup snapshot will be saved first, then hotel data will be replaced with the sample dataset. Proceed?");
                ImGui::Spacing();
                if (primaryButton("Backup and load sample data", ImVec2(0.0f, 36.0f))) {
                    m_system.seedVivaDemoData();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ghostButton("Cancel", ImVec2(0.0f, 36.0f))) { ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }

            // Double-Confirm Reset to Empty System Modals (Step 1 -> Step 2)
            if (m_adminOpsState.openResetStep1Modal) {
                ImGui::OpenPopup("##ResetConfirmStep1");
                m_adminOpsState.openResetStep1Modal = false;
            }
            ImGui::SetNextWindowSize(ImVec2(450.0f, 0.0f), ImGuiCond_Appearing);
            if (ImGui::BeginPopupModal("##ResetConfirmStep1", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextColored(pal.statusReserved, "Reset to Empty System — Confirmation 1 of 2");
                ImGui::Separator();
                ImGui::TextWrapped("This will clear all guests, bookings, invoices, tasks and statistics, leaving 50 empty rooms and 3 default staff accounts.");
                ImGui::Spacing();
                if (dangerButton("Continue to Final Confirm ->", ImVec2(0.0f, 36.0f))) {
                    ImGui::CloseCurrentPopup();
                    m_adminOpsState.openResetStep2Modal = true;
                }
                ImGui::SameLine();
                if (ghostButton("Cancel", ImVec2(0.0f, 36.0f))) { ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }

            if (m_adminOpsState.openResetStep2Modal) {
                ImGui::OpenPopup("##ResetConfirmStep2");
                m_adminOpsState.openResetStep2Modal = false;
            }
            ImGui::SetNextWindowSize(ImVec2(450.0f, 0.0f), ImGuiCond_Appearing);
            if (ImGui::BeginPopupModal("##ResetConfirmStep2", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextColored(pal.statusOccupied, "Reset to Empty System — Confirmation 2 of 2");
                ImGui::Separator();
                ImGui::TextWrapped("Are you sure you want to clear all hotel data and reset to an empty system?");
                ImGui::Spacing();
                if (dangerButton("Yes, Wipe & Reset Now", ImVec2(0.0f, 36.0f))) {
                    m_system.resetCleanState();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ghostButton("Abort", ImVec2(0.0f, 36.0f))) { ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }

            ImGui::EndTabItem();
        }

        // TAB 1: Algorithm X-Ray Live Telemetry + Adjustable Recommender Weights
        if (ImGui::BeginTabItem("Algorithm X-Ray", nullptr, (m_forcedVivaTab == 1) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
            const auto& entries = AlgoXRayRegistry::instance().entries();
            const float xrayH = std::max(220.0f, ImGui::GetContentRegionAvail().y * 0.52f);
            if (beginCard("##XRayTelemetryCard", "Live Algorithm Telemetry (Last Run per Algorithm)", ImVec2(0.0f, xrayH))) {
                if (ImGui::BeginTable("##XRayTbl", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY)) {
                    ImGui::TableSetupColumn("Algorithm", ImGuiTableColumnFlags_WidthFixed, 210.0f);
                    ImGui::TableSetupColumn("Input N", ImGuiTableColumnFlags_WidthFixed, 72.0f);
                    ImGui::TableSetupColumn("Comps / Swaps", ImGuiTableColumnFlags_WidthFixed, 125.0f);
                    ImGui::TableSetupColumn("Time (us)", ImGuiTableColumnFlags_WidthFixed, 85.0f);
                    ImGui::TableSetupColumn("Theoretical Big-O", ImGuiTableColumnFlags_WidthFixed, 210.0f);
                    ImGui::TableSetupColumn("Source Location");
                    ImGui::TableHeadersRow();
                    for (const AlgoTelemetryEntry& e : entries) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0); ImGui::TextColored(pal.accent, "%s", e.algorithmName.c_str());
                        ImGui::TableSetColumnIndex(1); ImGui::Text("%zu", e.inputSize);
                        ImGui::TableSetColumnIndex(2); ImGui::Text("%lld / %lld", e.comparisons, e.swapsOrMoves);
                        ImGui::TableSetColumnIndex(3); ImGui::Text("%.1f us", e.elapsedMicros);
                        ImGui::TableSetColumnIndex(4); ImGui::TextColored(pal.secondaryAccent, "%s", e.bigOComplexity.c_str());
                        ImGui::TableSetColumnIndex(5); ImGui::TextColored(pal.dimText, "%s", e.sourceLocation.c_str());
                    }
                    ImGui::EndTable();
                }
            }
            endCard();
            ImGui::Spacing();

            // Admin-adjustable Smart Room Recommender weights (Section 9.7)
            if (beginCard("##RecWeightsCard", "Smart Room Recommender Feature Weights (Sum = 1.00)", ImVec2(0.0f, 0.0f), true)) {
                RecommenderWeights& w = m_system.recommenderWeights();
                float wb = static_cast<float>(w.budget), wf = static_cast<float>(w.floor), wv = static_cast<float>(w.view);
                float we = static_cast<float>(w.elevator), wn = static_cast<float>(w.noise), wbed = static_cast<float>(w.bed);
                bool changed = false;
                if (ImGui::BeginTable("##WSliders", 2, ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    changed |= ImGui::SliderFloat("Budget Weight", &wb, 0.05f, 0.60f, "%.2f");
                    changed |= ImGui::SliderFloat("Floor Weight", &wf, 0.05f, 0.50f, "%.2f");
                    changed |= ImGui::SliderFloat("View Weight", &wv, 0.05f, 0.50f, "%.2f");
                    ImGui::TableSetColumnIndex(1);
                    changed |= ImGui::SliderFloat("Elevator Weight", &we, 0.05f, 0.50f, "%.2f");
                    changed |= ImGui::SliderFloat("Noise Weight", &wn, 0.05f, 0.50f, "%.2f");
                    changed |= ImGui::SliderFloat("Bed Weight", &wbed, 0.05f, 0.50f, "%.2f");
                    ImGui::EndTable();
                }
                if (changed) {
                    w.budget = wb; w.floor = wf; w.view = wv; w.elevator = we; w.noise = wn; w.bed = wbed;
                    w.normalize();
                }
                if (ghostButton("Reset Default Weights (0.30/0.15/0.20/0.10/0.10/0.15)", ImVec2(0.0f, 32.0f))) {
                    w = RecommenderWeights{};
                }
            }
            endCard();
            ImGui::EndTabItem();
        }

        // TAB 2: Sorting Micro-Benchmarks (100 / 1,000 / 10,000)
        if (ImGui::BeginTabItem("Sorting Micro-Benchmark", nullptr, (m_forcedVivaTab == 2) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
            if (primaryButton("Run MergeSort vs QuickSort vs std::sort (100 / 1,000 / 10,000)", ImVec2(0.0f, 38.0f))) {
                m_xrayState.benchmarks = runSortingMicroBenchmarks();
                Toast::push(NoticeType::Success, "Benchmark Complete", "Verified exact match with std::sort across all 3 sizes.");
            }
            ImGui::Spacing();
            if (!m_xrayState.benchmarks.empty()) {
                double xs[3] = {1.0, 2.0, 3.0};
                double mUs[3]{}, qUs[3]{}, sUs[3]{};
                for (std::size_t i = 0U; i < std::min<std::size_t>(3U, m_xrayState.benchmarks.size()); ++i) {
                    mUs[i] = m_xrayState.benchmarks[i].mergeSortMicros;
                    qUs[i] = m_xrayState.benchmarks[i].quickSortMicros;
                    sUs[i] = m_xrayState.benchmarks[i].stdSortMicros;
                }
                const float benchCardH = std::max(280.0f, ImGui::GetContentRegionAvail().y - 4.0f);
                if (beginCard("##BenchPlotCard", "Elapsed Microseconds (Lower is Faster)", ImVec2(0.0f, benchCardH))) {
                    const float benchPlotH = std::max(180.0f, ImGui::GetContentRegionAvail().y - 4.0f);
                    if (ImPlot::BeginPlot("##SortBenchBars", ImVec2(-1.0f, benchPlotH))) {
                        ImPlot::SetupAxes("Dataset (1=100, 2=1,000, 3=10,000)", "Microseconds (us)");
                        ImPlot::SetNextFillStyle(pal.accent, 0.85f);
                        ImPlot::PlotBars("Custom MergeSort", xs, mUs, 3, 0.22, 0, -0.24);
                        ImPlot::SetNextFillStyle(pal.secondaryAccent, 0.85f);
                        ImPlot::PlotBars("Custom QuickSort", xs, qUs, 3, 0.22, 0, 0.0);
                        ImPlot::SetNextFillStyle(pal.goldHighlight, 0.85f);
                        ImPlot::PlotBars("STL std::sort", xs, sUs, 3, 0.22, 0, 0.24);
                        ImPlot::EndPlot();
                    }
                }
                endCard();
            } else {
                drawEmptyState("No Benchmark Run Yet", "Click the button above to compare MergeSort, QuickSort, and std::sort.");
            }
            ImGui::EndTabItem();
        }

        // TAB 3: Step-by-Step Animated MergeSort / Min-Heap Visualiser
        if (ImGui::BeginTabItem("Step-by-Step Visualiser", nullptr, (m_forcedVivaTab == 3) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
            if (m_xrayState.visSteps.empty()) {
                m_xrayState.visSteps = recordMergeSortSteps({64, 25, 82, 12, 47, 91, 33, 58, 19, 74, 41, 68});
            }
            if (ghostButton("Load MergeSort Demo", ImVec2(0.0f, 34.0f))) {
                m_xrayState.visMode = 0;
                m_xrayState.visSteps = recordMergeSortSteps({64, 25, 82, 12, 47, 91, 33, 58, 19, 74, 41, 68});
                m_xrayState.visStepIdx = 0;
                m_xrayState.visPlaying = false;
            }
            ImGui::SameLine();
            if (ghostButton("Load Min-Heap Demo", ImVec2(0.0f, 34.0f))) {
                m_xrayState.visMode = 1;
                m_xrayState.visSteps = recordMinHeapSteps({42, 18, 65, 9, 31, 5, 27, 14, 52, 3});
                m_xrayState.visStepIdx = 0;
                m_xrayState.visPlaying = false;
            }
            ImGui::SameLine();
            if (primaryButton(m_xrayState.visPlaying ? "Pause" : "Auto-Play", ImVec2(0.0f, 34.0f))) {
                m_xrayState.visPlaying = !m_xrayState.visPlaying;
            }
            ImGui::SameLine();
            if (ghostButton("<- Step", ImVec2(0.0f, 34.0f), m_xrayState.visStepIdx > 0)) {
                --m_xrayState.visStepIdx;
            }
            ImGui::SameLine();
            if (ghostButton("Step ->", ImVec2(0.0f, 34.0f), m_xrayState.visStepIdx + 1 < static_cast<int>(m_xrayState.visSteps.size()))) {
                ++m_xrayState.visStepIdx;
            }

            if (m_xrayState.visPlaying) {
                m_xrayState.visStepTimer += ImGui::GetIO().DeltaTime;
                if (m_xrayState.visStepTimer >= 0.35f) {
                    m_xrayState.visStepTimer = 0.0f;
                    if (m_xrayState.visStepIdx + 1 < static_cast<int>(m_xrayState.visSteps.size())) {
                        ++m_xrayState.visStepIdx;
                    } else {
                        m_xrayState.visPlaying = false;
                    }
                }
            }

            ImGui::Spacing();
            if (beginCard("##VisCanvas", "Animated Algorithm State", ImVec2(0.0f, 0.0f), true)) {
                const SortStepSnapshot& snap = m_xrayState.visSteps[static_cast<std::size_t>(m_xrayState.visStepIdx)];
                ImGui::TextColored(pal.secondaryAccent, "Step %d / %zu — %s", m_xrayState.visStepIdx + 1, m_xrayState.visSteps.size(), snap.description.c_str());
                ImGui::Spacing();

                const ImVec2 origin = ImGui::GetCursorScreenPos();
                const float canvasW = std::max(300.0f, ImGui::GetContentRegionAvail().x);
                constexpr float canvasH = 210.0f;
                ImDrawList* dl = ImGui::GetWindowDrawList();

                const int count = std::max(1, static_cast<int>(snap.values.size()));
                const float barW = std::min(56.0f, (canvasW - 20.0f) / static_cast<float>(count) - 8.0f);
                int maxVal = 1;
                for (int v : snap.values) { maxVal = std::max(maxVal, v); }

                for (int i = 0; i < static_cast<int>(snap.values.size()); ++i) {
                    const float h = (static_cast<float>(snap.values[static_cast<std::size_t>(i)]) / static_cast<float>(maxVal)) * (canvasH - 36.0f);
                    const float bx = origin.x + 10.0f + static_cast<float>(i) * (barW + 8.0f);
                    const float by = origin.y + canvasH - h - 22.0f;
                    const bool isHi = (i == snap.highlightA || i == snap.highlightB);
                    const ImVec4 bCol = isHi ? pal.goldHighlight : pal.accent;
                    dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + barW, origin.y + canvasH - 22.0f), ThemeEngine::vec4ToU32(bCol), 6.0f);
                    char vBuf[16]{};
                    std::snprintf(vBuf, sizeof(vBuf), "%d", snap.values[static_cast<std::size_t>(i)]);
                    dl->AddText(ImVec2(bx + barW * 0.25f, origin.y + canvasH - 18.0f), ThemeEngine::vec4ToU32(pal.text), vBuf);
                }
                ImGui::Dummy(ImVec2(canvasW, canvasH));
            }
            endCard();
            ImGui::EndTabItem();
        }

        // TAB 4: Startup Self-Test (Section 11)
        if (ImGui::BeginTabItem("Self-Test Suite", nullptr, (m_forcedVivaTab == 4) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
            if (primaryButton("Re-run all self-tests", ImVec2(0.0f, 34.0f))) {
                m_startupSelfTests = runAllSelfTests(false);
                const auto p3 = HotelSystem::runPhase3SelfTests(false);
                m_startupSelfTests.insert(m_startupSelfTests.end(), p3.begin(), p3.end());
                Toast::push(NoticeType::Success, "Self-test complete", "All " + std::to_string(m_startupSelfTests.size()) + " checks executed.");
            }
            ImGui::Spacing();
            if (beginCard("##SelfTestDevCard", "Automated verification checks", ImVec2(0.0f, 0.0f))) {
                if (ImGui::BeginTable("##DevSelfTestTbl", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY)) {
                    ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                    ImGui::TableSetupColumn("Test Suite", ImGuiTableColumnFlags_WidthFixed, 300.0f);
                    ImGui::TableSetupColumn("Details");
                    ImGui::TableHeadersRow();
                    for (const SelfTestEntry& st : m_startupSelfTests) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        drawBadge(st.passed ? "PASS" : "FAIL", st.passed ? pal.statusAvailable : pal.statusOccupied);
                        ImGui::TableSetColumnIndex(1); ImGui::Text("%s", st.name.c_str());
                        ImGui::TableSetColumnIndex(2); ImGui::TextColored(pal.dimText, "%s", st.details.c_str());
                    }
                    ImGui::EndTable();
                }
            }
            endCard();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

// =============================================================================
// 7. COMMAND PALETTE OVERLAY (Ctrl+K, Levenshtein Fuzzy Ranking, Section 9.10 #1)
// =============================================================================
/*
 * Purpose: Renders the centered 600px Command Palette overlay triggered by Ctrl+K:
 *          fuzzy searches navigation targets, guests (name/CNIC), rooms (number), and quick actions,
 *          ranked via custom Levenshtein distance + prefix/substring bonus, with Up/Down/Enter/Esc controls
 *          and smooth fade/scale animation.
 * Inputs: deltaTime.
 * Outputs: Renders overlay and runs selected command on Enter/click.
 * Big-O: O(C * L + C log C) where C is candidate count.
 */
void GuiRenderer::renderCommandPaletteOverlay(float deltaTime) {
    const float animStep = deltaTime / 0.16f;
    m_cmdPalette.openAnimT = std::clamp(m_cmdPalette.openAnimT + (m_cmdPalette.isOpen ? animStep : -animStep), 0.0f, 1.0f);
    if (m_cmdPalette.openAnimT <= 0.01f) {
        return;
    }

    const ThemePalette& pal = m_theme.getActivePalette();
    const ImGuiViewport* vp = ImGui::GetMainViewport();

    struct PaletteEntry {
        std::string           category;
        std::string           label;
        int                   score;
        std::function<void()> run;
    };

    std::vector<PaletteEntry> candidates;
    const std::string q = m_cmdPalette.queryBuf;

    // 1. Navigation screens permitted for current role
    std::vector<NavItemSpec> navs;
    renderRoleAllowedNavItems(m_system.currentRole(), navs);
    for (const NavItemSpec& n : navs) {
        candidates.push_back(PaletteEntry{
            "Screen",
            std::string("Go to ") + n.label,
            computeFuzzyRankScore(q, n.label),
            [this, scr = n.screen]() { navigateTo(scr); }
        });
    }

    // 2. Quick Actions
    candidates.push_back(PaletteEntry{
        "Action",
        "Toggle Theme (Midnight Obsidian / Executive Pearl)",
        computeFuzzyRankScore(q, "Toggle theme"),
        [this]() { m_theme.toggleTheme(); }
    });
    if (HotelSystem::can(m_system.currentRole(), Action::SmartCheckIn)) {
        candidates.push_back(PaletteEntry{
            "Action",
            "Check in new guest",
            computeFuzzyRankScore(q, "Check in"),
            [this]() { navigateTo(ScreenId::SmartCheckIn); }
        });
    }
    if (HotelSystem::can(m_system.currentRole(), Action::UseVivaTools)) {
        candidates.push_back(PaletteEntry{
            "Screen",
            "Developer & Evaluation Tools",
            computeFuzzyRankScore(q, "Developer Evaluation Tools X-Ray"),
            [this]() { navigateTo(ScreenId::VivaTools); }
        });
        candidates.push_back(PaletteEntry{
            "Action",
            "Load sample data",
            computeFuzzyRankScore(q, "Load sample data seed"),
            [this]() {
                navigateTo(ScreenId::VivaTools);
                m_system.seedVivaDemoData();
            }
        });
        candidates.push_back(PaletteEntry{
            "Action",
            "Start guided tour (90s)",
            computeFuzzyRankScore(q, "Guided tour"),
            [this]() { startGuidedDemo(); }
        });
    }
    if (HotelSystem::can(m_system.currentRole(), Action::ViewAuditLog)) {
        candidates.push_back(PaletteEntry{
            "Action",
            "Verify audit log integrity",
            computeFuzzyRankScore(q, "Verify audit log"),
            [this]() {
                navigateTo(ScreenId::AuditLog);
                const int idx = m_system.verifyAuditLogChain();
                Toast::push(idx == -1 ? NoticeType::Success : NoticeType::Error, "Audit verification",
                            idx == -1 ? "All audit entries verified intact." : ("Integrity mismatch at entry #" + std::to_string(idx) + "."));
            }
        });
    }

    // 3. Rooms (by number)
    for (const Room& r : m_system.db().rooms) {
        const std::string lbl = "Room " + std::to_string(r.number) + " (" + toString(r.type) + " · " + toString(r.status) + ")";
        const int sc = computeFuzzyRankScore(q, std::to_string(r.number));
        if (q.empty() || sc >= 140) {
            candidates.push_back(PaletteEntry{
                "Room",
                lbl,
                sc,
                [this, rid = r.id]() {
                    navigateTo(ScreenId::RoomMatrix);
                    m_matrixState.selectedRoomId = rid;
                    m_matrixState.openRoomModalReq = true;
                }
            });
        }
    }

    // 4. Guests (by name or CNIC)
    for (const Guest& g : m_system.db().guests) {
        const int sc = std::max(computeFuzzyRankScore(q, g.fullName), computeFuzzyRankScore(q, g.cnic));
        if (q.empty() || sc >= 130) {
            candidates.push_back(PaletteEntry{
                "Guest",
                g.fullName + " (" + (g.isForeign ? g.passport : g.cnic) + ")",
                sc,
                [this, gid = g.id]() {
                    navigateTo(ScreenId::Guests);
                    m_crmState.selectedGuestId = gid;
                }
            });
        }
    }

    mergeSort(
        candidates,
        [](const PaletteEntry& a, const PaletteEntry& b) { return a.score > b.score; },
        nullptr,
        nullptr,
        "CommandPalette MergeSort"
    );
    if (candidates.size() > 10U) {
        candidates.resize(10U);
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        m_cmdPalette.isOpen = false;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false) && !candidates.empty()) {
        m_cmdPalette.selectedIndex = (m_cmdPalette.selectedIndex + 1) % static_cast<int>(candidates.size());
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false) && !candidates.empty()) {
        m_cmdPalette.selectedIndex = (m_cmdPalette.selectedIndex - 1 + static_cast<int>(candidates.size())) % static_cast<int>(candidates.size());
    }
    m_cmdPalette.selectedIndex = candidates.empty()
                                     ? 0
                                     : std::clamp(m_cmdPalette.selectedIndex, 0, static_cast<int>(candidates.size()) - 1);

    const float scale = 0.94f + 0.06f * m_cmdPalette.openAnimT;
    const float w = 600.0f * scale;
    const float h = 420.0f * scale;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + (vp->Size.x - w) * 0.5f, vp->Pos.y + 96.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_cmdPalette.openAnimT);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, pal.panel);
    ImGui::PushStyleColor(ImGuiCol_Border, pal.accent);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##CommandPaletteOverlay", nullptr, flags)) {
        if (m_cmdPalette.focusInput) {
            ImGui::SetKeyboardFocusHere();
            m_cmdPalette.focusInput = false;
        }
        ImGui::SetNextItemWidth(-1.0f);
        const bool enterHit = ImGui::InputTextWithHint(
            "##CmdPalInput",
            "Type a screen, room #, guest name/CNIC, or action... (Esc to close)",
            m_cmdPalette.queryBuf,
            sizeof(m_cmdPalette.queryBuf),
            ImGuiInputTextFlags_EnterReturnsTrue
        );
        ImGui::Separator();

        for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
            const PaletteEntry& c = candidates[static_cast<std::size_t>(i)];
            const bool isSel = (i == m_cmdPalette.selectedIndex);
            drawBadge(c.category.c_str(), isSel ? pal.accent : pal.surface, isSel ? ImVec4(1, 1, 1, 1) : pal.dimText);
            ImGui::SameLine();
            char itemLbl[160]{};
            std::snprintf(itemLbl, sizeof(itemLbl), "%s##CmdItem%d", c.label.c_str(), i);
            if (ImGui::Selectable(itemLbl, isSel)) {
                if (c.run) { c.run(); }
                m_cmdPalette.isOpen = false;
            }
        }

        if (enterHit && !candidates.empty()) {
            if (candidates[static_cast<std::size_t>(m_cmdPalette.selectedIndex)].run) {
                candidates[static_cast<std::size_t>(m_cmdPalette.selectedIndex)].run();
            }
            m_cmdPalette.isOpen = false;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

// =============================================================================
// 8. KEYBOARD SHORTCUTS OVERLAY (F1, Section 9.10 #8)
// =============================================================================
/*
 * Purpose: Renders the F1 Keyboard Shortcuts Cheat Sheet modal overlay.
 * Inputs: None.
 * Outputs: Renders cheat sheet overlay when m_showShortcutsOverlay is true.
 * Big-O: O(1) time, O(1) space.
 */
void GuiRenderer::renderShortcutsOverlay() {
    if (!m_showShortcutsOverlay) {
        return;
    }
    const ThemePalette& pal = m_theme.getActivePalette();
    const ImGuiViewport* vp = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(580.0f, 440.0f), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, pal.panel);
    ImGui::PushStyleColor(ImGuiCol_Border, pal.accent);

    if (ImGui::Begin("Keyboard shortcuts (F1)", &m_showShortcutsOverlay, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {
        const char* keys[9] = {"Ctrl + K", "Ctrl + T", "Ctrl + = / -", "Ctrl + Shift + D", "Ctrl + Z", "Ctrl + Y", "F1", "Esc", "Up / Down / Enter"};
        const char* desc[9] = {
            "Open quick search and command palette",
            "Switch light and dark theme",
            "Increase or decrease text size (90% to 130%)",
            "Open Developer & Evaluation Tools (Admin)",
            "Undo last room status, swap, charge or date edit",
            "Redo last reverted action",
            "Show or hide this shortcuts list",
            "Close active overlay or dialog",
            "Navigate and run command palette results"
        };

        if (ImGui::BeginTable("##ShortcutsTbl", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 175.0f);
            ImGui::TableSetupColumn("Action");
            ImGui::TableHeadersRow();
            for (int i = 0; i < 9; ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); drawBadge(keys[i], pal.accent);
                ImGui::TableSetColumnIndex(1); ImGui::Text("%s", desc[i]);
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        if (primaryButton("Close (Esc / F1)", ImVec2(-1.0f, 34.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            m_showShortcutsOverlay = false;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
}

// =============================================================================
// 9. GUIDED TOUR MODE (90-SECOND AUTOMATED WALKTHROUGH)
// =============================================================================
/*
 * Purpose: Starts the 90-second Guided Tour auto-walking through core hotel workflows.
 * Inputs: None.
 * Outputs: Activates m_demoState and resets timer.
 * Big-O: O(1) time, O(1) space.
 */
void GuiRenderer::startGuidedDemo() {
    m_demoState.active      = true;
    m_demoState.elapsed     = 0.0f;
    m_demoState.lastStage   = -1;
    m_demoState.captionText = "Starting 90-second Grand Meridian Hotel guided tour...";
    Toast::push(NoticeType::Info, "Guided tour started", "90-second automated tour is running.");
}

/*
 * Purpose: Advances the 90-second Guided Tour state machine and renders a floating bottom caption bar
 *          with progress indicator and [Next step] / [Stop] controls.
 * Inputs: deltaTime.
 * Outputs: Drives automated navigation/actions and renders caption overlay.
 * Big-O: O(1) per frame.
 */
void GuiRenderer::updateAndRenderGuidedDemo(float deltaTime) {
    if (!m_demoState.active || !m_system.isLoggedIn()) {
        return;
    }

    m_demoState.elapsed += deltaTime;
    const int stage = static_cast<int>(m_demoState.elapsed / 12.85f); // 7 stages x ~12.85s = 90s

    if (stage != m_demoState.lastStage) {
        m_demoState.lastStage = stage;
        switch (stage) {
            case 0:
                m_demoState.captionText = "Step 1/7 — Sample data: loading 50 rooms, 38 guests, 90 days of history and audit records.";
                if (HotelSystem::can(m_system.currentRole(), Action::UseVivaTools)) {
                    m_system.seedVivaDemoData();
                }
                navigateTo(ScreenId::Dashboard);
                break;
            case 1:
                m_demoState.captionText = "Step 2/7 — Dashboard: live occupancy, daily revenue, ADR, RevPAR and 7-day forecast.";
                navigateTo(ScreenId::Dashboard);
                break;
            case 2:
                m_demoState.captionText = "Step 3/7 — Room matrix: five floors with live status, VIP badges and room actions.";
                navigateTo(ScreenId::RoomMatrix);
                break;
            case 3:
                m_demoState.captionText = "Step 4/7 — Smart check-in: guest lookup and ranked room recommendations.";
                if (!m_system.db().guests.empty()) {
                    m_wizardState = SmartCheckInState{};
                    m_wizardState.selectedGuestId = m_system.db().guests.front().id;
                    m_wizardState.step = 2; // Show Top-3 Recommendation cards directly
                }
                navigateTo(ScreenId::SmartCheckIn);
                break;
            case 4:
                m_demoState.captionText = "Step 5/7 — Pricing simulator: occupancy-based rates and expected nightly revenue curve.";
                m_yieldState.occSliderPct = 88.0f;
                m_yieldState.weekendNight = true;
                navigateTo(ScreenId::YieldManagement);
                break;
            case 5:
                m_demoState.captionText = "Step 6/7 — Housekeeping: cleaning and maintenance tasks ordered by urgency.";
                navigateTo(ScreenId::HousekeepingBoard);
                break;
            case 6:
                m_demoState.captionText = "Step 7/7 — Audit log: verifying every recorded staff action from start to latest.";
                navigateTo(ScreenId::AuditLog);
                m_system.verifyAuditLogChain();
                break;
            default:
                m_demoState.active = false;
                Toast::push(NoticeType::Success, "Guided tour complete", "All core hotel views were shown.");
                return;
        }
    }

    const ThemePalette& pal = m_theme.getActivePalette();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    constexpr float kBarW = 860.0f;
    constexpr float kBarH = 74.0f;

    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + (vp->Size.x - kBarW) * 0.5f, vp->Pos.y + vp->Size.y - kBarH - 38.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kBarW, kBarH), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(pal.panel.x, pal.panel.y, pal.panel.z, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_Border, pal.goldHighlight);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##GuidedDemoCaptionBar", nullptr, flags)) {
        drawBadge("GUIDED TOUR", pal.goldHighlight, ImVec4(0.08f, 0.08f, 0.10f, 1.0f));
        ImGui::SameLine();
        ImGui::TextColored(pal.text, "%s", m_demoState.captionText.c_str());

        const float prog = std::clamp(m_demoState.elapsed / 90.0f, 0.0f, 1.0f);
        ImGui::ProgressBar(prog, ImVec2(kBarW - 235.0f, 12.0f), "");
        ImGui::SameLine();
        if (ghostButton("Next Step", ImVec2(0.0f, 28.0f))) {
            m_demoState.elapsed = static_cast<float>(stage + 1) * 12.85f + 0.05f;
        }
        ImGui::SameLine();
        if (dangerButton("Stop", ImVec2(0.0f, 28.0f))) {
            m_demoState.active = false;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

} // namespace hms
