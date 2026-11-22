#include "GuiRenderer.h"
#include "Sha256.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace hms {

namespace {

/*
 * Purpose: Case-insensitive substring check for CRM, Bookings, and Billing search boxes.
 * Inputs: haystack, needle.
 * Outputs: true if needle matches haystack case-insensitively.
 * Big-O: O(|haystack| * |needle|).
 */
bool ciContains(const std::string& haystack, const std::string& needle) {
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
 * Purpose: Opens a local HTML file in the platform default web browser per Section 9.5:
 *          ShellExecuteA on Windows, open on macOS, xdg-open on Linux.
 * Inputs: filePath (relative or absolute path to .html file).
 * Outputs: Launches browser process asynchronously.
 * Big-O: O(1) time, O(1) space.
 */
void openFileInDefaultBrowser(const std::string& filePath) {
    std::error_code ec;
    const std::string absPath = std::filesystem::absolute(filePath, ec).string();
    const std::string target = ec ? filePath : absPath;
#if defined(_WIN32)
    ShellExecuteA(nullptr, "open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    const std::string cmd = "open \"" + target + "\" &";
    std::system(cmd.c_str());
#else
    const std::string cmd = "xdg-open \"" + target + "\" &";
    std::system(cmd.c_str());
#endif
}

/*
 * Purpose: Maps BookingStatus to an accent color for Gantt timeline bars and status badges.
 * Inputs: status, palette.
 * Outputs: ImVec4 color.
 * Big-O: O(1) time, O(1) space.
 */
ImVec4 bookingStatusColor(BookingStatus status, const ThemePalette& pal) {
    switch (status) {
        case BookingStatus::Reserved:   return pal.statusReserved;
        case BookingStatus::CheckedIn:  return pal.statusOccupied;
        case BookingStatus::CheckedOut: return pal.statusAvailable;
        case BookingStatus::Cancelled:  return pal.statusMaintenance;
        case BookingStatus::NoShow:     return pal.statusCleaning;
    }
    return pal.accent;
}

} // namespace

// =============================================================================
// 1. HORIZONTAL GANTT TIMELINE (50 ROOMS x 30 DAYS, DRAG-EXTEND & SWAP, Section 9.6)
// =============================================================================
/*
 * Purpose: Renders the interactive horizontal Gantt Timeline (50 rooms grouped by collapsible floors 5..1,
 *          30-day window with scroll/zoom). Supports dragging the right edge of a booking bar to extend/shorten
 *          the stay and dragging a bar onto another room row to swap rooms. Drops that create a conflict flash red
 *          and are rejected via binary-search half-open interval overlap detection (Section 9.6).
 * Inputs: None (operates on m_timelineState & m_system).
 * Outputs: Renders Timeline screen and applies conflict-free edits through HotelSystem.
 * Big-O: O(R * B) for visible window drawing.
 */
void GuiRenderer::renderTimelineScreen() {
    const ThemePalette& pal = m_theme.getActivePalette();
    const HotelDatabase& db = m_system.db();
    const int32_t today = DateUtil::todayDayNumber();

    if (m_timelineState.conflictFlashTimer > 0.0f) {
        m_timelineState.conflictFlashTimer = std::max(0.0f, m_timelineState.conflictFlashTimer - ImGui::GetIO().DeltaTime);
    }

    drawSectionHeader(
        "Booking Timeline (Gantt Chart)",
        "50 Rooms x 30 Days · Drag bar to another room row to swap · Drag right edge to extend/shorten stay"
    );

    // Controls bar: Scroll window, Zoom, Today button
    if (beginCard("##TimelineControls", nullptr, ImVec2(0.0f, 66.0f))) {
        if (ghostButton("<< -7d", ImVec2(76.0f, 32.0f))) { m_timelineState.startDayOffset -= 7; }
        ImGui::SameLine();
        if (ghostButton("< -1d", ImVec2(66.0f, 32.0f))) { m_timelineState.startDayOffset -= 1; }
        ImGui::SameLine();
        if (primaryButton("Today", ImVec2(76.0f, 32.0f))) { m_timelineState.startDayOffset = -3; }
        ImGui::SameLine();
        if (ghostButton("+1d >", ImVec2(66.0f, 32.0f))) { m_timelineState.startDayOffset += 1; }
        ImGui::SameLine();
        if (ghostButton("+7d >>", ImVec2(76.0f, 32.0f))) { m_timelineState.startDayOffset += 7; }

        ImGui::SameLine(0.0f, 22.0f);
        ImGui::SetNextItemWidth(160.0f);
        ImGui::SliderFloat("Zoom (px/day)", &m_timelineState.dayColumnWidth, 24.0f, 64.0f, "%.0f px");

        ImGui::SameLine(0.0f, 20.0f);
        drawStatusDot(pal.statusReserved); ImGui::SameLine(); ImGui::Text("Reserved");
        ImGui::SameLine(0.0f, 12.0f);
        drawStatusDot(pal.statusOccupied); ImGui::SameLine(); ImGui::Text("Checked In");
        ImGui::SameLine(0.0f, 12.0f);
        drawStatusDot(pal.statusAvailable); ImGui::SameLine(); ImGui::Text("Checked Out");
    }
    endCard();
    ImGui::Spacing();

    const int32_t viewStartDay = today + m_timelineState.startDayOffset;
    constexpr int kDaysVisible = 30;
    const float dayW = m_timelineState.dayColumnWidth;
    constexpr float kRoomColW = 128.0f;
    constexpr float kRowH     = 30.0f;

    if (beginCard("##TimelineCanvas", nullptr, ImVec2(0.0f, 0.0f))) {
        ImGui::BeginChild("##TimelineScroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float totalGridW = kRoomColW + static_cast<float>(kDaysVisible) * dayW;

        // Draw 30-day header strip
        dl->AddRectFilled(
            origin,
            ImVec2(origin.x + totalGridW, origin.y + 34.0f),
            ThemeEngine::vec4ToU32(pal.surface),
            6.0f
        );
        dl->AddText(ImVec2(origin.x + 12.0f, origin.y + 9.0f), ThemeEngine::vec4ToU32(pal.dimText), "ROOM / FLOOR");

        for (int d = 0; d < kDaysVisible; ++d) {
            const int32_t dayNum = viewStartDay + d;
            const float x = origin.x + kRoomColW + static_cast<float>(d) * dayW;
            const bool isTodayCol = (dayNum == today);
            if (isTodayCol) {
                dl->AddRectFilled(
                    ImVec2(x, origin.y),
                    ImVec2(x + dayW, origin.y + 34.0f),
                    ThemeEngine::vec4ToU32(ImVec4(pal.accent.x, pal.accent.y, pal.accent.z, 0.28f))
                );
            }
            int y = 0, m = 0, dom = 0;
            DateUtil::fromDayNumber(dayNum, y, m, dom);
            char dayLbl[16]{};
            std::snprintf(dayLbl, sizeof(dayLbl), "%02d/%02d", dom, m);
            if (m_fonts.captionFont != nullptr) {
                dl->AddText(
                    m_fonts.captionFont,
                    11.0f,
                    ImVec2(x + 3.0f, origin.y + 10.0f),
                    ThemeEngine::vec4ToU32(isTodayCol ? pal.accent : pal.dimText),
                    dayLbl
                );
            }
            dl->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + 34.0f), ThemeEngine::vec4ToU32(pal.border), 1.0f);
        }

        ImGui::Dummy(ImVec2(totalGridW, 38.0f));

        // Render Floors 5 down to 1 (collapsible groups)
        for (int floor = Cfg::FLOORS; floor >= 1; --floor) {
            const int fIdx = floor - 1;
            char fHeader[48]{};
            std::snprintf(fHeader, sizeof(fHeader), "%s  Floor %d (Rooms %d01–%d10)##TlFl%d",
                          m_timelineState.floorExpanded[fIdx] ? "[-]" : "[+]", floor, floor, floor, floor);
            if (ghostButton(fHeader, ImVec2(260.0f, 26.0f))) {
                m_timelineState.floorExpanded[fIdx] = !m_timelineState.floorExpanded[fIdx];
            }
            if (!m_timelineState.floorExpanded[fIdx]) {
                continue;
            }

            for (int unit = 1; unit <= Cfg::ROOMS_PER_FLOOR; ++unit) {
                const Room* room = m_system.findRoomByNumber(floor * 100 + unit);
                if (room == nullptr) {
                    continue;
                }

                const ImVec2 rowPos = ImGui::GetCursorScreenPos();
                // Row background & drop target for Room Swap drag-and-drop
                dl->AddRectFilled(
                    rowPos,
                    ImVec2(rowPos.x + totalGridW, rowPos.y + kRowH - 2.0f),
                    ThemeEngine::vec4ToU32((unit % 2 == 0) ? pal.panel : ImVec4(pal.surface.x, pal.surface.y, pal.surface.z, 0.35f))
                );

                // Vertical day grid lines + today highlight stripe
                for (int d = 0; d < kDaysVisible; ++d) {
                    const float gx = rowPos.x + kRoomColW + static_cast<float>(d) * dayW;
                    if (viewStartDay + d == today) {
                        dl->AddRectFilled(
                            ImVec2(gx, rowPos.y),
                            ImVec2(gx + dayW, rowPos.y + kRowH - 2.0f),
                            ThemeEngine::vec4ToU32(ImVec4(pal.accent.x, pal.accent.y, pal.accent.z, 0.10f))
                        );
                    }
                    dl->AddLine(ImVec2(gx, rowPos.y), ImVec2(gx, rowPos.y + kRowH - 2.0f), ThemeEngine::vec4ToU32(ImVec4(pal.border.x, pal.border.y, pal.border.z, 0.45f)), 1.0f);
                }

                // Room label cell
                char rLbl[32]{};
                std::snprintf(rLbl, sizeof(rLbl), "%d  %s", room->number, toString(room->type));
                dl->AddText(ImVec2(rowPos.x + 8.0f, rowPos.y + 6.0f), ThemeEngine::vec4ToU32(pal.text), rLbl);

                // Invisible row drop target for dragging a booking onto this room
                ImGui::SetCursorScreenPos(rowPos);
                char dropId[32]{};
                std::snprintf(dropId, sizeof(dropId), "##TlRowDrop_%d", room->id);
                ImGui::InvisibleButton(dropId, ImVec2(totalGridW, kRowH - 2.0f));
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HMS_BOOKING_SWAP")) {
                        const int draggedBookingId = *static_cast<const int*>(payload->Data);
                        std::string swapErr;
                        if (!m_system.swapBookingRoom(draggedBookingId, room->id, true, &swapErr)) {
                            m_timelineState.conflictFlashBookingId = draggedBookingId;
                            m_timelineState.conflictFlashTimer = 0.9f;
                            Toast::push(NoticeType::Error, "Swap Conflict Rejected", swapErr);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // Draw all bookings for this room intersecting the 30-day window
                for (const Booking& b : db.bookings) {
                    if (b.roomId != room->id || b.status == BookingStatus::Cancelled || b.status == BookingStatus::NoShow) {
                        continue;
                    }
                    if (!intervalsOverlap(b.checkInDay, b.checkOutDay, viewStartDay, viewStartDay + kDaysVisible)) {
                        continue;
                    }

                    const int visStart = std::max<int>(b.checkInDay, viewStartDay);
                    const int visEnd   = std::min<int>(b.checkOutDay, viewStartDay + kDaysVisible);
                    const float bx0 = rowPos.x + kRoomColW + static_cast<float>(visStart - viewStartDay) * dayW + 2.0f;
                    const float bx1 = rowPos.x + kRoomColW + static_cast<float>(visEnd - viewStartDay) * dayW - 2.0f;
                    const float barW = std::max(16.0f, bx1 - bx0);

                    const bool flashingRed = (m_timelineState.conflictFlashBookingId == b.id && m_timelineState.conflictFlashTimer > 0.0f);
                    const ImVec4 barCol = flashingRed ? pal.statusOccupied : bookingStatusColor(b.status, pal);

                    ImGui::SetCursorScreenPos(ImVec2(bx0, rowPos.y + 3.0f));
                    char barId[32]{};
                    std::snprintf(barId, sizeof(barId), "##TlBar_%d", b.id);
                    ImGui::InvisibleButton(barId, ImVec2(std::max(10.0f, barW - 8.0f), kRowH - 8.0f));

                    // Drag bar to swap rooms
                    if ((b.status == BookingStatus::Reserved || b.status == BookingStatus::CheckedIn) &&
                        ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                        ImGui::SetDragDropPayload("HMS_BOOKING_SWAP", &b.id, sizeof(int));
                        const Guest* g = m_system.findGuestById(b.guestId);
                        ImGui::Text("Swap Booking #%d (%s) -> drop on target room row", b.id, g ? g->fullName.c_str() : "Guest");
                        ImGui::EndDragDropSource();
                    }

                    if (ImGui::IsItemHovered()) {
                        const Guest* g = m_system.findGuestById(b.guestId);
                        ImGui::BeginTooltip();
                        ImGui::TextColored(pal.accent, "Booking #%d — %s", b.id, toString(b.status));
                        ImGui::Text("Guest : %s", g ? g->fullName.c_str() : "Unknown");
                        ImGui::Text("Dates : %s -> %s", DateUtil::formatDate(b.checkInDay).c_str(), DateUtil::formatDate(b.checkOutDay).c_str());
                        ImGui::TextColored(pal.dimText, "Drag bar to swap room; drag right edge to extend/shorten.");
                        ImGui::EndTooltip();
                    }

                    // Right-edge resize handle (8px wide) to extend/shorten stay
                    if (b.checkOutDay <= viewStartDay + kDaysVisible &&
                        (b.status == BookingStatus::Reserved || b.status == BookingStatus::CheckedIn)) {
                        ImGui::SetCursorScreenPos(ImVec2(bx0 + barW - 8.0f, rowPos.y + 3.0f));
                        char rszId[32]{};
                        std::snprintf(rszId, sizeof(rszId), "##TlRsz_%d", b.id);
                        ImGui::InvisibleButton(rszId, ImVec2(8.0f, kRowH - 8.0f));
                        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
                            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                        }
                        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                            m_timelineState.resizingBookingId = b.id;
                        }
                        if (m_timelineState.resizingBookingId == b.id && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                            const float mouseX = ImGui::GetIO().MousePos.x;
                            const int targetOutDay = viewStartDay + static_cast<int>(std::llround((mouseX - (rowPos.x + kRoomColW)) / dayW));
                            const int clampedOut = std::max(b.checkInDay + 1, targetOutDay);
                            if (clampedOut != b.checkOutDay) {
                                std::string editErr;
                                if (!m_system.editBookingDates(b.id, b.checkInDay, clampedOut, true, &editErr)) {
                                    m_timelineState.conflictFlashBookingId = b.id;
                                    m_timelineState.conflictFlashTimer = 0.9f;
                                    Toast::push(NoticeType::Error, "Resize Conflict Rejected", editErr);
                                } else {
                                    Toast::push(NoticeType::Success, "Stay Updated", "Booking #" + std::to_string(b.id) + " checkout -> " + DateUtil::formatDate(clampedOut));
                                }
                            }
                            m_timelineState.resizingBookingId = -1;
                        }
                    }

                    dl->AddRectFilled(
                        ImVec2(bx0, rowPos.y + 3.0f),
                        ImVec2(bx0 + barW, rowPos.y + kRowH - 5.0f),
                        ThemeEngine::vec4ToU32(ImVec4(barCol.x, barCol.y, barCol.z, 0.88f)),
                        6.0f
                    );
                    const Guest* g = m_system.findGuestById(b.guestId);
                    if (g != nullptr && barW > 36.0f && m_fonts.captionFont != nullptr) {
                        dl->PushClipRect(ImVec2(bx0 + 4.0f, rowPos.y + 3.0f), ImVec2(bx0 + barW - 6.0f, rowPos.y + kRowH - 5.0f), true);
                        dl->AddText(m_fonts.captionFont, 12.0f, ImVec2(bx0 + 6.0f, rowPos.y + 7.0f), IM_COL32(255, 255, 255, 255), g->fullName.c_str());
                        dl->PopClipRect();
                    }
                }

                ImGui::SetCursorScreenPos(ImVec2(rowPos.x, rowPos.y + kRowH));
            }
        }
        ImGui::EndChild();
    }
    endCard();
}

// =============================================================================
// 2. GUEST CRM REGISTRY (Sortable via Custom MergeSort, 15/page, Detail Drawer)
// =============================================================================
/*
 * Purpose: Renders the Guest CRM Registry with ImGui Tables API (sortable column headers powered by
 *          custom MergeSort, text filter, 15-row pagination), right-hand Detail Drawer (profile, loyalty
 *          progress bar toward next tier, stay history), and validated [+ Add Guest] modal (Section 9.4).
 * Inputs: None (uses m_crmState).
 * Outputs: Renders CRM screen and registers/updates guests through HotelSystem.
 * Big-O: O(G log G) for MergeSort column sorting.
 */
void GuiRenderer::renderGuestsCrmScreen() {
    const ThemePalette& pal = m_theme.getActivePalette();
    const HotelDatabase& db = m_system.db();

    drawSectionHeader(
        "Guest CRM & Loyalty Registry",
        "Custom MergeSort sortable table · 15 rows/page · Hand-written validators · Custom CNIC Hash Table"
    );

    // Filter & Add bar
    if (beginCard("##CrmTopBar", nullptr, ImVec2(0.0f, 66.0f))) {
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputTextWithHint("##CrmSearch", "Filter by name, CNIC, passport, phone, email...", m_crmState.searchBuf, sizeof(m_crmState.searchBuf));
        ImGui::SameLine();
        if (primaryButton("+ Register Guest", ImVec2(170.0f, 34.0f))) {
            m_crmState.openAddModal = true;
            m_crmState.formName[0] = '\0';
            m_crmState.formCnic[0] = '\0';
            m_crmState.formPassport[0] = '\0';
            m_crmState.formPhone[0] = '\0';
            m_crmState.formEmail[0] = '\0';
            m_crmState.formAddress[0] = '\0';
            m_crmState.formPrefs[0] = '\0';
            m_crmState.formIsForeign = false;
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(pal.dimText, "Total Registered: %zu  |  CNIC Hash Table Entries: %zu", db.guests.size(), m_system.cnicHashTable().size());
    }
    endCard();
    ImGui::Spacing();

    // Build filtered vector and sort via custom MergeSort
    std::vector<const Guest*> filtered;
    filtered.reserve(db.guests.size());
    for (const Guest& g : db.guests) {
        if (ciContains(g.fullName, m_crmState.searchBuf) ||
            ciContains(g.cnic, m_crmState.searchBuf) ||
            ciContains(g.passport, m_crmState.searchBuf) ||
            ciContains(g.phone, m_crmState.searchBuf) ||
            ciContains(g.email, m_crmState.searchBuf)) {
            filtered.push_back(&g);
        }
    }

    const int sortCol = m_crmState.sortColumn;
    const bool sortAsc = m_crmState.sortAscending;
    mergeSort(
        filtered,
        [sortCol, sortAsc](const Guest* a, const Guest* b) {
            bool less = false;
            switch (sortCol) {
                case 0: less = a->id < b->id; break;
                case 1: less = a->fullName < b->fullName; break;
                case 2: less = static_cast<int>(a->tier) < static_cast<int>(b->tier); break;
                case 3: less = a->stayCount < b->stayCount; break;
                case 4: less = a->lifetimeSpend < b->lifetimeSpend; break;
                default: less = a->id < b->id; break;
            }
            return sortAsc ? less : !less;
        },
        nullptr,
        nullptr,
        "CRM Guest Table MergeSort"
    );

    constexpr int kRowsPerPage = 15;
    const int totalPages = std::max(1, (static_cast<int>(filtered.size()) + kRowsPerPage - 1) / kRowsPerPage);
    m_crmState.currentPage = std::clamp(m_crmState.currentPage, 0, totalPages - 1);

    // Split view: Left Table (65%) + Right Detail Drawer (35%)
    if (ImGui::BeginTable("##CrmSplit", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Table", ImGuiTableColumnFlags_WidthStretch, 1.85f);
        ImGui::TableSetupColumn("Drawer", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (beginCard("##CrmTableCard", "Guest Directory (Click Column Headers to MergeSort)", ImVec2(0.0f, 0.0f))) {
            const ImGuiTableFlags tFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                           ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable |
                                           ImGuiTableFlags_ScrollY;
            const float tableH = std::max(160.0f, ImGui::GetContentRegionAvail().y - 46.0f);
            if (ImGui::BeginTable("##GuestsTable", 6, tFlags, ImVec2(0.0f, tableH))) {
                ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 52.0f, 0);
                ImGui::TableSetupColumn("Full Name", ImGuiTableColumnFlags_WidthStretch, 0.0f, 1);
                ImGui::TableSetupColumn("CNIC / Passport", ImGuiTableColumnFlags_NoSort);
                ImGui::TableSetupColumn("Tier", ImGuiTableColumnFlags_WidthFixed, 92.0f, 2);
                ImGui::TableSetupColumn("Stays", ImGuiTableColumnFlags_WidthFixed, 64.0f, 3);
                ImGui::TableSetupColumn("Lifetime Spend", ImGuiTableColumnFlags_WidthFixed, 130.0f, 4);
                ImGui::TableHeadersRow();

                if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
                    if (specs->SpecsDirty && specs->SpecsCount > 0) {
                        m_crmState.sortColumn = static_cast<int>(specs->Specs[0].ColumnUserID);
                        m_crmState.sortAscending = (specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
                        specs->SpecsDirty = false;
                    }
                }

                const int startIdx = m_crmState.currentPage * kRowsPerPage;
                const int endIdx   = std::min<int>(static_cast<int>(filtered.size()), startIdx + kRowsPerPage);
                for (int i = startIdx; i < endIdx; ++i) {
                    const Guest* g = filtered[static_cast<std::size_t>(i)];
                    const bool selected = (m_crmState.selectedGuestId == g->id);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    char idSel[32]{};
                    std::snprintf(idSel, sizeof(idSel), "#%d##GSel%d", g->id, g->id);
                    if (ImGui::Selectable(idSel, selected, ImGuiSelectableFlags_SpanAllColumns)) {
                        m_crmState.selectedGuestId = g->id;
                    }
                    ImGui::TableSetColumnIndex(1); ImGui::Text("%s", g->fullName.c_str());
                    ImGui::TableSetColumnIndex(2); ImGui::Text("%s", g->isForeign ? g->passport.c_str() : g->cnic.c_str());
                    ImGui::TableSetColumnIndex(3); drawBadge(toString(g->tier), tierColor(g->tier));
                    ImGui::TableSetColumnIndex(4); ImGui::Text("%d", g->stayCount);
                    ImGui::TableSetColumnIndex(5); ImGui::Text("%s", formatPKR(g->lifetimeSpend).c_str());
                }
                ImGui::EndTable();
            }

            // Pagination controls
            ImGui::Spacing();
            if (ghostButton("<- Prev", ImVec2(90.0f, 30.0f), m_crmState.currentPage > 0)) {
                --m_crmState.currentPage;
            }
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(pal.dimText, "Page %d / %d (%zu guests)", m_crmState.currentPage + 1, totalPages, filtered.size());
            ImGui::SameLine();
            if (ghostButton("Next ->", ImVec2(90.0f, 30.0f), m_crmState.currentPage + 1 < totalPages)) {
                ++m_crmState.currentPage;
            }
        }
        endCard();

        // Right Column: Guest Detail Drawer
        ImGui::TableSetColumnIndex(1);
        if (beginCard("##CrmDetailDrawer", "Guest Profile & Loyalty Progress", ImVec2(0.0f, 0.0f))) {
            const Guest* sel = m_system.findGuestById(m_crmState.selectedGuestId);
            if (sel == nullptr) {
                drawEmptyState("Select a Guest", "Click any row in the directory to inspect profile & stay history.");
            } else {
                ImGui::TextColored(pal.text, "%s", sel->fullName.c_str());
                drawBadge(toString(sel->tier), tierColor(sel->tier));
                ImGui::SameLine();
                ImGui::TextColored(pal.dimText, "  %s", sel->nationality.c_str());
                ImGui::Separator();

                ImGui::Text("%s : %s", sel->isForeign ? "Passport" : "CNIC", sel->isForeign ? sel->passport.c_str() : sel->cnic.c_str());
                ImGui::Text("Phone    : %s", sel->phone.c_str());
                ImGui::Text("Email    : %s", sel->email.empty() ? "N/A" : sel->email.c_str());
                ImGui::Text("Address  : %s", sel->address.empty() ? "N/A" : sel->address.c_str());
                ImGui::Text("Joined   : %s", DateUtil::formatDate(sel->registeredDay).c_str());
                ImGui::Spacing();

                // Loyalty Tier Progress Bar toward next tier
                ImGui::TextColored(pal.goldHighlight, "Loyalty Progression");
                double nextSpend = Cfg::SILVER_SPEND;
                int nextStays = Cfg::SILVER_STAYS;
                const char* nextName = "Silver (5% off)";
                if (sel->tier == LoyaltyTier::Silver) {
                    nextSpend = Cfg::GOLD_SPEND;
                    nextStays = Cfg::GOLD_STAYS;
                    nextName = "Gold (10% + Free Breakfast)";
                } else if (sel->tier == LoyaltyTier::Gold) {
                    nextSpend = Cfg::PLATINUM_SPEND;
                    nextStays = Cfg::PLATINUM_STAYS;
                    nextName = "Platinum (15% + VIP Perks)";
                } else if (sel->tier == LoyaltyTier::Platinum) {
                    nextSpend = Cfg::PLATINUM_SPEND;
                    nextStays = Cfg::PLATINUM_STAYS;
                    nextName = "Maximum Tier Reached";
                }
                const float pctSpend = static_cast<float>(std::clamp(sel->lifetimeSpend / nextSpend, 0.0, 1.0));
                const float pctStays = static_cast<float>(std::clamp(static_cast<double>(sel->stayCount) / static_cast<double>(nextStays), 0.0, 1.0));
                const float prog = (sel->tier == LoyaltyTier::Platinum) ? 1.0f : std::max(pctSpend, pctStays);

                ImGui::ProgressBar(prog, ImVec2(-1.0f, 18.0f));
                if (m_fonts.captionFont != nullptr) { ImGui::PushFont(m_fonts.captionFont); }
                ImGui::TextColored(pal.dimText, "Next: %s | Stays: %d/%d | Spend: %s", nextName, sel->stayCount, nextStays, formatPKR(sel->lifetimeSpend).c_str());
                if (m_fonts.captionFont != nullptr) { ImGui::PopFont(); }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(pal.secondaryAccent, "Stay History");
                int stayRows = 0;
                for (int i = static_cast<int>(db.bookings.size()) - 1; i >= 0; --i) {
                    const Booking& b = db.bookings[static_cast<std::size_t>(i)];
                    if (b.guestId == sel->id) {
                        ++stayRows;
                        const Room* r = m_system.findRoomById(b.roomId);
                        drawBadge(toString(b.status), bookingStatusColor(b.status, pal));
                        ImGui::SameLine();
                        ImGui::Text("Rm %d · %s -> %s", r ? r->number : 0, DateUtil::formatDate(b.checkInDay).c_str(), DateUtil::formatDate(b.checkOutDay).c_str());
                    }
                }
                if (stayRows == 0) {
                    ImGui::TextColored(pal.dimText, "No bookings recorded yet.");
                }
            }
        }
        endCard();
        ImGui::EndTable();
    }

    // Validated [+ Register Guest] Modal
    if (m_crmState.openAddModal) {
        ImGui::OpenPopup("##CrmAddGuestModal");
        m_crmState.openAddModal = false;
    }
    ImGui::SetNextWindowSize(ImVec2(540.0f, 470.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("##CrmAddGuestModal", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextColored(pal.accent, "Register New Guest");
        ImGui::Separator();
        ImGui::Checkbox("Foreign Guest (Passport)", &m_crmState.formIsForeign);

        std::string nameErr, idErr, phoneErr, emailErr;
        const bool nameOk = validatePersonName(m_crmState.formName, &nameErr);
        bool idOk = false;
        bool phoneOk = true;
        if (!m_crmState.formIsForeign) {
            idOk = validateCnic(m_crmState.formCnic, &idErr);
            if (idOk && m_system.isDuplicateCnic(m_crmState.formCnic)) {
                idOk = false;
                idErr = "Duplicate CNIC found in Custom Hash Table!";
            }
            phoneOk = validatePakistaniPhone(m_crmState.formPhone, nullptr, &phoneErr);
        } else {
            idOk = validatePassport(m_crmState.formPassport, &idErr);
        }
        const bool emailOk = (m_crmState.formEmail[0] == '\0') || validateEmail(m_crmState.formEmail, &emailErr);

        ImGui::InputTextWithHint("Full Name", "e.g. Ayesha Siddiqui", m_crmState.formName, sizeof(m_crmState.formName));
        if (!nameOk) { ImGui::TextColored(pal.statusOccupied, "%s", nameErr.c_str()); }

        if (!m_crmState.formIsForeign) {
            ImGui::InputTextWithHint("CNIC", "XXXXX-XXXXXXX-X", m_crmState.formCnic, sizeof(m_crmState.formCnic));
        } else {
            ImGui::InputTextWithHint("Passport", "AB1234567", m_crmState.formPassport, sizeof(m_crmState.formPassport));
        }
        if (!idOk) { ImGui::TextColored(pal.statusOccupied, "%s", idErr.c_str()); }

        ImGui::InputTextWithHint("Phone", "03XX-XXXXXXX or +92 3XX XXXXXXX", m_crmState.formPhone, sizeof(m_crmState.formPhone));
        if (!phoneOk) { ImGui::TextColored(pal.statusOccupied, "%s", phoneErr.c_str()); }

        ImGui::InputTextWithHint("Email", "name@example.com", m_crmState.formEmail, sizeof(m_crmState.formEmail));
        if (!emailOk) { ImGui::TextColored(pal.statusOccupied, "%s", emailErr.c_str()); }

        ImGui::InputText("Nationality", m_crmState.formNationality, sizeof(m_crmState.formNationality));
        ImGui::InputText("Address", m_crmState.formAddress, sizeof(m_crmState.formAddress));
        ImGui::InputText("Preferences", m_crmState.formPrefs, sizeof(m_crmState.formPrefs));

        ImGui::Spacing();
        const bool canSave = nameOk && idOk && phoneOk && emailOk;
        if (primaryButton("Save Guest", ImVec2(140.0f, 36.0f), canSave)) {
            Guest ng{};
            ng.fullName        = m_crmState.formName;
            ng.isForeign       = m_crmState.formIsForeign;
            ng.cnic            = ng.isForeign ? "" : m_crmState.formCnic;
            ng.passport        = ng.isForeign ? m_crmState.formPassport : "";
            ng.phone           = m_crmState.formPhone;
            ng.email           = m_crmState.formEmail;
            ng.nationality     = m_crmState.formNationality[0] ? m_crmState.formNationality : "Pakistan";
            ng.address         = m_crmState.formAddress;
            ng.preferencesNote = m_crmState.formPrefs;
            std::string regErr;
            const int gid = m_system.registerGuest(ng, &regErr);
            if (gid > 0) {
                m_crmState.selectedGuestId = gid;
                ImGui::CloseCurrentPopup();
            } else {
                Toast::push(NoticeType::Error, "Save Failed", regErr);
            }
        }
        ImGui::SameLine();
        if (ghostButton("Cancel", ImVec2(110.0f, 36.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// =============================================================================
// 3. BOOKINGS MANAGEMENT SCREEN (Section 9.6)
// =============================================================================
/*
 * Purpose: Renders the Bookings Management screen with status filters, search, and one-click
 *          Check-In, Check-Out (triggering Invoice Preview & confetti), Cancel, and No-Show actions.
 * Inputs: None.
 * Outputs: Renders Bookings table.
 * Big-O: O(B) time.
 */
void GuiRenderer::renderBookingsScreen() {
    const ThemePalette& pal = m_theme.getActivePalette();
    const HotelDatabase& db = m_system.db();

    drawSectionHeader(
        "Bookings & Reservations Ledger",
        "Manage active stays, check-ins, cancellations (>48h free / <=48h 1st-night fee) & no-shows"
    );

    if (beginCard("##BookingsTopBar", nullptr, ImVec2(0.0f, 66.0f))) {
        const char* statusOpts[] = {"All Statuses", "Reserved", "Checked In", "Checked Out", "Cancelled", "No-Show"};
        ImGui::SetNextItemWidth(160.0f);
        ImGui::Combo("##BkStatusFilter", &m_opsState.bookingStatusFilter, statusOpts, 6);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputTextWithHint("##BkSearch", "Search guest name or room #...", m_opsState.bookingSearchBuf, sizeof(m_opsState.bookingSearchBuf));
        ImGui::SameLine();
        if (primaryButton("+ New Booking (Wizard)", ImVec2(200.0f, 34.0f))) {
            navigateTo(ScreenId::SmartCheckIn);
        }
    }
    endCard();
    ImGui::Spacing();

    if (beginCard("##BookingsTableCard", "All Bookings", ImVec2(0.0f, 0.0f))) {
        if (db.bookings.empty()) {
            drawEmptyState("No Bookings Yet", "Use the Smart Check-In Wizard or Viva Demo Seeder to populate bookings.");
        } else if (ImGui::BeginTable("##BookingsTbl", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 54.0f);
            ImGui::TableSetupColumn("Guest");
            ImGui::TableSetupColumn("Room", ImGuiTableColumnFlags_WidthFixed, 95.0f);
            ImGui::TableSetupColumn("Stay Dates", ImGuiTableColumnFlags_WidthFixed, 195.0f);
            ImGui::TableSetupColumn("Rate / Surge", ImGuiTableColumnFlags_WidthFixed, 145.0f);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 105.0f);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 235.0f);
            ImGui::TableHeadersRow();

            for (int i = static_cast<int>(db.bookings.size()) - 1; i >= 0; --i) {
                const Booking& b = db.bookings[static_cast<std::size_t>(i)];
                if (m_opsState.bookingStatusFilter != 0 &&
                    static_cast<int>(b.status) != (m_opsState.bookingStatusFilter - 1)) {
                    continue;
                }
                const Guest* g = m_system.findGuestById(b.guestId);
                const Room* r = m_system.findRoomById(b.roomId);
                const std::string gName = g ? g->fullName : "Unknown";
                const std::string rNum  = r ? std::to_string(r->number) : "0";
                if (!ciContains(gName, m_opsState.bookingSearchBuf) && !ciContains(rNum, m_opsState.bookingSearchBuf)) {
                    continue;
                }

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("#%d", b.id);
                ImGui::TableSetColumnIndex(1); ImGui::Text("%s", gName.c_str());
                ImGui::TableSetColumnIndex(2); ImGui::Text("Rm %s", rNum.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%s -> %s", DateUtil::formatDate(b.checkInDay).c_str(), DateUtil::formatDate(b.checkOutDay).c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%s (x%.2f)", formatPKR(b.quotedRate).c_str(), b.surgeMultiplier);
                ImGui::TableSetColumnIndex(5);
                drawBadge(toString(b.status), bookingStatusColor(b.status, pal));
                ImGui::TableSetColumnIndex(6);

                if (b.status == BookingStatus::Reserved) {
                    char ciId[32]{}; std::snprintf(ciId, sizeof(ciId), "Check-In##BkCI%d", b.id);
                    if (primaryButton(ciId, ImVec2(82.0f, 26.0f))) { m_system.checkInBooking(b.id); }
                    ImGui::SameLine();
                    char cxId[32]{}; std::snprintf(cxId, sizeof(cxId), "Cancel##BkCX%d", b.id);
                    if (dangerButton(cxId, ImVec2(66.0f, 26.0f))) { m_system.cancelBooking(b.id, false); }
                    ImGui::SameLine();
                    char nsId[32]{}; std::snprintf(nsId, sizeof(nsId), "NoShow##BkNS%d", b.id);
                    if (ghostButton(nsId, ImVec2(68.0f, 26.0f))) { m_system.cancelBooking(b.id, true); }
                } else if (b.status == BookingStatus::CheckedIn) {
                    char coId[32]{}; std::snprintf(coId, sizeof(coId), "Check-Out##BkCO%d", b.id);
                    if (primaryButton(coId, ImVec2(94.0f, 26.0f))) {
                        const CheckOutResult co = m_system.checkOutBooking(b.id, DateUtil::currentMinuteOfDay(), "Card");
                        if (co.success) {
                            if (co.loyaltyPromoted) { triggerLoyaltyConfetti(); }
                            openInvoicePreview(co.invoiceId);
                        }
                    }
                } else if (b.invoiceId != -1) {
                    char invId[32]{}; std::snprintf(invId, sizeof(invId), "View Invoice##BkInv%d", b.id);
                    if (ghostButton(invId, ImVec2(115.0f, 26.0f))) {
                        openInvoicePreview(b.invoiceId);
                    }
                }
            }
            ImGui::EndTable();
        }
    }
    endCard();
    renderInvoicePreviewModal();
}

// =============================================================================
// 4. BILLING, INVOICES & IN-APP INVOICE PREVIEW MODAL (Section 9.5)
// =============================================================================
/*
 * Purpose: Exports an invoice to TXT & HTML and opens the in-app Invoice Preview modal.
 * Inputs: invoiceId.
 * Outputs: Sets m_opsState preview fields and triggers modal open.
 * Big-O: O(I + A) time.
 */
void GuiRenderer::openInvoicePreview(int invoiceId) {
    const Invoice* inv = m_system.findInvoiceById(invoiceId);
    if (inv == nullptr) {
        return;
    }
    m_opsState.previewInvoiceId = invoiceId;
    m_system.exportInvoiceFiles(*inv, &m_opsState.previewTxtPath, &m_opsState.previewHtmlPath);
    m_opsState.openPreviewModal = true;
}

/*
 * Purpose: Renders the Billing & Invoices screen with paid/unpaid filters, Mark Paid button,
 *          and [Export & Preview] button opening the HTML-mirrored Invoice Preview modal (Section 9.5).
 * Inputs: None.
 * Outputs: Renders Billing table and modal.
 * Big-O: O(I) time.
 */
void GuiRenderer::renderBillingScreen() {
    const ThemePalette& pal = m_theme.getActivePalette();
    const HotelDatabase& db = m_system.db();

    drawSectionHeader(
        "Billing, Tax Folios & Receipt Exports",
        "Itemized GST folios · 80-column plain ASCII .txt & print-ready .html receipts"
    );

    if (beginCard("##BillingFilterBar", nullptr, ImVec2(0.0f, 66.0f))) {
        const char* paidOpts[] = {"All Invoices", "Paid Only", "Unpaid Only"};
        ImGui::SetNextItemWidth(150.0f);
        ImGui::Combo("##BillPaidFilter", &m_opsState.billingPaidFilter, paidOpts, 3);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputTextWithHint("##BillSearch", "Search invoice # or guest name...", m_opsState.billingSearchBuf, sizeof(m_opsState.billingSearchBuf));
    }
    endCard();
    ImGui::Spacing();

    if (beginCard("##BillingTableCard", "Issued Invoices", ImVec2(0.0f, 0.0f))) {
        if (db.invoices.empty()) {
            drawEmptyState("No Invoices Generated", "Invoices are automatically created upon Check-Out or Cancellation Fee.");
        } else if (ImGui::BeginTable("##InvoicesTbl", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Invoice #", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Guest");
            ImGui::TableSetupColumn("Issued", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Taxable", ImGuiTableColumnFlags_WidthFixed, 115.0f);
            ImGui::TableSetupColumn("Grand Total", ImGuiTableColumnFlags_WidthFixed, 125.0f);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 92.0f);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 235.0f);
            ImGui::TableHeadersRow();

            for (int i = static_cast<int>(db.invoices.size()) - 1; i >= 0; --i) {
                const Invoice& inv = db.invoices[static_cast<std::size_t>(i)];
                if (m_opsState.billingPaidFilter == 1 && !inv.paid) { continue; }
                if (m_opsState.billingPaidFilter == 2 && inv.paid)  { continue; }

                const Guest* g = m_system.findGuestById(inv.guestId);
                const std::string gName = g ? g->fullName : "Guest";
                if (!ciContains(inv.number, m_opsState.billingSearchBuf) && !ciContains(gName, m_opsState.billingSearchBuf)) {
                    continue;
                }

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextColored(pal.accent, "%s", inv.number.c_str());
                ImGui::TableSetColumnIndex(1); ImGui::Text("%s", gName.c_str());
                ImGui::TableSetColumnIndex(2); ImGui::Text("%s", DateUtil::formatDate(inv.issuedDay).c_str());
                ImGui::TableSetColumnIndex(3); ImGui::Text("%s", formatPKR(inv.taxableAmount).c_str());
                ImGui::TableSetColumnIndex(4); ImGui::TextColored(pal.goldHighlight, "%s", formatPKR(inv.grandTotal).c_str());
                ImGui::TableSetColumnIndex(5);
                drawBadge(inv.paid ? "PAID" : "UNPAID", inv.paid ? pal.statusAvailable : pal.statusOccupied);
                ImGui::TableSetColumnIndex(6);

                char prevBtn[32]{}; std::snprintf(prevBtn, sizeof(prevBtn), "Preview & Export##Prev%d", inv.id);
                if (primaryButton(prevBtn, ImVec2(138.0f, 26.0f))) {
                    openInvoicePreview(inv.id);
                    Toast::push(NoticeType::Success, "Invoice Exported", "Saved TXT & HTML receipts for " + inv.number);
                }
                if (!inv.paid) {
                    ImGui::SameLine();
                    char payBtn[32]{}; std::snprintf(payBtn, sizeof(payBtn), "Mark Paid##Pay%d", inv.id);
                    if (ghostButton(payBtn, ImVec2(84.0f, 26.0f))) {
                        m_system.markInvoicePaid(inv.id, "Card");
                    }
                }
            }
            ImGui::EndTable();
        }
    }
    endCard();
    renderInvoicePreviewModal();
}

/*
 * Purpose: Renders the in-app Invoice Preview modal mirroring the print-ready HTML layout,
 *          with [Open HTML in Browser] (ShellExecute/xdg-open/open) and [Copy Path] buttons (Section 9.5).
 * Inputs: None (uses m_opsState.previewInvoiceId).
 * Outputs: Renders modal popup.
 * Big-O: O(A) where A is number of booking add-ons.
 */
void GuiRenderer::renderInvoicePreviewModal() {
    if (m_opsState.openPreviewModal) {
        ImGui::OpenPopup("##InvoicePreviewModal");
        m_opsState.openPreviewModal = false;
    }

    const Invoice* inv = m_system.findInvoiceById(m_opsState.previewInvoiceId);
    if (inv == nullptr) {
        return;
    }

    const ThemePalette& pal = m_theme.getActivePalette();
    const Guest* g = m_system.findGuestById(inv->guestId);
    const Booking* b = m_system.findBookingById(inv->bookingId);
    const Room* r = (b != nullptr) ? m_system.findRoomById(b->roomId) : nullptr;

    ImGui::SetNextWindowSize(ImVec2(660.0f, 560.0f), ImGuiCond_Appearing);
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("##InvoicePreviewModal", nullptr, ImGuiWindowFlags_NoResize)) {
        // Header mirroring HTML folio sheet
        if (m_fonts.headingFont != nullptr) { ImGui::PushFont(m_fonts.headingFont); }
        ImGui::TextColored(pal.accent, "NUST SEECS Grand Hotel");
        if (m_fonts.headingFont != nullptr) { ImGui::PopFont(); }
        ImGui::SameLine(ImGui::GetWindowWidth() - 180.0f);
        ImGui::TextColored(pal.text, "%s", inv->number.c_str());

        ImGui::TextColored(pal.dimText, "Sector H-12, Islamabad · Official Tax Folio · Issued %s", DateUtil::formatDate(inv->issuedDay).c_str());
        ImGui::SameLine(ImGui::GetWindowWidth() - 120.0f);
        drawBadge(inv->paid ? "PAID" : "UNPAID", inv->paid ? pal.statusAvailable : pal.statusOccupied);
        ImGui::Separator();
        ImGui::Spacing();

        // Guest & Stay block
        ImGui::BeginChild("##InvMetaBox", ImVec2(0.0f, 76.0f), ImGuiChildFlags_Borders);
        if (ImGui::BeginTable("##InvMetaCols", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Billed To: %s", g ? g->fullName.c_str() : "Guest");
            if (g != nullptr) {
                drawBadge(toString(g->tier), tierColor(g->tier));
                ImGui::SameLine();
                ImGui::TextColored(pal.dimText, " %s", g->isForeign ? g->passport.c_str() : g->cnic.c_str());
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("Room: %s", r ? ("Room " + std::to_string(r->number) + " (" + toString(r->type) + ")").c_str() : "N/A");
            if (b != nullptr) {
                ImGui::TextColored(pal.dimText, "%s -> %s (%d nights)", DateUtil::formatDate(b->checkInDay).c_str(), DateUtil::formatDate(b->checkOutDay).c_str(), inv->nights);
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
        ImGui::Spacing();

        // Itemized table
        if (ImGui::BeginTable("##InvItemsTbl", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH, ImVec2(0.0f, 155.0f))) {
            ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Qty", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("Amount", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableHeadersRow();

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("Room Accommodation");
            ImGui::TableSetColumnIndex(1); ImGui::Text("%d", inv->nights);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%s", formatPKR(inv->roomSubtotal).c_str());

            if (b != nullptr) {
                for (const AddOn& a : b->addOns) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::Text("%s (%s)", a.description.c_str(), toString(a.type));
                    ImGui::TableSetColumnIndex(1); ImGui::Text("%d", a.quantity);
                    ImGui::TableSetColumnIndex(2); ImGui::Text("%s", formatPKR(a.unitPrice * a.quantity).c_str());
                }
            }
            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::Text("Room Subtotal    : %s", formatPKR(inv->roomSubtotal).c_str());
        ImGui::Text("Add-Ons Subtotal : %s", formatPKR(inv->addOnSubtotal).c_str());
        if (inv->loyaltyDiscount > 0.0) {
            ImGui::TextColored(pal.statusAvailable, "Loyalty Discount : -%s", formatPKR(inv->loyaltyDiscount).c_str());
        }
        if (inv->promoDiscount > 0.0) {
            ImGui::TextColored(pal.statusAvailable, "Promo Discount   : -%s", formatPKR(inv->promoDiscount).c_str());
        }
        ImGui::Text("GST (%.0f%%)        : %s", inv->taxRate * 100.0, formatPKR(inv->taxAmount).c_str());
        ImGui::TextColored(pal.goldHighlight, "GRAND TOTAL      : %s  (%s)", formatPKR(inv->grandTotal).c_str(), inv->paymentMethod.c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (primaryButton("Open HTML in Browser", ImVec2(190.0f, 36.0f))) {
            openFileInDefaultBrowser(m_opsState.previewHtmlPath);
            Toast::push(NoticeType::Info, "Browser Launched", "Opened " + m_opsState.previewHtmlPath);
        }
        ImGui::SameLine();
        if (ghostButton("Copy Path", ImVec2(115.0f, 36.0f))) {
            std::error_code ec;
            const std::string absPath = std::filesystem::absolute(m_opsState.previewHtmlPath, ec).string();
            ImGui::SetClipboardText(ec ? m_opsState.previewHtmlPath.c_str() : absPath.c_str());
            Toast::push(NoticeType::Success, "Path Copied", "Copied HTML receipt path to clipboard.");
        }
        ImGui::SameLine();
        if (ghostButton("Close", ImVec2(95.0f, 36.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// =============================================================================
// 5. FIFO WAITLIST QUEUE SCREEN (Section 9.6)
// =============================================================================
/*
 * Purpose: Renders the FIFO Waitlist Queue screen (Receptionist/Admin) showing queued guests
 *          in exact std::queue order with [Book Now] and [Remove] controls.
 * Inputs: None.
 * Outputs: Renders Waitlist screen.
 * Big-O: O(W) time.
 */
void GuiRenderer::renderWaitlistScreen() {
    const ThemePalette& pal = m_theme.getActivePalette();
    const HotelDatabase& db = m_system.db();
    const int32_t today = DateUtil::todayDayNumber();

    drawSectionHeader(
        "FIFO Waitlist Queue",
        "std::queue ordered by request date · Auto-matches when any compatible room frees up"
    );

    if (beginCard("##WaitlistAddCard", "Quick Add to Waitlist", ImVec2(0.0f, 108.0f))) {
        const char* typeNames[] = {"Standard", "Deluxe", "Suite", "Presidential"};
        const std::string selGuestName = [&]() -> std::string {
            if (const Guest* g = m_system.findGuestById(m_opsState.wlGuestId)) { return g->fullName; }
            return "Select Guest...";
        }();
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::BeginCombo("##WlGuest", selGuestName.c_str())) {
            for (const Guest& g : db.guests) {
                if (ImGui::Selectable(g.fullName.c_str(), m_opsState.wlGuestId == g.id)) {
                    m_opsState.wlGuestId = g.id;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130.0f);
        ImGui::Combo("##WlType", &m_opsState.wlTypeIdx, typeNames, 4);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::InputInt("In +d", &m_opsState.wlInOff);
        m_opsState.wlInOff = std::clamp(m_opsState.wlInOff, 0, 60);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::InputInt("Nights", &m_opsState.wlNights);
        m_opsState.wlNights = std::clamp(m_opsState.wlNights, 1, 21);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputFloat("Max PKR", &m_opsState.wlBudget, 0.0f, 0.0f, "%.0f");
        ImGui::SameLine();
        if (primaryButton("Enqueue", ImVec2(105.0f, 34.0f), m_opsState.wlGuestId != -1)) {
            m_system.addToWaitlist(
                m_opsState.wlGuestId,
                static_cast<RoomType>(m_opsState.wlTypeIdx),
                today + m_opsState.wlInOff,
                today + m_opsState.wlInOff + m_opsState.wlNights,
                static_cast<double>(m_opsState.wlBudget)
            );
        }
    }
    endCard();
    ImGui::Spacing();

    if (beginCard("##WaitlistQueueCard", "Active FIFO Waitlist", ImVec2(0.0f, 0.0f))) {
        std::queue<WaitlistEntry> q = m_system.waitlistQueue();
        if (q.empty()) {
            drawEmptyState("Waitlist Empty", "No guests are currently waiting for rooms.");
        } else if (ImGui::BeginTable("##WlTbl", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("Pos", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("Guest");
            ImGui::TableSetupColumn("Wanted Type", ImGuiTableColumnFlags_WidthFixed, 115.0f);
            ImGui::TableSetupColumn("Dates", ImGuiTableColumnFlags_WidthFixed, 190.0f);
            ImGui::TableSetupColumn("Max Budget", ImGuiTableColumnFlags_WidthFixed, 125.0f);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 185.0f);
            ImGui::TableHeadersRow();

            int pos = 1;
            while (!q.empty()) {
                const WaitlistEntry wl = q.front();
                q.pop();
                const Guest* g = m_system.findGuestById(wl.guestId);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("#%d", pos++);
                ImGui::TableSetColumnIndex(1); ImGui::Text("%s", g ? g->fullName.c_str() : "Guest");
                ImGui::TableSetColumnIndex(2); drawBadge(toString(wl.wantedType), pal.accent);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%s -> %s", DateUtil::formatDate(wl.fromDay).c_str(), DateUtil::formatDate(wl.toDay).c_str());
                ImGui::TableSetColumnIndex(4); ImGui::Text("%s", formatPKR(wl.maxBudget).c_str());
                ImGui::TableSetColumnIndex(5);
                char bkId[32]{}; std::snprintf(bkId, sizeof(bkId), "Book Now##WlBk%d", wl.id);
                if (primaryButton(bkId, ImVec2(88.0f, 26.0f))) {
                    m_wizardState = SmartCheckInState{};
                    m_wizardState.selectedGuestId = wl.guestId;
                    m_wizardState.checkInOffsetDays = std::max(0, wl.fromDay - today);
                    m_wizardState.nights = std::max(1, wl.toDay - wl.fromDay);
                    m_wizardState.prefs.budget = wl.maxBudget;
                    m_wizardState.step = 2;
                    m_system.removeWaitlistEntry(wl.id);
                    navigateTo(ScreenId::SmartCheckIn);
                }
                ImGui::SameLine();
                char rmId[32]{}; std::snprintf(rmId, sizeof(rmId), "Remove##WlRm%d", wl.id);
                if (dangerButton(rmId, ImVec2(76.0f, 26.0f))) {
                    m_system.removeWaitlistEntry(wl.id);
                }
            }
            ImGui::EndTable();
        }
    }
    endCard();
}

} // namespace hms
