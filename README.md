# NUST SEECS Hotel Management System (`NUST_SEECS_HMS`)

**BSCS 1st-Semester Programming Fundamentals — Flagship Semester Project**
**Institution:** National University of Sciences & Technology (NUST), SEECS — Section D
**Instructor:** Sir Shah Khalid
**Language & Toolchain:** 100% C++17 (`-Wall -Wextra -Wpedantic`), Dear ImGui (docking) + ImPlot + GLFW 3.4 + OpenGL 3.3 Core

---

## 1. Project Overview

**NUST_SEECS_HMS** is a standalone, single-executable C++17 desktop Hotel Management System built from scratch with zero database servers, zero Python/web runtimes, and zero external cryptography or algorithm libraries. It combines a dark/light dual-theme desktop GUI (**Midnight Obsidian** & **Executive Pearl** with smooth 250 ms cubic ease-out transitions) with production-inspired data structures, algorithms, binary persistence, and cryptographic tamper detection:

- **50-Room Live Operations:** Interactive 5×10 Room Matrix (Floors 1–5), per-room inspection modal with live accrued billing, 4-step **Smart Check-In Wizard**, and a 50×30 **Reservation Gantt Timeline** supporting drag-to-extend and drag-to-swap.
- **Role-Based Access Control (RBAC):** Server-side permission enforcement (`HotelSystem::can` / `checkPermission`) for **Admin**, **Receptionist**, and **Housekeeping**, with 3-strike 30-second login lockout and `RBAC_DENIED` audit logging.
- **Binary Persistence & Crash Safety:** 11 fixed-width binary `.dat` tables (`HMSDATA1` magic header + version + record count + trailing 32-bit FNV-1a checksum), atomic `.tmp` + rename saves, startup crash recovery, and timestamped snapshot backups (up to 10 retained, auto-restore on corruption).
- **FIPS 180-4 SHA-256 & Tamper-Evident Audit Chain:** Hand-written SHA-256 with 16-byte random hex salts, 1,000-round iterated key stretching, and a blockchain-style audit log where modifying a single byte in `data/audit_log.dat` triggers an immediate flashing red tamper alert.
- **Core Algorithms (100% Hand-Written):** Custom stable MergeSort, median-of-three QuickSort, Binary Search, 127-bucket Chained CNIC Hash Table, Array-Backed Binary Min-Heap Dispatcher, Multi-Criteria Smart Room Recommender, Dynamic Yield Management & Elasticity Simulator, 7-Day Least-Squares Linear Regression Forecast, Bottom-Up DP Levenshtein Fuzzy Search (`Ctrl+K`), and Character-Scanning Input Validators (no `std::regex`).
- **Viva Defense Tooling:** Built-in **Viva Demo Seeder** (35 Pakistani & international guests, 30 days of PKR 4–7M history, 76% occupancy / 1.10× surge), **Algorithm X-Ray** live telemetry, **MergeSort vs QuickSort vs `std::sort` Micro-Benchmarks**, **Step-by-Step MergeSort & Min-Heap Visualiser**, **15-Test Startup Self-Test Suite**, and a **90-Second Guided Demo Mode**.

---

## 2. Screenshots Placeholder List

Capture the following screenshots (at `1600×900` or `1920×1080`) after running `[Seed Viva Demo Data]` in **Viva Tools** and place them in `docs/screenshots/`:

| # | Suggested Filename | What to Capture |
|---|---|---|
| 1 | `01_splash_login.png` | 1.8 s Splash screen or Login card with animated floating particle canvas and password-visibility toggle. |
| 2 | `02_dashboard_dark.png` | Admin Dashboard in **Midnight Obsidian (Dark)** theme showing the 4 KPI cards, 30-day revenue/occupancy ImPlot chart, room-status donut, and quick-action bar. |
| 3 | `03_dashboard_light.png` | Admin Dashboard in **Executive Pearl (Light)** theme (`Ctrl+T`) demonstrating crisp contrast and clean typography. |
| 4 | `04_room_matrix_modal.png` | 5×10 Interactive Room Matrix with Floor/Type/Status filter pills and an open **Room NNN Modal** showing live accrued billing. |
| 5 | `05_smart_checkin_top3.png` | 4-Step **Smart Check-In Wizard** (Step 3) displaying the Top-3 ranked room cards with Match % ring badges and 6-feature reason chips. |
| 6 | `06_gantt_timeline.png` | 50×30 **Reservation Gantt Timeline** with color-coded booking bars, current-day vertical marker, and hover tooltip. |
| 7 | `07_guests_crm.png` | **Guests CRM** table sorted via custom MergeSort (15 rows/page) with Pakistani/international badges, Loyalty tier chips, and slide-over detail drawer. |
| 8 | `08_billing_invoice_preview.png` | **Billing & Invoices** view with the 80-column ASCII Invoice Preview modal open alongside the exported print-ready `INV-*.html` in a browser. |
| 9 | `09_yield_simulator.png` | **Yield Management Pricing Simulator** showing occupancy/season/weekend/elasticity sliders and the ImPlot revenue-vs-surge curve with optimal multiplier marker. |
| 10 | `10_analytics_heatmap_forecast.png` | **Analytics** screen with the 90-day GitHub-style Occupancy Heatmap Calendar and 7-day Linear Regression Forecast ($\pm 1\sigma$ shaded band). |
| 11 | `11_housekeeping_heap.png` | **Housekeeping Dispatch Board** ordered by the custom Binary Min-Heap with colored priority bars, urgency chips, and `[Start]`/`[Done]`/`[Reassign]` actions. |
| 12 | `12_service_orders_kanban.png` | **Service Orders Kanban** (Ordered / Preparing / Delivered) showing drag-and-drop room-service cards. |
| 13 | `13_audit_verified_vs_tampered.png` | **Audit Log** screen showing the green `Verified (N entries)` banner side-by-side with the flashing red `TAMPER DETECTED` banner after flipping 1 byte of `audit_log.dat`. |
| 14 | `14_viva_xray_visualiser.png` | **Viva Tools** showing the Algorithm X-Ray table, sorting micro-benchmark bar chart (`100 / 1,000 / 10,000`), and Step-by-Step MergeSort / Min-Heap bar visualiser. |
| 15 | `15_command_palette_shortcuts.png` | Centered `Ctrl+K` fuzzy **Command Palette** and `F1` **Keyboard Shortcuts** overlay. |

---

## 3. Default Credentials

Three default accounts are seeded automatically on first launch (passwords stored using per-user 16-byte random salt + 1,000-round SHA-256):

| Role | Username | Password | Accessible Modules |
|---|---|---|---|
| **Admin** | `admin` | `Admin@123` | Full access: Dashboard, Room Matrix, Smart Check-In, Timeline, Guests, Bookings, Billing, Waitlist, Yield Pricing & Override, Analytics, Housekeeping, Service Orders, Staff, Audit Log, Backups, Night Audit, Settings, Viva Tools |
| **Receptionist** | `reception` | `Desk@123` | Reception Dashboard, Room Matrix, Smart Check-In, Timeline, Guests CRM, Bookings, Billing & Invoice Export, Waitlist, Service Orders |
| **Housekeeping** | `housekeeping` | `Clean@123` | Housekeeping Dashboard, Room Matrix (status updates), Housekeeping Min-Heap Dispatch Board, Service Orders |

> **Security Note:** Three consecutive failed logins trigger a 30-second lockout timer. Any unauthorized action attempt is rejected server-side by `HotelSystem::checkPermission()` and recorded as `RBAC_DENIED` in the SHA-256 audit chain.

---

## 4. Build Instructions

CMake automatically fetches **GLFW 3.4**, **Dear ImGui v1.91.5-docking**, and **ImPlot v0.16** on the first configure via `FetchContent`.

### 4.1 Windows — MSYS2 UCRT64 (MinGW + CMake + Ninja)

1. Install [MSYS2](https://www.msys2.org/) and open the **MSYS2 UCRT64** shell:
   ```bash
   pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja git
   ```
2. Configure and build (from PowerShell or MSYS2 UCRT64):
   ```powershell
   $env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build --parallel
   ```
   *(If you prefer GNU Make instead of Ninja, replace `-G Ninja` with `-G "MinGW Makefiles"`.)*
3. Run:
   ```powershell
   .\build\NUST_SEECS_HMS.exe
   ```

### 4.2 Windows — Visual Studio 2022

**Option A — IDE ("Open Folder"):**
1. Open Visual Studio 2022 with the **Desktop development with C++** workload installed.
2. Choose **File → Open → Folder…** and select the project root (`D:\HMS`).
3. Wait for CMake configuration to finish, select `NUST_SEECS_HMS.exe` as the startup item, and press **F5** (or **Ctrl+F5**).

**Option B — Developer Command Prompt / PowerShell CLI:**
```powershell
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64
cmake --build build-vs --config Release --parallel
.\build-vs\Release\NUST_SEECS_HMS.exe
```

### 4.3 Linux (Ubuntu / Debian)

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build git libgl1-mesa-dev xorg-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/NUST_SEECS_HMS
```

### 4.4 Offline / Vendored Mode (No Internet Required)

If building on an offline lab machine, place the extracted source folders at:
- `third_party/glfw`
- `third_party/imgui`
- `third_party/implot`

Then configure with `-DHMS_USE_VENDORED=ON`:
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DHMS_USE_VENDORED=ON
cmake --build build --parallel
```

---

## 5. Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+K` | Open / close the fuzzy **Command Palette** |
| `Ctrl+T` | Toggle theme (**Midnight Obsidian** ↔ **Executive Pearl**) with 250 ms cross-fade |
| `Ctrl+Z` | **Undo** last session action (room status, room swap, booking edit, add-on charge) |
| `Ctrl+Y` | **Redo** previously undone action |
| `F1` | Toggle the **Keyboard Shortcuts** cheat-sheet overlay |
| `Esc` | Close active modal, drawer, Command Palette, or shortcuts overlay |
| `↑` / `↓` + `Enter` | Navigate and execute results inside the Command Palette |

---

## 6. Running Self-Tests, Smoke Test & Audit Tamper Verification

### 6.1 Headless CLI Self-Test (`--selftest`)

Runs all 15 verification checks (SHA-256 FIPS vectors, audit chain integrity + tamper detection, MergeSort & QuickSort vs `std::sort` on 1,000 random ints, Binary Search hit/miss, Min-Heap non-decreasing pop order, CNIC/Phone/Email validators on 10 valid + 10 invalid strings each, exact folio tax/discount arithmetic, full 11-table binary save→load round-trip, and Phase 3 RBAC/CRM/billing/waitlist/undo integration tests) without opening a GUI window:

```powershell
.\build\NUST_SEECS_HMS.exe --selftest
```

- **Expected result:** `15/15 PASS` and exit code `0`. Any failure returns a non-zero exit code.
- The exact same self-test suite is also viewable interactively in **Viva Tools → Self-Test**.

### 6.2 GUI Smoke Test (`--smoke-test`)

Launches the full GLFW + OpenGL 3.3 + Dear ImGui window, renders frames across seeded state, toggles both themes, executes Night Audit export, and exits cleanly with code `0`:

```powershell
.\build\NUST_SEECS_HMS.exe --smoke-test
```

### 6.3 Live Audit Tamper Test (Viva Demo)

1. Run the app, log in as `admin`, seed data (or perform any action), and open **Audit Log** — observe the green **Chain Verified** banner.
2. Close the app, open `data/audit_log.dat` in a hex editor (or flip 1 byte inside an audit record), and relaunch.
3. Open **Audit Log** — `FileManager` detects checksum mismatch / `Sha256::verifyAuditChain()` pinpoints the exact first corrupted entry index and displays the flashing red **TAMPER DETECTED** banner.

---

## 7. Viva Defense Cheat Sheet

| PF Concept | Where Used | `File::function` | Time Complexity | Space Complexity |
|---|---|---|---|---|
| `struct` (aggregate data modeling) | All 13 domain records (`Room`, `Guest`, `Booking`, `Invoice`, `User`, `AuditEntry`, `HousekeepingTask`, `WaitlistEntry`, `ServiceOrder`, `DailyStat`, `AddOn`, `GuestPreferences`, `RoomMatch`) | `include/Models.h` | $O(1)$ field access | $O(1)$ per record |
| `enum class` (type-safe states) | Room types/statuses, booking states, loyalty tiers, user roles, RBAC actions | `include/Models.h::toString`, `include/HotelSystem.h::Action` | $O(1)$ | $O(1)$ (`int32_t`) |
| Pointers & References | Non-owning lookups (`Room*`, `Guest*`), optional out-parameters (`std::string* outError`), zero raw `new`/`delete` | `HotelSystem.cpp::findRoomById`, `HotelSystem.cpp::findGuestById` | $O(N)$ linear / $O(1)$ hash | $O(1)$ |
| `std::vector` | Dynamic tables for rooms, guests, bookings, invoices, heap array, hash buckets | `FileManager.h::HotelDatabase`, `Algorithms.h::TaskMinHeap` | $O(1)$ amortized push, $O(1)$ index | $O(N)$ |
| `std::map` / `std::unordered_map` | Grouped timeline bookings per room, daily revenue aggregation, analytics breakdowns | `Gui_Rooms.cpp::renderTimeline`, `HotelSystem.cpp::runNightAudit` | $O(\log N)$ map / $O(1)$ avg unordered | $O(N)$ |
| Binary File I/O + Atomic Rename + FNV-1a | Crash-safe persistence of 11 `.dat` tables (`HMSDATA1` header, `.tmp` write, size check, rename, FNV-1a checksum) | `FileManager.cpp::writeAtomicEnvelope`, `FileManager.cpp::readVerifiedEnvelope`, `FileManager.cpp::computeFnv1a` | $O(B)$ bytes | $O(B)$ buffer |
| Sorting — MergeSort | Stable ranking of room recommendations & multi-column Guest/Booking/Billing table sorting | `Algorithms.h::mergeSort`, `Algorithms.h::detail::mergeSortCombine` | $O(N \log N)$ worst/avg/best | $O(N)$ auxiliary |
| Sorting — QuickSort | Median-of-three in-place sorting for sorting micro-benchmarks & comparison | `Algorithms.h::quickSort`, `Algorithms.h::detail::quickSortPartition` | $O(N \log N)$ avg, $O(N^2)$ worst | $O(\log N)$ stack |
| Binary Search | CNIC lookup on sorted guests & locating first candidate interval for booking conflict checks | `Algorithms.cpp::binarySearchInt`, `Algorithms.cpp::binarySearchGuestByCnic`, `Algorithms.cpp::hasRoomBookingConflictSorted` | $O(\log N)$ | $O(1)$ |
| Hashing — SHA-256 & Audit Chain | FIPS 180-4 password hashing (16B salt × 1,000 rounds) & tamper-evident audit log chain | `Sha256.cpp::sha256Hex`, `Sha256.cpp::hashPassword`, `Sha256.cpp::verifyAuditChain` | $O(L)$ per message, $O(E)$ chain verify | $O(1)$ state |
| Hashing — Custom Hash Table | Separate-chaining 127-bucket hash table for $O(1)$ average CNIC → Guest ID lookup | `Algorithms.cpp::GuestCnicHashTable::hashCnic`, `GuestCnicHashTable::findGuestId` | $O(1)$ avg, $O(N)$ worst | $O(N + B)$ ($B=127$) |
| Binary Min-Heap / Priority Queue | Housekeeping & maintenance dispatch ordered by negative priority key $-P$ | `Algorithms.cpp::TaskMinHeap::push`, `popMin`, `siftUp`, `siftDown`, `updateKey` | $O(\log N)$ push/pop, $O(N)$ updateKey | $O(N)$ |
| `std::stack` (LIFO) | Session Undo / Redo command pattern (`m_undoStack`, `m_redoStack`) | `HotelSystem.cpp::undo`, `HotelSystem.cpp::redo` | $O(1)$ push/pop | $O(U)$ commands |
| `std::queue` (FIFO) | Waitlist queue with automatic room-availability matching | `HotelSystem.cpp::addToWaitlist`, `HotelSystem.cpp::checkWaitlistMatchForRoom` | $O(1)$ enqueue, $O(W)$ scan | $O(W)$ entries |
| Recursion | Divide-and-conquer splitting in MergeSort and QuickSort | `Algorithms.h::detail::mergeSortRecursive`, `detail::quickSortRecursive` | $O(N \log N)$ | $O(\log N)$ call stack |
| String Handling (Hand-Written Validators) | Character-by-character validation of CNIC, Pakistani phone (+ normalizer), passport, email, name (no `std::regex`) | `Algorithms.cpp::validateCnic`, `validatePakistaniPhone`, `validatePassport`, `validateEmail`, `validatePersonName` | $O(L)$ string length | $O(1)$ |
| Dynamic Programming (Levenshtein) | Case-insensitive edit distance + prefix/substring bonus for `Ctrl+K` fuzzy Command Palette | `Algorithms.cpp::levenshteinDistance`, `Algorithms.cpp::computeFuzzyRankScore` | $O(M \cdot N)$ | $O(\min(M, N))$ (2-row DP) |
| Linear Regression (Least Squares) | 7-day revenue & occupancy forecasting with $\pm 1\sigma$ confidence band over 30-day history | `Algorithms.cpp::compute7DayForecast` | $O(N)$ ($N=30$) | $O(1)$ + 7 outputs |
| Half-Open Interval Overlap | Conflict-impossible double-booking check $[a_{\text{start}}, a_{\text{end}}) \cap [b_{\text{start}}, b_{\text{end}}) \neq \emptyset$ | `Algorithms.cpp::intervalsOverlap`, `Algorithms.cpp::hasRoomBookingConflictSorted` | $O(1)$ check, $O(\log B + k)$ room scan | $O(1)$ |

---

## 8. Major Algorithms — Worked Examples & 3-Line Big-O Justifications

### 8.1 Multi-Criteria Smart Room Recommender + Custom MergeSort
**Formula:** $S = \sum_{i=1}^{6} w_i f_i + \text{tierBonus}$, with default weights $(0.30, 0.15, 0.20, 0.10, 0.10, 0.15)$ normalized to sum to 1.0, and Platinum tier bonus $+0.02$ on floors $\ge 4$, clamped to $[0, 1]$.

**Worked Example:** A **Platinum** guest wants a **Pool** view, **King** bed, noise tolerance $t = 3$, near elevator (`elevatorPref = 0`), budget $B = \text{PKR } 15{,}000/\text{night}$, floor preference **High** (`noFloorPref = false, wantsLowFloor = false`). Candidate **Room 406** (Floor 4, Deluxe, $p = 12{,}500 \times 1.10 = \text{PKR } 13{,}750$, Pool view, King bed, noise $n = 2$, elevator distance $d = 1$):
- $f_{\text{budget}} = 1 - 0.3 \times \frac{15{,}000 - 13{,}750}{15{,}000} = 0.975$, $f_{\text{floor}} = \frac{4-1}{4} = 0.75$, $f_{\text{view}} = 1.0$, $f_{\text{elev}} = 1 - \frac{1}{5} = 0.80$, $f_{\text{noise}} = 1.0$ ($t \ge n$), $f_{\text{bed}} = 1.0$.
- Weighted sum $= 0.30(0.975) + 0.15(0.75) + 0.20(1) + 0.10(0.80) + 0.10(1) + 0.15(1) = 0.935$.
- Plus Platinum floor $\ge 4$ bonus $+0.02 \Rightarrow S = 0.955$ (**96% match**). Candidates are ranked descending by `mergeSort` (ties broken by lower room number, cross-checked against `std::sort`).

**3-Line Big-O Justification:**
1. Evaluating $R$ candidate rooms against 6 features takes $O(R)$ time, and filtering conflicts against $B$ bookings takes $O(R \log B)$ when sorted by check-in day.
2. Custom MergeSort recursively splits $R$ rooms in $\lceil \log_2 R \rceil$ levels and performs $O(R)$ merge work per level, yielding $O(R \log R)$ guaranteed worst-case time.
3. Auxiliary space is $O(R)$ for the merge buffer `temp` and output score list.

---

### 8.2 Dynamic Yield Management & Elasticity Simulator
**Formula:** $\text{surge} = \min(1.60, \text{occMult} \times \text{seasonMult} \times \text{weekendMult})$, $\text{price} = \text{round}_{10}(\text{baseRate} \times \text{surge})$, demand $D = D_0 \cdot \text{surge}^{\varepsilon}$, revenue $R = \text{price} \cdot D$.

**Worked Example:** 38 of 50 rooms occupied/reserved ($\text{occ} = 76\% \Rightarrow \text{occMult} = 1.10$), peak summer July ($\text{seasonMult} = 1.15$), Friday night ($\text{weekendMult} = 1.10$):
- Raw surge $= 1.10 \times 1.15 \times 1.10 = 1.3915 \le 1.60$.
- Deluxe base PKR 12,500 $\Rightarrow 12{,}500 \times 1.3915 = 17{,}393.75 \rightarrow \text{PKR } 17{,}390$ (rounded to nearest PKR 10).
- With $D_0 = 50$ and $\varepsilon = -1.2$: $D = 50 \times 1.3915^{-1.2} \approx 33.66$ rooms, expected revenue $R \approx \text{PKR } 585{,}347$.

**3-Line Big-O Justification:**
1. `computeYieldPricing` evaluates a fixed set of threshold checks and multiplications in $O(1)$ time and $O(1)$ space.
2. `simulateYieldCurve` samples $K = 61$ surge grid points from `1.00` to `1.60` in `0.01` steps, running in $O(K)$ time.
3. Storing the sampled curve points uses $O(K)$ memory, which is constant ($K = 61$) at runtime.

---

### 8.3 Housekeeping & Maintenance Binary Min-Heap Dispatcher
**Formula:** $P = 3U + V + E + 2\cdot\text{sev}$, heap key $= -P$ (ties broken by earlier creation timestamp).

**Worked Example:** Room 509 (Presidential) has a **Maintenance** task (severity $\text{sev} = 4$) with a **Platinum** guest ($V = 6$) arriving in **2 hours** ($U = 8$), 4 hours since checkout ($E = \min(10, 0.5 \times 4) = 2$):
- $P = 3(8) + 6 + 2 + 2(4) = 24 + 6 + 2 + 8 = 40.0$, so heap key $= -40.0$.
- A routine Cleaning task with $P = 14.0$ has key $-14.0$; since $-40.0 < -14.0$, Room 509 sifts up to index `0` and dispatches first.

**3-Line Big-O Justification:**
1. A complete binary tree of $N$ tasks stored in `std::vector<HousekeepingTask>` has height $\lfloor \log_2 N \rfloor$, so `push` (`siftUp`) and `popMin` (`siftDown`) run in $O(\log N)$ time.
2. `updateKey` performs a linear scan to locate `taskId` in $O(N)$ followed by an $O(\log N)$ re-sift, giving $O(N)$ total time.
3. Space is $O(N)$ in-place within the vector (plus a temporary $O(N)$ copy when rendering the full sorted dispatch list).

---

### 8.4 FIPS 180-4 SHA-256 & Tamper-Evident Audit Chain
**Formula:** $\text{hash}_i = \text{SHA256}(\text{hash}_{i-1} \mathbin{\|} \text{"|"} \mathbin{\|} \text{timestamp} \mathbin{\|} \text{"|"} \mathbin{\|} \text{userId} \mathbin{\|} \text{"|"} \mathbin{\|} \text{action} \mathbin{\|} \text{"|"} \mathbin{\|} \text{details})$, with $\text{hash}_0 = 0^{64}$.

**Worked Example:** Entry #1 (`LOGIN`, `admin`) hashes `000...000|1763540000|1|LOGIN|User admin signed in` to produce `a4f9...`. Entry #2 (`CHECK_IN`) includes `a4f9...` as its `prevHash`. If an attacker changes `admin` to `guest` in Entry #1, Entry #1's recomputed hash changes completely, breaking Entry #1 and every subsequent link in the chain.

**3-Line Big-O Justification:**
1. SHA-256 processes input in fixed 512-bit (64-byte) blocks with 64 compression rounds per block, taking $O(L)$ time for an $L$-byte message.
2. `verifyAuditChain` recomputes the digest for each of the $E$ entries sequentially, running in $O(E \cdot \bar{L})$ time.
3. Working state is fixed (8 × 32-bit state words + 64 × 32-bit schedule words), requiring $O(1)$ auxiliary space.

---

### 8.5 Half-Open Interval Overlap & Binary Search Conflict Detection
**Formula:** Two half-open stays $[a_s, a_e)$ and $[b_s, b_e)$ overlap iff $a_s < b_e \land b_s < a_e$.

**Worked Example:** Existing booking is Nov 19–22 (`[7262, 7265)`). New booking starting on checkout day Nov 22–24 (`[7265, 7267)`) checks `7262 < 7267 && 7265 < 7265` → `false` (**allowed**, same-day turnover). A booking for Nov 21–23 (`[7264, 7266)`) checks `7262 < 7266 && 7264 < 7265` → `true` (**blocked**).

**3-Line Big-O Justification:**
1. A single half-open interval overlap check executes two integer comparisons in $O(1)$ time.
2. Given a room's $B$ bookings sorted by `checkInDay`, binary search finds the first interval with `checkInDay >= reqCheckOut` in $O(\log B)$ time and scans backward only while intervals can still overlap.
3. Auxiliary space is $O(1)$ (iterative binary search, no recursion stack).

---

### 8.6 Custom 127-Bucket CNIC Hash Table
**Hash Function:** Polynomial rolling hash $h = (h \times 31 + c) \bmod 127$ over the normalized CNIC string, resolved via separate chaining (`std::vector<std::vector<BucketNode>>`).

**Worked Example:** Inserting `"35202-1234567-1"` (Guest #1) hashes to bucket `b = hashCnic("35202-1234567-1") % 127` and appends `{cnic, 1}`. Duplicate-CNIC registration checks `findGuestId()` in bucket `b`, visiting only ~1 node on average (load factor $\alpha = 35 / 127 \approx 0.28$).

**3-Line Big-O Justification:**
1. Computing the rolling hash takes $O(L)$ where $L = 15$ (constant CNIC length), i.e., $O(1)$.
2. With prime bucket count $B = 127$ and uniform hashing, each bucket has expected length $\alpha = N / B$, giving $O(1 + \alpha)$ average insert/lookup/delete time ($O(N)$ worst case if all keys collide).
3. Total memory is $O(N + B)$ for $N$ stored entries across $B = 127$ bucket vectors.

---

### 8.7 Levenshtein Edit Distance (DP) & 7-Day Linear Regression
**Worked Example (Levenshtein):** Query `"chek"` vs `"Check In"` (lowercased): minimum edits = 1 insertion (`'c'`) to reach `"check"` + prefix bonus → ranks `"Check In"` at the top of the `Ctrl+K` palette.
**Worked Example (Linear Regression):** Over 30 days ($x = 0 \dots 29$) with rising daily revenue $y_i$, ordinary least squares computes slope $m = \frac{N\sum xy - \sum x \sum y}{N\sum x^2 - (\sum x)^2}$, intercept $b = \bar{y} - m\bar{x}$, and residual standard deviation $\sigma = \sqrt{\frac{1}{N}\sum (y_i - \hat{y}_i)^2}$, then projects days $30 \dots 36$ with shaded band $\hat{y}_x \pm \sigma$.

**3-Line Big-O Justification:**
1. Levenshtein DP fills an $(M+1)\times(N+1)$ recurrence using two rolling 1D rows, achieving $O(M \cdot N)$ time and $O(\min(M, N))$ space.
2. Ordinary least squares makes two linear passes over $N = 30$ daily statistics (sums, then residual variance), running in $O(N)$ time.
3. Regression stores only scalar accumulators and 7 forecast points, using $O(1)$ auxiliary space.

---

## 9. Likely Examiner Questions (15 Viva Q&As)

1. **Q: Why did you use `enum class` instead of plain `enum` or `#define` constants?**
   **A:** `enum class` is strongly typed and scoped (e.g., `RoomStatus::Available` never collides with `BookingStatus::Cancelled`), prevents accidental implicit conversion to `int`, and fixes the underlying size to `int32_t` for deterministic binary file serialization.

2. **Q: Why did you store dates as `int32_t` days since 2000-01-01 instead of strings like `"2026-11-19"`?**
   **A:** Integer day counts make night-count subtraction (`checkOutDay - checkInDay`), sorting, binary search, and half-open interval overlap checks $O(1)$ integer operations without parsing strings or worrying about leap-year bugs (handled once in `DateUtil::toDays` / `fromDays` using Howard Hinnant's civil-date algorithm).

3. **Q: How do you guarantee a room can never be double-booked?**
   **A:** Every booking creation, date edit, and room swap runs `hasRoomBookingConflictSorted` using half-open intervals `[checkInDay, checkOutDay)`. Because checkout day is excluded, Guest A leaving Nov 22 and Guest B arriving Nov 22 never collide, while any true overlap is rejected before state mutation.

4. **Q: Why did you write custom MergeSort and QuickSort instead of just calling `std::sort`?**
   **A:** Programming Fundamentals requires mastering divide-and-conquer, recursion, and complexity trade-offs firsthand. We use custom stable MergeSort for production ranking/table sorting (guaranteeing $O(N \log N)$ worst-case and preserving tie order), implement median-of-three QuickSort for benchmarking, and cross-check both against `std::sort` in `--selftest` to prove correctness.

5. **Q: Why is MergeSort stable and why does stability matter in the HMS UI?**
   **A:** During `mergeSortCombine`, when two elements compare equal we always take the left-half element first. If a receptionist sorts Guests by Loyalty Tier after sorting by Name, stability preserves the alphabetical order within each tier.

6. **Q: Why does the Housekeeping Min-Heap store `-P` as its key instead of using a Max-Heap directly?**
   **A:** Storing the negated priority `-P` lets a textbook binary min-heap (`siftUp` / `siftDown` on `<`) pop the highest-urgency task at index `0`, with earlier creation timestamps breaking ties cleanly.

7. **Q: Why does `TaskMinHeap::updateKey` take $O(N)$ instead of $O(\log N)$?**
   **A:** Because our heap is a pure array-backed `std::vector` without an auxiliary `taskId -> index` hash map, finding the task by ID takes $O(N)$ linear search before the $O(\log N)$ re-sift. For $\le 50$ rooms ($N \le 50$), linear scan is cache-friendly and takes under 1 microsecond.

8. **Q: How does your atomic file save prevent data corruption if the app crashes mid-write?**
   **A:** `FileManager::writeAtomicEnvelope` writes the full binary envelope (header + records + FNV-1a checksum) to `file.dat.tmp`, flushes and checks the file size is non-zero, and only then atomically replaces `file.dat`. If a crash occurs mid-write, the old `file.dat` remains intact and startup recovery cleans up the leftover `.tmp` file.

9. **Q: What happens if someone manually corrupts one byte of `data/audit_log.dat`?**
   **A:** Two independent defenses catch it: (1) `FileManager::loadAuditLog` verifies the FNV-1a checksum and intentionally disables backup auto-restore for `audit_log.dat` so forensic evidence is never silently overwritten, and (2) `verifyAuditChain()` recomputes every entry's SHA-256 hash linked to `prevHash`, pinpointing the exact tampered index and showing a flashing red banner in the Audit Log view.

10. **Q: Why do you salt passwords and hash them 1,000 times instead of storing plain `SHA256(password)`?**
    **A:** A unique 16-byte random salt per user prevents rainbow-table lookups and ensures two users with identical passwords have completely different hashes. Iterating SHA-256 1,000 times slows down offline brute-force guessing (an educational key-stretching model; production systems use Argon2id or bcrypt).

11. **Q: Why did you choose 127 buckets for `GuestCnicHashTable`?**
    **A:** 127 is a prime number, which minimizes clustering modulo `kBucketCount` under our polynomial rolling hash ($h = 31h + c$). For a 50-room hotel with tens to a few hundred guests, 127 buckets keeps the load factor low ($\alpha < 1$) while using minimal memory.

12. **Q: How does the Undo/Redo system work without duplicating the entire database?**
    **A:** It uses the Command Pattern with two `std::stack<UndoCommand>` stacks (`m_undoStack`, `m_redoStack`). Each command stores only the minimal before/after delta inside `apply` and `revert` lambdas, making undo/redo $O(1)$ time and lightweight in memory.

13. **Q: Why did you avoid `std::regex` for CNIC, phone, and email validation?**
    **A:** Hand-written character-scanning parsers run in guaranteed linear $O(L)$ time with zero backtracking risk, zero `<regex>` compile-time/binary bloat, and let us produce exact, human-readable error messages (e.g., `"CNIC must have hyphens at positions 5 and 13"`).

14. **Q: How is the 16% GST tax calculated relative to Loyalty and Promo discounts?**
    **A:** `HotelSystem::calculateFolio` computes `roomSubtotal + addOnSubtotal + lateCheckoutFee`, subtracts the Loyalty discount (Silver 5%, Gold 10%, Platinum 15%) and any valid promo code (`WELCOME10`, `NUST500`, `SEECS15`) to get `taxableAmount` (clamped $\ge 0$), and applies 16% GST to `taxableAmount`. All numbers are verified to the exact paisa in `--selftest`.

15. **Q: Why is there zero raw `new` or `delete` in the entire project?**
    **A:** Modern C++17 RAII containers (`std::vector`, `std::string`, `std::stack`, `std::queue`, `std::map`) automatically manage heap memory on scope exit, eliminating memory leaks and dangling pointers by construction.

---

## 10. Team Module-Ownership Table (3-Member Template)

Fill in your group members' names and CMS IDs before submission:

| Team Member | CMS ID | Primary Owned Modules | Key Viva Demo Responsibilities |
|---|---|---|---|
| **Member 1 (Lead / Systems & Security)** | `______` | `Models.h`, `Sha256.*`, `FileManager.*`, `main.cpp`, `CMakeLists.txt` | Binary persistence, atomic crash recovery, SHA-256 password salting, live audit tamper test, `--selftest` |
| **Member 2 (Algorithms & Core Logic)** | `______` | `Algorithms.*`, `HotelSystem.*` | Smart Room Recommender + MergeSort, Dynamic Yield Simulator, Min-Heap Dispatcher, CNIC Hash Table, Undo/Redo, Waitlist, Folio & Billing |
| **Member 3 (GUI, Analytics & UX)** | `______` | `Theme.*`, `GuiRenderer.*`, `Gui_Dashboard.cpp`, `Gui_Rooms.cpp`, `Gui_Operations.cpp`, `Gui_Admin.cpp` | Midnight Obsidian / Executive Pearl themes, Room Matrix, 4-Step Check-In Wizard, Gantt Timeline, Kanban, `Ctrl+K` Palette, Guided Demo Mode |
