# MASTER EXECUTION PROMPT — NUST SEECS Hotel Management System (HMS)

## 0. YOUR ROLE AND MISSION
You are an autonomous senior C++ software engineer. Build, compile, run and verify a complete, visually stunning, 100% C++ **Hotel Management System** named **NUST_SEECS_HMS** from scratch. It is the flagship 1st-semester BSCS Programming Fundamentals end-semester project at NUST SEECS (Section D, evaluated by Sir Shah Khalid). The professor awards extra marks for the best GUI, and a 1st-semester student must be able to defend every line in a viva. So:
- GUI quality is the top priority: polished, animated, consistent, 60 FPS.
- Code must be readable and beginner-defensible: plain loops, clear names, short functions (<60 lines where possible), a comment block above every function stating purpose, inputs, outputs and Big-O. No template metaprogramming, no macros beyond include guards, no clever tricks.

## 1. NON-NEGOTIABLE LAWS
1. **Zero Placeholders / Zero Truncation.** Never write `// TODO`, `// implement here`, `/* ... existing code ... */`, empty stubs or "similar to above". Every .h and .cpp is written out in full.
2. **Language:** Standard C++17 only. No Python, no scripts in the product, no web-views, no external DB (no SQLite/MySQL). Persistence uses only `<fstream>` and `<filesystem>`.
3. **Memory safety:** no raw `new`/`delete` unless needed; prefer `std::vector`, `std::string`, `std::unique_ptr`. Zero leaks, dangling pointers, UB, or uninitialized members (give every struct member a default initializer). Compile with `-Wall -Wextra -Wpedantic` (MSVC `/W4`) and fix every warning.
4. **PF alignment:** heavy, visible use of `struct`, `enum class`, pointers/references, `std::vector`, `std::map`/`std::unordered_map`, lambdas/comparator functors, `std::stack`/`std::queue`, plus hand-written algorithms: MergeSort, QuickSort, binary search, min-heap, Levenshtein distance, SHA-256, a custom hash table for guest lookup by CNIC. Where STL could do it, ALSO implement the custom version and use the custom one in the app (STL sort only as a verified cross-check in the X-Ray panel).
5. **Currency:** PKR, formatted with thousands separators ("PKR 12,500"). **Dates:** store as `int32_t dayNumber` (days since 2000-01-01) plus `int32_t minuteOfDay`; write your own civil-date conversion functions in `DateUtil` (inside `Models.h`/`Algorithms.cpp`) to avoid timezone bugs. "Today" = real system date at launch, but the demo seeder anchors history relative to it.
6. **Autonomous verification loop:** if you have terminal/build access you MUST create all files, run CMake configure + build, fix every error/warning, launch the binary and confirm the window opens, then run the self-test (Section 11). Report what you actually verified; never claim a check you did not run. If you have no terminal, say so plainly and do a careful manual compile-review pass of each file.
7. **Phase gates:** work in the phases of Section 12. Do not start a phase until the previous one compiles and runs.

## 2. TECH STACK (LOCKED)
- CMake ≥ 3.16, `CMAKE_CXX_STANDARD 17`, `CMAKE_CXX_STANDARD_REQUIRED ON`, extensions OFF.
- **FetchContent** (GIT_SHALLOW TRUE):
  - GLFW: `https://github.com/glfw/glfw.git` tag `3.4` (disable GLFW_BUILD_DOCS/TESTS/EXAMPLES/INSTALL).
  - Dear ImGui **docking branch**: `https://github.com/ocornut/imgui.git` tag `v1.91.5-docking` (if that tag fails, use branch `docking`). ImGui has no CMakeLists: use `FetchContent_Populate`/`FetchContent_MakeAvailable` and build a static library from `imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp imgui_demo.cpp backends/imgui_impl_glfw.cpp backends/imgui_impl_opengl3.cpp`.
  - ImPlot: `https://github.com/epezent/implot.git` tag `v0.16` (compile `implot.cpp implot_items.cpp`).
- **Offline fallback:** if `third_party/glfw`, `third_party/imgui`, `third_party/implot` directories exist, use them via `add_subdirectory`/source lists INSTEAD of fetching (CMake option `HMS_USE_VENDORED`, default OFF). Document in README.
- OpenGL 3.3 core, GLSL `#version 330` (ImGui backend string "#version 330"). Link `OpenGL::GL`, `glfw`; on Windows also `-static` libgcc/libstdc++ for MinGW so the .exe runs anywhere; on MSVC set `/utf-8`.
- Executable name: **NUST_SEECS_HMS**. Copy no assets: the app must run with zero external files. Fonts: try system fonts in order (`C:/Windows/Fonts/segoeui.ttf`, `/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf`, `/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf`), size 17 px body + 28 px headings + 12 px caption (merge three `AddFontFromFileTTF` calls); fall back to ImGui's default font if none exists. Never crash if a font is missing.
- Window: 1600×900 default, resizable, min 1280×720, VSync on, MSAA 4x (`glfwWindowHint(GLFW_SAMPLES,4)`), ImGui docking + viewports DISABLED (single window) but `DockSpaceOverViewport` ENABLED. Set `io.IniFilename = "data/layout.ini"`.

## 3. DIRECTORY TREE (MANDATORY MINIMUM)
CMakeLists.txt, README.md, .gitignore, data/ (runtime), backups/ (runtime), exports/ (runtime)
- include/Models.h — all structs/enums
- include/Sha256.h, src/Sha256.cpp
- include/FileManager.h, src/FileManager.cpp — serialization, atomic saves, backups, Viva Seeder
- include/Algorithms.h, src/Algorithms.cpp — sorts, searches, recommender, surge, heap, Levenshtein, forecast, interval overlap, custom hash table
- include/HotelSystem.h, src/HotelSystem.cpp — state manager, RBAC, CRM/loyalty, billing, invoice export, undo/redo, waitlist
- include/GuiRenderer.h, src/GuiRenderer.cpp — theme, layout, shared widgets, toasts, modals, command palette
- src/main.cpp
Permitted extras if a file would exceed ~1500 lines: `include/Theme.h`, `src/Theme.cpp`, and `src/Gui_<Screen>.cpp` partial files (declared in GuiRenderer.h). Never shrink functionality to avoid splitting.

## 4. DATA MODEL (include/Models.h) — exact members
All enums are `enum class` with `toString()` helpers.
- `enum class RoomStatus { Available, Occupied, Reserved, CleaningRequired, Maintenance }`
- `enum class RoomType { Standard, Deluxe, Suite, Presidential }`
- `enum class ViewType { City, Garden, Pool }`
- `enum class BedConfig { Single, Double, Twin, King }`
- `enum class Role { Admin, Receptionist, Housekeeping }`
- `enum class LoyaltyTier { Standard, Silver, Gold, Platinum }`
- `enum class BookingStatus { Reserved, CheckedIn, CheckedOut, Cancelled, NoShow }`
- `enum class TaskType { Cleaning, Maintenance }`
- `enum class TaskStatus { Queued, InProgress, Done }`
- `enum class AddOnType { Minibar, Laundry, Dining, Spa, Transport }`

`struct Room { int id; int number; int floor; RoomType type; ViewType view; BedConfig bed; double baseRate; int noiseLevel (1 quiet..5 loud); int elevatorDistance (rooms from elevator, 0..5); RoomStatus status; int currentBookingId (-1 none); int lastCheckoutDay; int lastCheckoutMinute; std::string notes; }`
`struct Guest { int id; std::string fullName, cnic, passport, phone, email, nationality, address; bool isForeign; LoyaltyTier tier; double lifetimeSpend; int stayCount; int registeredDay; std::string preferencesNote; }`
`struct Booking { int id; int guestId; int roomId; int checkInDay; int checkOutDay; int checkInMinute; int checkOutMinute; int adults; int children; BookingStatus status; double quotedRate; double surgeMultiplier; std::string promoCode; std::vector<AddOn> addOns; int invoiceId (-1); int createdDay; }`
`struct AddOn { AddOnType type; std::string description; double unitPrice; int quantity; int dayAdded; }`
`struct Invoice { int id; std::string number (e.g. "INV-1042"); int bookingId; int guestId; int issuedDay; int nights; double roomSubtotal; double addOnSubtotal; double loyaltyDiscount; double promoDiscount; double taxableAmount; double taxRate; double taxAmount; double grandTotal; bool paid; std::string paymentMethod; }`
`struct User { int id; std::string username; std::string saltHex; std::string passHashHex; Role role; std::string displayName; bool active; int lastLoginDay; }`
`struct HousekeepingTask { int id; int roomId; TaskType type; TaskStatus status; int severity (1..5, maintenance only); int createdDay; int createdMinute; double priorityScore; std::string assignedTo; std::string note; }`
`struct AuditEntry { int index; long long timestamp; int userId; std::string username; std::string action; std::string details; std::string prevHashHex; std::string hashHex; }`
`struct WaitlistEntry { int id; int guestId; RoomType wantedType; int fromDay; int toDay; double maxBudget; int createdDay; }`
`struct GuestPreferences { double budget; bool wantsLowFloor; bool noFloorPref; ViewType view; bool anyView; int elevatorPref (0 near, 1 any, 2 far); int noiseTolerance (1..5); BedConfig bed; bool anyBed; }`
`struct RoomMatch { int roomId; double score01; int percent; std::string reasonText; }`
`struct DailyStat { int day; double revenue; double occupancyPct; int checkins; int checkouts; }`
`struct ServiceOrder { int id; int bookingId; AddOnType type; std::string item; int qty; double price; int stage (0 Ordered,1 Preparing,2 Delivered); }`
Constants namespace `Cfg`: `TAX_RATE=0.16`, `SURGE_T1=0.70`, `SURGE_T2=0.85`, `SURGE_M1=1.10`, `SURGE_M2=1.25`, `SURGE_CAP=1.60`, `FLOORS=5`, `ROOMS_PER_FLOOR=10`, loyalty thresholds below.

## 5. PERSISTENCE (FileManager)
- Files in `data/`: `rooms.dat guests.dat bookings.dat invoices.dat users.dat audit_log.dat tasks.dat waitlist.dat orders.dat stats.dat settings.dat`.
- Binary format per file: 8-byte magic ("HMSDATA1"), uint32 version, uint32 record count, records (strings are uint32 length + bytes, ints fixed-width via `int32_t/int64_t/double` written with `write(reinterpret_cast<const char*>(&x), sizeof x)`), then a trailing uint32 FNV-1a checksum of everything before it. On load, a bad magic/checksum raises a clear error toast, and the file is auto-restored from the newest backup if any exists.
- **Atomic save:** write `<name>.tmp`, flush/close, verify size > 0, then `std::filesystem::rename` over the real file (remove the target first on Windows if rename fails). Never write directly to the real file.
- **Snapshot backups:** on logout, on exit, and every 5 minutes of runtime, copy `data/` into `backups/YYYYMMDD_HHMMSS/`; keep the newest 10 and delete older. Admin panel lists backups with a [Restore] button (confirm modal).
- **Crash recovery:** at startup, if a leftover `.tmp` exists, ignore it and log "Recovered from interrupted write" in the audit log; show a toast.
- `settings.dat` holds: theme choice, tax rate, surge on/off, next-id counters.
- **Viva Seeder** (`seedVivaData()`, `resetCleanState()`; wired to Admin buttons with confirm modals): see Section 10.

## 6. CRYPTO (Sha256.h/.cpp)
Implement FIPS 180-4 SHA-256 from scratch: the 64 round constants K, initial hash H0..H7, message padding (0x80 then zeros then 64-bit big-endian bit length), 512-bit block processing with the rotr/Ch/Maj/Σ0/Σ1/σ0/σ1 functions, hex digest output. Provide `std::string sha256Hex(const std::string&)` and a self-test against known vectors ("" → `e3b0c442...b855`, "abc" → `ba7816bf...15ad`) that runs in the startup self-test.
- Passwords: `saltHex` = 16 random bytes (use `std::random_device`) as hex; `passHashHex` = SHA256 applied 1000 times over `salt + password` (iterated; comment that this is an educational key-stretch, not production-grade).
- **Audit hash chain:** `hash_i = SHA256(prevHash_i + "|" + timestamp + "|" + userId + "|" + action + "|" + details)`, genesis `prevHash` = 64 zeros. `verifyChain()` recomputes every entry and returns the index of the first mismatch or -1. Admin UI shows a green "CHAIN VERIFIED ✔ (N entries)" or a flashing red "TAMPERING DETECTED at entry #K" banner, checked on login and via [Verify Now]. Log on: login/logout/failed login, booking create/cancel, check-in/out, room swap, price override, status change, user create/disable, seed/reset/restore.

## 7. RBAC and screens per role
Login screen: centered 460×560 glass-style card over an animated gradient background with slow drifting particles (drawn with ImDrawList), logo glyph "H" in a rounded square, username/password fields, show-password toggle, 3 failed attempts → 30 s lockout with countdown. Default accounts: `admin / Admin@123`, `reception / Desk@123`, `housekeeping / Clean@123`.
- **Admin:** Dashboard, Room Matrix, Timeline, Guests, Bookings, Billing, Yield Management, Analytics, Housekeeping Board, Staff, Audit Log, Backups, Settings, Viva Tools (Seeder, Reset, X-Ray).
- **Receptionist:** Dashboard (own), Room Matrix, Smart Check-In, Timeline, Guests (CRM), Bookings, Billing, Waitlist.
- **Housekeeping:** Dispatch Board (priority queue), Room Matrix (read + status change only), Service Orders Kanban.
Menu items a role cannot access are hidden, and every action function re-checks role server-side (`HotelSystem::can(Role, Action)`); a denied action shows an error toast and writes an audit entry.

## 8. THEME ENGINE (exact palettes)
Two themes, toggled instantly by a toolbar sun/moon button (and Ctrl+T), cross-faded over 250 ms by lerping the style colors each frame. Rounding: Window 12, Frame 8, Child 10, Popup 10, Grab 8, Tab 8, Scrollbar 12; FramePadding (12,8); ItemSpacing (10,10); WindowPadding (18,18); anti-aliased lines/fills ON; Window borders 1 px.
**Midnight Obsidian (dark):** background `#0B0E14`, panel `#121722`, surface/frame `#1A2030`, hover `#232B40`, border `#2A3248`, text `#E6EAF2`, dim text `#8B93A7`, accent `#6C8CFF`, accent hover `#8AA4FF`, accent active `#4F6FE8`, secondary accent `#22D3EE`, gold highlight `#F5B942`.
**Executive Pearl (light):** background `#F4F6FA`, panel `#FFFFFF`, surface `#EDF0F6`, hover `#E2E7F2`, border `#D5DAE6`, text `#1B2233`, dim text `#66708A`, accent `#3B5BDB`, accent hover `#4C6EF5`, accent active `#2F4AB8`, secondary `#0891B2`, gold `#D97706`.
**Status colors (both themes):** Available `#10B981`, Occupied `#EF4444`, Reserved `#F59E0B`, Cleaning `#8B5CF6`, Maintenance `#64748B`. Tier colors: Standard `#94A3B8`, Silver `#CBD5E1`, Gold `#FBBF24`, Platinum `#A78BFA`.
**Shared widgets (write once, reuse everywhere):** `Card(title)` rounded panel with a soft drop-shadow (2 offset translucent rects), `KpiTile(label, value, delta, sparkline)` with count-up animation over 600 ms (ease-out cubic), `Badge(text, color)` pill, `StatusDot`, `ProgressRing`, `PrimaryButton/GhostButton/DangerButton` with hover lift and 120 ms color lerp, `Tooltip`, `SectionHeader`, `EmptyState` illustration drawn with primitives. Smooth page transition: fade + 12 px slide-in over 180 ms when the active screen changes.
**Shell layout:** left sidebar 230 px (collapsible to 64 px icon rail; icons drawn as vector glyphs with ImDrawList, no icon fonts) with logo, nav items, role badge and logout at the bottom; top bar 56 px with breadcrumb, global search field (Ctrl+K opens the command palette), live clock, theme toggle, notification bell with unread count, user avatar circle with initials. Status bar 26 px: "Occupancy 72% | Surge ×1.10 | Chain ✔ | Saved HH:MM:SS | FPS".
**Toasts:** top-right stack, max 5, 340×72 cards, slide in from the right over 250 ms (ease-out), colored left accent bar by type (Success `#10B981`, Info `#6C8CFF`, Warning `#F59E0B`, Error `#EF4444`), auto-dismiss after 4 s with a shrinking progress line, click to dismiss, fade-out last 300 ms. API: `Toast::push(type, title, message)`. Example: "Room 304 Checked Out — Invoice #INV-1042 Generated".
**Splash:** 1.8 s animated splash (logo scale-in, progress bar while data loads, "NUST SEECS · Section D · PF Project") before login.

## 9. FEATURE SPECIFICATIONS

### 9.1 Dashboard (per role)
Admin: 4 KPI tiles (Occupancy %, Today's Revenue, ADR = revenue ÷ occupied room-nights, RevPAR = revenue ÷ total rooms), ImPlot line chart "Revenue — last 30 days" with a dashed 7-day forecast, stacked bar chart "Occupancy by floor", donut chart (custom ImDrawList arcs) "Room status mix", "Today's Arrivals / Departures" lists, a live activity feed from the audit log. Receptionist: arrivals/departures, quick actions, availability counter. Housekeeping: queue length, avg wait, rooms ready today.

### 9.2 Interactive Floor-Plan Room Matrix
Five floor rows (Floor 5 at top to Floor 1 at bottom), 10 tiles per floor, numbers 101–110 … 501–510, with an elevator block drawn between rooms x05 and x06 and stairs at both ends. Tile 92×72, rounded 10, filled with the status color at 85% alpha, room number in bold, type/guest-surname caption, tier crown icon for VIP guests, and a pulsing ring on rooms with an urgent housekeeping task. Hover: tile lifts 3 px with glow plus tooltip (type, rate, view, bed). Filters bar: status chips (toggle), type dropdown, floor dropdown, search by number or guest. Legend with counts per status.
**Click → modal "Room NNN"** (640×520): header with status badge; left column specs (type, floor, view, bed, base rate, today's surge-adjusted rate, noise, elevator distance); middle column current guest card (name, tier badge, phone, stay dates, nights so far) and **live accrued bill** (room + add-ons − discount + tax) updating each frame; right column action buttons: Check-In (if Reserved/Available), Check-Out (if Occupied), Room Swap (opens a picker of available rooms with the recommender score), Dispatch Housekeeping, Flag Maintenance (severity slider 1–5 and note), Add Service Charge. Buttons disabled with explanatory tooltips when invalid or not permitted for the role.

### 9.3 Smart Check-In wizard (Receptionist/Admin)
Stepper with 4 steps: (1) Find or register guest (fuzzy search + CNIC binary search + [New Guest] form), (2) Stay details (dates, adults/children, promo), (3) Preferences form and recommended rooms, (4) Review and confirm. Step 3 shows the **Top-3 recommendation cards** with percent-match badges ("96% Match"), a per-feature mini bar breakdown, and a one-line reason ("Pool view, 1 room from elevator, within budget"). Walk-in booking creation and check-in happen in one flow; pre-booked guests go through the same wizard starting from the booking. If no rooms are available, offer [Add to Waitlist].

### 9.4 CRM and validation
Guest registry table (ImGui Tables API: sortable by clicking headers via the custom MergeSort, resizable columns, row selection, text filter, pagination 15 rows). Detail drawer shows profile, tier progress bar toward the next tier, and stay history.
Validation functions in `Algorithms.cpp` (hand-written character scanning, NOT std::regex, so they can be explained): CNIC `XXXXX-XXXXXXX-X` (13 digits with dashes at positions 5 and 13); Pakistani phone `03XX-XXXXXXX` or `+92 3XX XXXXXXX` (normalise); passport = 1–2 letters followed by 7 digits; email = exactly one `@`, a non-empty local part, a domain containing a `.` that is not first or last, and no spaces; names = letters/spaces/hyphens, length 2–60. Red inline error text under each field, with the Save button disabled until valid. Duplicate-CNIC check via the custom hash table.
**Loyalty state machine** (re-evaluated on every checkout; promote only, never auto-demote): Standard → Silver at ≥5 stays OR lifetime ≥ PKR 150,000; → Gold at ≥15 stays OR ≥ PKR 500,000; → Platinum at ≥30 stays OR ≥ PKR 1,500,000. Perks: Silver 5% discount; Gold 10% + free breakfast (a PKR 0 "Breakfast" line item on each night); Platinum 15% + priority housekeeping (+6 in the heap formula) + complimentary late checkout until 16:00. Promotion fires a gold confetti burst (ImDrawList particles, 1.5 s) and a toast.

### 9.5 Billing, tax and export
Folio: for each night `nightRate = baseRate × surgeMultiplier (locked at booking)`; `roomSubtotal = Σ nightRate`; `addOnSubtotal = Σ unitPrice×quantity` (Minibar, Laundry, Dining, Spa, Transport); `loyaltyDiscount = tierPct × roomSubtotal`; promo codes (stored in a small table): `WELCOME10` 10% room, `NUST500` flat PKR 500, `SEECS15` 15% on 3+ nights; only ONE promo applies, never below zero; `taxable = roomSubtotal + addOnSubtotal − loyaltyDiscount − promoDiscount`; `tax = taxable × 0.16` (configurable); `grandTotal = taxable + tax`, rounded to the nearest rupee. Late checkout after 12:00 adds 50% of one night unless Platinum.
**Export:** `exports/INV-<number>.txt` (80-column aligned ASCII receipt with box-drawing in plain ASCII) and `exports/INV-<number>.html` (self-contained, inline CSS, print-ready with `@media print`, hotel header, guest block, itemised table, totals, tier badge, QR-like decorative block drawn with CSS). After export an in-app **Invoice Preview modal** renders the folio with an ImGui layout that mirrors the HTML, with buttons [Open HTML in Browser] (platform call: `ShellExecute` on Windows, `xdg-open` on Linux, `open` on macOS) and [Copy Path]. A toast confirms.

### 9.6 Timeline (Gantt) and booking engine
Horizontal timeline: rows = 50 rooms (grouped by floor, collapsible), columns = 30 days (scroll/zoom). Bookings are rounded bars colored by status. Drag the right edge to extend or shorten a stay; drag the bar to another row to swap rooms; a drop that creates a conflict flashes red and rejects. Conflict detection uses `intervalsOverlap(aStart,aEnd,bStart,bEnd)` (half-open [in, out)) with a per-room sorted booking vector and binary search; implement and document it in `Algorithms.cpp`. Overbooking is impossible by construction.
**Waitlist:** FIFO `std::queue`-style ordered by created time; when a booking is cancelled or a room frees up, the engine scans the waitlist, auto-matches the first entry whose type, dates and budget fit, and shows a toast with [Book Now].
**Cancellation policy:** >48 h before check-in: free; 24–48 h: 1 night charged; <24 h or no-show: 100% first night. Writes an invoice line "Cancellation fee".

### 9.7 Algorithm 1 — Multi-Criteria Smart Room Recommender
For each AVAILABLE room r that is free for the requested dates, compute feature scores in [0,1]:
- `f_budget`: with B = budget, p = baseRate × surge: if p ≤ B then `1 − 0.3×(B−p)/B`, else `max(0, 1 − 2×(p−B)/B)`.
- `f_floor`: no preference → 1; low preference → `1 − (floor−1)/4`; high preference → `(floor−1)/4`.
- `f_view`: 1 if the view matches (or any view), else 0.3.
- `f_elev`: with d = elevatorDistance (0..5): near → `1 − d/5`; far → `d/5`; any → 1.
- `f_noise`: with room noise n and tolerance t: if t ≥ n then 1 else `1 − (n−t)/4`.
- `f_bed`: 1 if matches (or any), else 0.2.
Weights `w = {budget 0.30, floor 0.15, view 0.20, elevator 0.10, noise 0.10, bed 0.15}` (sum 1.0, shown in the X-Ray panel and adjustable by sliders for Admin). `S(r) = Σ wᵢ·fᵢ(r)`, `percent = round(100·S)`. Tier bonus: Platinum guests get +0.02 for rooms on floors ≥4 (cap S at 1.0). Rank ALL candidate rooms with the custom **MergeSort** (descending by S, ties by lower room number), take the top 3. Complexity O(N log N) time, O(N) space. The X-Ray panel shows the comparison count and cross-checks the result against `std::sort`.

### 9.8 Algorithm 2 — Dynamic Yield Management
`occ = occupiedOrReservedRoomsForTheNight ÷ 50`. `occMult = 1.25 if occ > 0.85; 1.10 if occ > 0.70; else 1.00`. `seasonMult`: 1.15 in Dec–Jan and Jun–Aug, else 1.00. `weekendMult = 1.10` on Fri/Sat nights, else 1.00. `surge = min(Cfg::SURGE_CAP, occMult × seasonMult × weekendMult)`. `price = baseRate × surge`, rounded to PKR 10. Admin **Pricing Simulator**: sliders for occupancy (0–100%), season toggle, weekend toggle, and elasticity ε (default −1.2). Live outputs: surge multiplier, price per type, expected demand `D = D0 × (surge)^ε`, expected revenue `R = price × D`, drawn as an ImPlot curve of revenue vs surge (1.0–1.6) with a marker at the current point and the revenue-maximising multiplier highlighted. A manual **price override** (Admin only, with a reason text) is audit-logged.

### 9.9 Algorithm 3 — Housekeeping & Maintenance Min-Heap Dispatcher
Implement a custom array-backed binary min-heap (`std::vector<HousekeepingTask>`, `siftUp`, `siftDown`, `push`, `popMin`, `peekMin`, `updateKey` by linear search then re-sift). Priority `P = 3U + V + E + 2·sev`, where `U` = next check-in urgency (≤1 h: 10, ≤3 h: 8, ≤6 h: 6, ≤12 h: 4, ≤24 h: 2, none: 0), `V` = arriving guest's tier weight (Standard 0, Silver 2, Gold 4, Platinum 6), `E = min(10, 0.5 × hoursSinceCheckout)`, `sev` = severity 1–5 for maintenance (0 for cleaning). Heap key = `−P` (so the highest P is the minimum); ties broken by the earlier creation time. Scores are recomputed every 30 s of app time. The Dispatch Board lists tasks in heap-pop order (copy the heap and pop to display) with a colored priority bar, urgency chips, and buttons [Start], [Done] (sets the room to Available and creates a toast) and [Reassign]. Tasks are created automatically on checkout (Cleaning) and by the "Flag Maintenance" action.

### 9.10 Niche wow features (all required)
1. **Command Palette (Ctrl+K):** centered 600 px overlay, fuzzy search over navigation targets, guests (name/CNIC), rooms (number) and actions ("Check in", "Toggle theme", "Seed data"), ranked with the custom Levenshtein distance plus a prefix/substring bonus, arrow keys to navigate, Enter to run, Esc to close, with a fade/scale animation.
2. **Undo/Redo (Ctrl+Z / Ctrl+Y):** command pattern with two `std::stack<Command>` for status changes, room swaps, add-on charges and booking edits (each Command has `apply()` and `revert()` as lambdas or functors); the toolbar shows the last action's name, and undo is limited to the current session.
3. **Occupancy Heatmap Calendar:** GitHub-style grid of 90 days × color intensity (ImDrawList rectangles, tooltips show date, occupancy %, revenue).
4. **7-day Forecast:** least-squares linear regression over the last 30 days of revenue and occupancy, plotted as a dashed line with a shaded ±1σ band, with the formula and slope shown in the X-Ray panel.
5. **Night Audit:** Admin button [Run Night Audit] closes the day: rolls no-shows, posts room charges, flags unpaid invoices, writes a `DailyStat`, writes `exports/NightAudit_<date>.txt`, and takes a backup.
6. **Service Orders Kanban:** 3 columns (Ordered / Preparing / Delivered) with drag-and-drop cards (ImGui drag-drop payloads); delivering an order adds the charge to the guest's folio.
7. **Algorithm X-Ray panel (Viva Tools):** a live side panel that shows, for the last run of each algorithm, the name, input size N, comparisons/swaps counted, elapsed microseconds, theoretical Big-O, and the source file and function name; with a micro-benchmark button that runs MergeSort vs QuickSort vs `std::sort` on random arrays of 100/1,000/10,000 elements and draws the results as a bar chart. Include a **step-by-step MergeSort/heap visualiser** (animated bars) for explaining the algorithms live to the examiner.
8. **Keyboard shortcuts overlay (F1):** a cheat sheet of every shortcut.
9. **Guided Demo Mode:** a [Start Guided Demo] button that auto-walks a 90-second tour (login → seed → check-in → surge change → heap dispatch → audit verify) with captions, so the group can show every feature fast.

## 10. VIVA DEMO SEEDER (Admin Viva Tools)
`[Seed Viva Demo Data]` (confirm modal, backup first) creates:
- 50 rooms: floors 1–5 × 10 rooms (101–110 … 501–510); types: floors 1–2 mostly Standard, 3 Deluxe, 4 Deluxe/Suite, 5 Suite plus 2 Presidential (509, 510); base rates Standard 8,000, Deluxe 12,500, Suite 22,000, Presidential 45,000 PKR; views cycle City/Garden/Pool; bed configs vary; `elevatorDistance = abs(unitIndex − 4.5)` rounded; `noiseLevel` derived from elevator and street side.
- 35 guests: ~22 Pakistani (varied names across Punjabi, Pashtun, Sindhi, Baloch, Kashmiri backgrounds, valid-format CNICs, 03XX phones) and ~13 international (UK, UAE, China, USA, Turkey, Saudi Arabia, Germany with passport numbers); tiers spread to include at least 3 Platinum, 5 Gold, 7 Silver.
- 30 days of history: bookings, invoices, `DailyStat` rows with a realistic weekly pattern (weekend peaks) and a rising trend, totalling roughly PKR 4–7 million; today's occupancy ≈ 76% so the 1.10× surge is active.
- Active states: 8 rooms CleaningRequired and 3 Maintenance, all in the heap with varied priorities; 6 arrivals and 5 departures today; 4 pending service orders; 2 waitlist entries.
- ≥ 60 valid, chained audit log entries.
`[Reset Clean State]` (double-confirm) wipes `data/` and recreates rooms plus the 3 default users only. Both write an audit entry and a toast.

## 11. STARTUP SELF-TEST (runs in `main`, shown in a Viva Tools "Self-Test" tab)
Check SHA-256 vectors, hash-chain verify on a fresh log, MergeSort/QuickSort versus `std::sort` on 1,000 random ints, binary search hit/miss, heap pops in non-decreasing key order, CNIC/phone/email validators on 10 valid and 10 invalid strings, tax/discount calc for a known folio (assert exact numbers), save→load round-trip equality for every record type. Print PASS/FAIL per check in the UI and console; exit code non-zero in `--selftest` CLI mode (run the tests without opening a window).

## 12. BUILD PHASES (compile + run gate after EACH)
P0: CMake + FetchContent + empty ImGui window with the docking space, theme toggle and font loading. P1: Models, Sha256, FileManager, audit chain, self-test CLI. P2: Algorithms (all) + self-test. P3: HotelSystem (RBAC, CRM, loyalty, billing, exports, undo, waitlist). P4: GUI shell, login, splash, toasts, dashboard. P5: Room Matrix, modals, Smart Check-In wizard. P6: Timeline, Guests, Bookings, Billing, Invoice preview. P7: Yield, Analytics, Housekeeping board, Kanban, Command Palette, X-Ray, heatmap, forecast. P8: Seeder, guided demo, backups UI, Night Audit. P9: README, full self-test, warning sweep, final run. Report each gate's outcome.

## 13. README.md (viva defense)
Include: project overview and screenshots placeholder list (describe what to capture); default credentials table; build steps for Windows (MinGW via CMake + Ninja/Make, Visual Studio 2022 "Open Folder" and CLI), Linux (`apt install build-essential cmake libgl1-mesa-dev xorg-dev` then build), the vendored/offline mode; keyboard shortcuts; how to run `--selftest`; and the **Viva Defense Cheat Sheet** table with columns: PF concept | Where used | File::function | Time | Space — covering structs, enum class, pointers/references, vectors, maps, file I/O (binary + atomic rename), sorting (MergeSort, QuickSort), binary search, hashing (SHA-256, custom hash table), heap/priority queue, stack (undo), queue (waitlist), recursion (MergeSort/QuickSort), string handling (validators), Levenshtein DP, linear regression, interval overlap. For each major algorithm add a short worked example and a 3-line Big-O justification. Add a "Likely examiner questions" section with 15 questions and concise answers, and a team module-ownership table template (3 members).

## 14. FINAL DELIVERABLE CHECKLIST
All files fully written; builds with zero warnings; `--selftest` all PASS; the app launches to the splash and login; every role's screens are reachable and the seeder produces a rich populated demo; both themes look polished; exports open correctly; the audit tamper test works (edit one byte of `audit_log.dat` → red banner); README complete. End with a concise report: what was built, what you actually verified and how, and any known limitations (be honest).