# Function Reference — rop-algorithm/core

คู่มืออ้างอิงทุกฟังก์ชันใน C++ ALNS Solver เรียงตามโมดูล

---

## ภาพรวมสถาปัตยกรรม

```
main.cpp
  └─► SolverServiceImpl::Solve()         [gRPC entry point]
        └─► solveRequest()                [solver_service.cpp:174]
              ├── adaptiveConstruct()      [construction]
              ├── orOptRelocate()          [routeOpt]  (pre-ALNS consolidation)
              ├── ALNSSolver::solve()      [alns]      (ถ้า enableALNS=true)
              │     ├── destroy operators  [alns/destroy.cpp]
              │     ├── repair operators   [alns/repair.cpp]
              │     └── applyTwoOptToSolution()
              ├── applyTwoOpt()            [routeOpt]  (ถ้า enableALNS=false)
              ├── orOptRelocate()          [routeOpt]  (post-optimization)
              └── writeResponse()          [output]
```

ทุกโมดูลอยู่ภายใต้ namespace `hfvrptwb` (Heterogeneous Fleet Vehicle Routing Problem with Time Windows and Breaks)

---

## 1. Entry Point & gRPC Service

### `main.cpp`
| ฟังก์ชัน | บทบาท |
|----------|--------|
| `main()` | เริ่ม gRPC server ที่ `0.0.0.0:50051` (หรือจาก `argv[1]`) ลงทะเบียน `SolverServiceImpl` แล้ว `Wait()` ตลอดไป |

### `solver_service.h`
| โครงสร้าง/คลาส | บทบาท |
|----------------|--------|
| `SolveConfig` | config ทั้งหมดของการ solve — fixed cost, cost/km, seed, enableALNS, enableMultiTrip, reloadMin, multiStartCount |
| `SolverServiceImpl` | implements `solver::SolverService::Service` สำหรับ gRPC — `Solve()` เรียก `solveRequest()` |
| `SolverV2` | wrapper สำหรับเรียก solver โดยตรง (ไม่ผ่าน gRPC) — `Solve()` คืน `SolveResponse` และเก็บ `OperatorStats` |

### `solver_service.cpp`
| ฟังก์ชัน | บรรทัด | บทบาท |
|----------|--------|--------|
| `addDrop()` | 16 | เพิ่ม unassigned node + drop reason ลง response |
| `writeRoute()` | 28 | validate route ทั้งหมด → เขียน route/stops/timing ลง response ถ้า feasible |
| `writeResponse()` | 67 | รวมทุก route + unassigned → คำนวณ objective = sum(route_cost) + 1000 × unassigned_count ตั้งค่า status (`OK`/`TIMEOUT`/`INFEASIBLE`) |
| `applyTwoOpt()` | 102 | เรียก `twoOptTrip()` สำหรับทุก trip ในทุก route (ใช้เมื่อ enableALNS=false) |
| `solveRequest()` | 174 | **แกนหลักของ pipeline** — เรียก construction → or-opt → ALNS/2-opt → or-opt → writeResponse |

**Flow `solveRequest()` แบบละเอียด:**

```
1. ตรวจสอบ input: matrix_size < 2 → OK ทันที, vehicles_size == 0 → INFEASIBLE
2. adaptiveConstruct()                      ← Phase 1: จัดรถด้วย greedy insertion
3. orOptRelocate()                          ← ลดจำนวนรถก่อนเข้า ALNS
4. ถ้า enableALNS:
     ALNSSolver::solve()                   ← Phase 2: ALNS optimization
     (multi-start: รัน n รอบด้วย seed ต่างกัน เลือก best)
   ถ้าไม่ enableALNS:
     applyTwoOpt()                          ← Phase 2: 2-opt optimization
5. orOptRelocate()                          ← Phase 3: post-optimization
6. writeResponse()                          ← Phase 4: สร้าง output
```

---

## 2. Construction Module

### `construction/adaptive_constructor.h`
| โครงสร้าง | บทบาท |
|-----------|--------|
| `ConstructedRoute` | ผลลัพธ์ 1 route — vehicle_index, nodes, trips (multi-trip), total_cost |
| `ConstructionDrop` | node ที่จัดไม่ได้ — node_id, code, detail |
| `ConstructionResult` | ผลลัพธ์ทั้งหมด — routes, drops, timed_out |

### `construction/adaptive_constructor.cpp`

| ฟังก์ชัน | บรรทัด | บทบาท |
|----------|--------|--------|
| `sortDeadline()` | 25 | แปลง deadline_min: 0 → INT_MAX (ไม่มี deadline ไว้ท้าย) |
| `priorityOrder()` | 29 | เรียง node ตามลำดับการจัด: deadline ASC → priority DESC → id ASC |
| `expired()` | 44 | เช็ค timeout — `elapsed >= limit_ms` |
| `priorityBonus()` | 51 | คำนวณ bonus สำหรับการเลือก insertion: `priority×10` + `deadline_bonus(5)` + `must_serve_bonus(100)` |
| `inserted()` | 58 | สร้าง vector ใหม่โดยแทรก node_index ที่ตำแหน่ง pos |
| `bestSingleInsertion()` | 64 | **Single node insertion** — brute-force ทุก vehicle × ทุกตำแหน่ง หา position ที่ `delta_cost - priorityBonus` ต่ำสุดที่ feasible |
| `bestMultiTripInsertion()` | 125 | **Multi-trip insertion** — ลองทุก trip + สร้าง trip ใหม่ validate ทั้ง schedule (รวม reload time) |
| `applyMultiTripInsertion()` | 203 | ใส่ node ลง schedule ตาม best candidate |
| `findPair()` | 228 | หา node คู่ (pickup/delivery) ที่มี pair_id ตรงกัน |
| `bestPairInsertion()` | 238 | **PD pair insertion** — enumerate p1 < p2 ทุกคู่ในทุก vehicle ต้อง feasible พร้อมกัน |
| `applyPairInsertion()` | 278 | ใส่ทั้ง pickup และ delivery ลง route ด้วย cost รวมที่ดีที่สุด |
| `adaptiveConstruct()` | 314 | **ฟังก์ชันหลัก** — เริ่มจาก priorityOrder() → วนลูปแต่ละ node → เลือก insertion แบบที่เหมาะสม (single/pair/multi-trip) → คืน ConstructionResult |

**วิธีเลือก insertion:**
- Enumerate ALL vehicles × ALL positions
- คำนวณ `score = delta_cost - priorityBonus(node)`
- เลือกตำแหน่งที่ score ต่ำสุดและผ่าน feasibility check (6 ข้อ)
- ถ้าไม่มีตำแหน่งไหน feasible → node กลายเป็น unassigned

---

## 3. ALNS Module

### `alns/alns.h`
| โครงสร้าง/คลาส | บทบาท |
|----------------|--------|
| `ALNSConfig` | config ทั้งหมด: segment_size(40), reaction_factor(0.1), initial_temp(100), cooling_rate(0.9995), min_temp(1.0), reheat_temp(50), enable_sector_removal(true), forbid_new_vehicle_prob(0.5) |
| `OperatorStats` | สถิติ operators: final_weights, selection_count, improve_count, best_count |
| `ALNSSolver` | ตัว solver หลัก — `solve()` รัน destroy/repair loop ด้วย simulated annealing |

### `alns/adaptive_penalty.h`
| ฟังก์ชัน | บทบาท |
|----------|--------|
| `AdaptivePenalty::update()` | ปรับ penalty coefficient ตามสัดส่วน feasible solutions ใน segment ล่าสุด — feasible ต่ำ → เพิ่ม penalty (1.2×) กระตุ้นให้หา feasible; feasible สูง → ลด penalty (0.85×) เน้น optimize cost |
| `AdaptivePenalty::coefficient()` | คืนค่า penalty ปัจจุบัน (ใช้ `cap_pen_` เป็นตัวแทน) |

**กลไก Adaptive Penalty:**
```
feasible_ratio < 0.15  →  เพิ่ม penalty ×1.2  (กดดันให้หา feasible)
feasible_ratio > 0.25  →  ลด penalty ×0.85  (เน้น minimize cost)
อื่น ๆ                  →  คงเดิม
```

Penalty ใช้คูณกับจำนวน unassigned ใน objective function → ทำให้ ALNS พยายามลด unassigned ก่อน optimize ระยะทาง

### `alns/solution.h`
| ฟังก์ชัน/โครงสร้าง | บทบาท |
|-------------------|--------|
| `VehicleTrips` | 1 คันรถ → vehicle_index + list of RouteState (trips) |
| `ALNSSolution` | solution ทั้งหมด — vehicles (แต่ละคันมีหลาย trips) + unrouted nodes + objective |
| `computeObjective()` | คำนวณ objective = sum(trip.cost) + 1000 × unrouted_count |
| `fromConstruction()` | แปลง ConstructionResult → ALNSSolution (สำหรับเริ่ม ALNS) |
| `toConstruction()` | แปลง ALNSSolution → ConstructionResult (สำหรับส่งกลับ writeResponse) |

### `alns/alns.cpp`
| ฟังก์ชัน | บรรทัด | บทบาท |
|----------|--------|--------|
| `applyTwoOptToSolution()` | 23 | เรียก `twoOptTrip()` ทุก trip แล้ว validate ทั้ง schedule |
| `ALNSSolver::solve()` | 50 | **ALNS main loop** |

**ALNS Main Loop (alns.cpp:50):**

```
1. แปลง ConstructionResult → ALNSSolution
2. เริ่ม simulated annealing: T = 100.0, cooling = 0.9995
3. แต่ละ iteration:
   a. เลือก destroy operator (weighted random จาก destroy_weights)
   b. เลือก repair operator  (weighted random จาก repair_weights)
   c. q = total_routed / 4  (clamp 4-15)
   d. destroy → repair → คำนวณ objective ใหม่ (ด้วย adaptive penalty)
   e. SA acceptance: รับเสมอถ้า delta<0; รับด้วย prob=exp(-delta/T) ถ้าแย่ลง
   f. ถ้า accept:
      - อัพเดท current solution
      - เก็บ best overall + best feasible
      - ให้คะแนน operator (best=33, better=9, accepted=13)
4. ทุก segment_size iterations:
   a. ปรับ destroy/repair weights ด้วย reaction_factor
   b. ปรับ adaptive penalty ตาม feasible_ratio
5. T *= cooling_rate; ถ้า T < min_temp → reheat
6. จนหมดเวลา → คืน best feasible (หรือ best overall ถ้าไม่เคย feasible)
7. applyTwoOptToSolution() → แปลงกลับ ConstructionResult
```

### `alns/destroy.h` + `alns/destroy.cpp`

| ฟังก์ชัน | บรรทัด | บทบาท |
|----------|--------|--------|
| `removeNode()` | 12 | เอา 1 node ออกจาก trip — re-evaluate trip; ถ้า trip ว่าง → ลบ trip; ถ้ารถไม่มี trip → ลบรถ |
| `randomRemoval()` | 48 | สุ่ม q nodes ออกจาก solution (ใช้ shuffle) |
| `worstRemoval()` | 68 | เอา q nodes ที่ removal save cost สูงสุด — คำนวณ `trip.cost - cost_without_node` เรียงจากมากไปน้อย |
| `shawRemoval()` | 109 | เอา q nodes ที่คล้ายกันมากสุด — เลือก seed node สุ่ม; คำนวณ similarity = travel_time + |tw_diff| + |demand_diff|; เอาที่คล้ายสุด |
| `priorityAwareRemoval()` | 151 | เอา q nodes ที่ priority ต่ำสุดก่อน (priority=0 ก่อน แล้ว priority=1, 2, ...) |
| `routeConsolidationDestroy()` | 172 | เลือกรถ 2 คันที่มีจำนวน node น้อยสุด → ลบ nodes ทั้งหมดจากทั้ง 2 คัน (พยายามย้าย node ไปคันอื่นใน repair) |
| `tripRemoval()` | 205 | เลือก trip ที่มีจำนวน node น้อยสุด → ลบทุก node ใน trip นั้น |
| `tagViolationRemoval()` | 227 | ตรวจสอบทุก node → ลบ node ที่ tag ไม่ตรงกับรถที่ assign |
| `lateCustomerRemoval()` | 250 | เอา q nodes ที่มาสายสุด — ใช้ forward_labels ดู `arrival - tw_end` → เรียงจากสายมากไปน้อย |
| `sectorRemoval()` | 282 | เอา nodes จาก 2 sector — เลือก seed → เอา q/2 nodes ใกล้สุดในเชิงมุม (bearing จาก depot); เลือก seed ตรงข้าม (มุมห่างสุด) → เอาอีก q/2 nodes |

### `alns/repair.h` + `alns/repair.cpp`

| ฟังก์ชัน | บรรทัด | บทบาท |
|----------|--------|--------|
| `findBestInsertion()` | 45 | **แกนกลางของ repair** — enumerate ทุก vehicle × trip × position; เลือก `delta_cost - priorityBonus` ต่ำสุดที่ feasible; รองรับทั้ง existing trip, new trip, new vehicle |
| `insertNode()` | 132 | ใส่ node ลง solution ตาม InsertionOption — รองรับ new vehicle, new trip, existing trip (พร้อม rollback ถ้า validate ล้มเหลว) |
| `greedyRepair()` | 171 | เรียง unrouted nodes ตาม deadline/priority → ใส่ทีละตัวด้วย `findBestInsertion()` |
| `priorityFirstRepair()` | 184 | เรียง unrouted nodes ตาม priority อย่างเดียว → ใส่ทีละตัว |
| `regret2Repair()` | 200 | **Regret-2 heuristic** — คำนวณ cost1 (best insertion) และ cost2 (2nd best) สำหรับทุก node; เลือก node ที่มี regret = cost2 - cost1 สูงสุดก่อน ใส่ node ที่ "เสียใจทีหลังมากสุด" ก่อน |
| `regret3Repair()` | 326 | **Regret-3 heuristic** — เหมือน regret-2 แต่ใช้ 3 อันดับแรก: regret = (cost2-cost1) + (cost3-cost1) |
| `proactiveBreakInsertion()` | 459 | ไม่ได้ใส่ unrouted node — ตรวจจับการขับต่อเนื่องเกิน 210 นาที → แยก trip ที่จุดเกิน แทรก break ระหว่าง trips |

### `alns/local_search.h` + `alns/local_search.cpp`

| ฟังก์ชัน | บทบาท |
|----------|--------|
| `tripMerge()` | รวม 2 trips ในรถคันเดียวกัน ถ้าลด cost ได้ (ลอง merge → 2-opt → validate) |
| `customerMoveAcrossTrips()` | ย้าย node ข้าม trip ในรถคันเดียวกัน ถ้าลด cost ได้ |
| `twoOptStar()` | สลับ tail segment ระหว่าง 2 trips จาก 2 คันรถ — (A[0..cut_a) + B[cut_b..]) และ (B[0..cut_b) + A[cut_a..]) |
| `relocateAcrossVehicles()` | ย้าย 1 node จากรถ A ไปแทรกที่ตำแหน่งใดๆ ในรถ B (ข้ามคัน) |
| `consolidateVehicles()` | เลือกรถที่มี node น้อยสุด → ลบรถทั้งคัน → พยายามย้ายทุก node ไปรถที่เหลือ |
| `swapStar()` | สลับ 2 nodes ระหว่าง 2 คันรถ — หา best insertion สำหรับ node_a ในรถ B และ node_b ในรถ A พร้อมกัน |
| `applyLocalSearch()` | เรียก local search ทั้งหมดตามลำดับ: tripMerge → customerMoveAcrossTrips → twoOptStar → relocateAcrossVehicles → swapStar |

---

## 4. Route Optimization Module

### `routeOpt/two_opt.cpp`
| ฟังก์ชัน | บทบาท |
|----------|--------|
| `twoOptTrip()` | **2-opt intra-route** — ลอง reverse segment (i+1..j) ทุกคู่; รับเฉพาะเมื่อ feasible และ cost ลด > 1e-6; iterate จนไม่มีการปรับปรุง |

**หลักการ 2-opt:**
```
ก่อน: depot → A → B → C → D → E → depot
       edge (A,B), (C,D)  →  reverse B-C → A → C → B → D
หลัง: depot → A → C → B → D → E → depot
```

### `routeOpt/or_opt.cpp`
| ฟังก์ชัน | บทบาท |
|----------|--------|
| `orOptRelocate()` | **Or-opt inter-route** — ดึงแต่ละ node ออกจาก trip → ลองแทรกที่ทุกตำแหน่งในทุก trip (รวมข้ามคัน) → รับเมื่อ net_gain > 1e-6; iterate จนไม่มีการปรับปรุง |

---

## 5. Feasibility Check Modules

### `feasibility/capacity.cpp`
| ฟังก์ชัน | บทบาท |
|----------|--------|
| `fitsCapacity()` | เช็ค `cluster_weight + node_weight <= vehicle_capacity` |
| `fitsMaxTasks()` | เช็ค `max_tasks == 0 || cluster_size < max_tasks` |

### `feasibility/tags.cpp`
| ฟังก์ชัน | บทบาท |
|----------|--------|
| `isTagCompatible()` | เช็คว่า vehicle มีอย่างน้อย 1 tag ตรงกับ node tag → false = incompatibility |

### `feasibility/max_distance.cpp`
| ฟังก์ชัน | บทบาท |
|----------|--------|
| `isMaxDistanceFeasible()` | เช็ค `max_distance == 0 || route_distance <= max_distance` |

### `feasibility/precedence.cpp`
| ฟังก์ชัน | บทบาท |
|----------|--------|
| `isLBPrecedenceFeasible()` | เช็ค pickup ต้องมาก่อน delivery สำหรับ order แบบ linehaul-backhaul (ไม่มี pair_id) |
| `isPDPairFeasible()` | เช็ค pickup ต้องมาก่อน delivery ของ pair_id เดียวกัน (PD pair) |

### `feasibility/shift.cpp`
| ฟังก์ชัน | บทบาท |
|----------|--------|
| `isShiftWindowFeasible()` | เช็ค `shift_end == 0 || last_depart <= shift_end` |

### `feasibility/break.cpp`
| ฟังก์ชัน | บทบาท |
|----------|--------|
| `isBreakWindowFeasible()` | เช็คว่ามี gap ระหว่าง travel segments ที่ overlap กับ break window `[break_start, break_end)` หรือไม่ |

---

## 6. Route Validator Module

### `validator/labels.h` + `validator/labels.cpp`

| โครงสร้าง/ฟังก์ชัน | บทบาท |
|-------------------|--------|
| `StopSpec` | specification ของ 1 stop — earliest/latest arrival, service time, linehaul/backhaul demand |
| `ForwardLabel` | state หลัง visit node — earliest_arrival, departure_time, load_linehaul/backhaul, time_warp, distance_m |
| `BackwardLabel` | state ก่อน visit node (สำหรับ backward pass) — latest_departure, slack |
| `RouteLabelSummary` | สรุปทั้ง route — feasible?, fail_code, max_load, time_warp, last_departure |
| `extendForwardLabel()` | คำนวณ ForwardLabel ถัดไป — arrival = max(departure+travel, tw_start); time_warp = max(0, arrival-tw_end); อัพเดท load |

**Forward Label Propagation:**
```
label[N] = extend(label[N-1], stop[N], travel[N-1→N])
  arrival = max(depart[N-1] + travel, tw_start[N])
  wait    = max(0, tw_start[N] - arrival)
  depart  = arrival + service_time[N]
  feasible = (arrival <= tw_end[N])
```

### `validator/route_state.h` + `validator/route_state.cpp`

| โครงสร้าง/ฟังก์ชัน | บทบาท |
|-------------------|--------|
| `RouteState` | state ของ 1 route/trip — nodes, distance, duration, cost, forward_labels, label_summary |
| `InsertionEval` | ผลลัพธ์การลอง insert — feasible?, delta_cost, next (RouteState ใหม่), fail_code |
| `evaluateRouteState()` | ประเมิน candidate route — validateTrips() → ถ้า feasible → สร้าง RouteState ใหม่ พร้อม forward labels |
| `evaluateInsertion()` | ประเมินการ insert 1 node ที่ตำแหน่ง position — สร้าง candidate nodes → evaluateRouteState() |
| `buildForwardLabels()` | สร้าง forward labels สำหรับทุก node ใน route (ใช้สำหรับ destroy operators เช่น lateCustomerRemoval) |

### `validator/route_validator.h` + `validator/route_validator.cpp`

| โครงสร้าง/ฟังก์ชัน | บทบาท |
|-------------------|--------|
| `StopTiming` | timing ของ 1 stop — node_index, arrival_min, depart_min |
| `RouteValidationResult` | ผลลัพธ์การ validate — feasible?, code, detail, total_distance, total_duration, total_cost, timings |
| `validateTripPrecedence()` | ตรวจสอบ PD precedence + LB precedence + pair completeness |
| `validateTrips()` | **ฟังก์ชัน validate หลัก** — ตรวจสอบครบทุก constraint: |

**ลำดับการตรวจสอบใน `validateTrips()` (6 ข้อ):**

```
1. MATRIX      — matrix_size > 0, distances/durations ขนาดถูกต้อง
2. MAX_TASKS   — จำนวน stop ใน trip ≤ vehicle.max_tasks
3. PRECEDENCE  — validateTripPrecedence() (PD pair + LB)
4. CAPACITY    — linehaul load เริ่มต้น ≤ capacity + load ระหว่างทางไม่เกิน capacity + load ไม่ติดลบ
5. TAG         — ทุก node ต้องมีอย่างน้อย 1 tag ตรงกับ vehicle
6. BREAK       — ถ้ายังไม่พักเบรกและ arrival ≥ break_start → ต้องแทรก break (เวลาเดินไป break_end)
7. TIME_WINDOW — arrival ≤ tw_end (wait ได้ถ้ามาก่อน tw_start)
8. SHIFT       — return time ≤ shift_end
9. MAX_DISTANCE — total_distance ≤ max_distance
```

ทุกครั้งที่ผ่าน:
- คำนวณ `total_distance_m` (เมตร)
- คำนวณ `total_duration_min` = return_time - shift_start
- คำนวณ `total_cost` = fixed_cost + (distance_km × cost_per_km)

ถ้า reload_min > 0: เพิ่มเวลา reload ระหว่าง trips

---

## 7. Drop Logic Module

### `drop/drop_logic.h` + `drop/drop_logic.cpp`

| ฟังก์ชัน | บทบาท |
|----------|--------|
| `normalizeDropCode()` | แปลง code ภายใน → code ที่แสดงผล: `TAG`→`TAG_MISMATCH`, `CAPACITY`→`CAPACITY_FULL`, `TIME_WINDOW`→`TIMEWINDOW_TIGHT`, `SHIFT`→`DRIVER_HOURS`, empty→`NO_FEASIBLE_INSERTION` |

---

## 8. Priority Module

### `priority/sorter.h`
| ฟังก์ชัน | บทบาท |
|----------|--------|
| `sortNodeIndices()` | เรียง node indices ตาม deadline_min ASC (0=no deadline → INT_MAX เรียงท้าย) → priority DESC → stable_sort |

ใช้ใน priority_shape_clustering เพื่อเรียงลำดับก่อนป้อนเข้า k-medoids

---

## 9. Priority Shape Clustering Module

โมดูลทดลองสำหรับการจัดกลุ่ม orders ด้วย k-medoids แบบ time-aware ก่อนส่งให้ construction

### `priority_shape_clustering/types.h`
| โครงสร้าง | บทบาท |
|-----------|--------|
| `Node` | ข้อมูล node สำหรับ clustering — id, lat/lon, weight, service_time, tw_start/end, priority, deadline_min |
| `Vehicle` | ข้อมูลรถ — id, capacity, shift_start/end |
| `Cluster` | ผลลัพธ์ 1 cluster — center_id, node_ids (visit order), total_weight, Macro Node state (E, L, S, last_node_id) |

### `priority_shape_clustering/slender_utils.h` + `slender_utils.cpp`
| ฟังก์ชัน | บทบาท |
|----------|--------|
| `calculateAngle()` | คำนวณ bearing angle (radians) จาก depot ไป node — ใช้ atan2 |
| `computeSlenderMatrix()` | สร้าง N×N similarity matrix: `delta[i][j] = 0.9×theta_diff + 0.1×rho_diff` — theta = angular distance (normalized 0-1), rho = radial distance difference |

### `priority_shape_clustering/slender_solver.h` + `slender_solver.cpp`
| ฟังก์ชัน | บทบาท |
|----------|--------|
| `SlenderSolver::plan()` | **Entry point** — สร้าง slender matrix → multi-trip K-medoids loop จน assign ครบหรือไม่มี progress |
| `SlenderSolver::runOneRound()` | 1 รอบของ K-medoids — 10 restarts แบบสุ่ม เลือก best จากจำนวน assigned nodes สูงสุด |
| `initFromMedoid()` | สร้าง MacroState เริ่มต้นจาก medoid เดี่ยว |
| `tryMerge()` | ลอง append node i ต่อท้าย sequence — คำนวณ feasibility (3 cases: hard reject / mandatory wait / overlap) |
| `assignScore()` | คะแนนการ assign node เข้า cluster: `delta + BETA×wait + ALPHA×flex_loss + LAMBDA×(4-priority)` |
| `kMedoidsIterate()` | K-medoids convergence — assign nodes → update medoids → repeat จน converged หรือครบ MAX_ITER(50) |

**Macro Node Theory (tryMerge):**
```
State: E = earliest feasible departure, L = latest feasible departure, S = accum service+travel

Case 1 (infeasible): E + S + t > l_i           → node i ไม่สามารถถึงก่อน TW ปิด
Case 2 (wait):       L + S + t < e_i           → ต้องรอ → window collapse เป็นจุดเดียว
Case 3 (overlap):    [E,L] ∩ [e_i-S-t, l_i-S-t] ≠ ∅  → feasible
```

**Multi-trip loop (SlenderSolver::plan):**
```
1. เรียง orders: deadline ASC → priority DESC
2. while (มี unassigned และมี progress):
     runOneRound() → ได้ K clusters
     อัพเดท ready_time ของแต่ละ vehicle (บวก travel กลับ depot)
     ถ้า ready_time > shift_end → vehicle ถูก block (capacity=0)
3. คืน SlenderPlan (trips_per_vehicle + unassigned)
```

### `priority_shape_clustering/route_optimizer.h`
Header-only — TSP optimization สำหรับ cluster route ด้วย MST + 2-opt:

| ฟังก์ชัน | บทบาท |
|----------|--------|
| `buildMST()` | สร้าง Minimum Spanning Tree ด้วย Prim's algorithm (priority queue) |
| `dfs()` | Depth-first traversal บน MST → ได้ initial tour |
| `getRoute()` | แปลง MST edges → adjacency tree → DFS → initial tour |
| `applySwap()` | กลับ segment `(i+1..j)` ใน route (2-opt move) |
| `twoOptPass()` | 1 pass ของ 2-opt — ลอง swap ทุกคู่ edge; รับเฉพาะที่ลด cost > 1e-9 |
| `twoOpt()` | 2-opt full loop — iterate จนไม่มี improvement หรือครบ 10,000 รอบ |
| `getRouteDistance()` | คำนวณระยะทางรวมของ route |
| `optimizeRoute()` | **Pipeline หลัก**: buildMST → getRoute → twoOpt → return `RouteResult{route, distance}` (เริ่มและจบที่ node 0) |

> **หมายเหตุ:** `routeOpt/mst2opt.cpp` เป็นไฟล์ standalone ที่มี logic คล้ายกันแต่ไม่ถูก compile เข้า solver binary ตัวใด — ใช้สำหรับทดสอบแยกเท่านั้น

---

## 11. Benchmark Harness

### `bench_gen.h` + `bench_gen.cpp`
| ฟังก์ชัน/โครงสร้าง | บทบาท |
|-------------------|--------|
| `BenchConfig` | config สร้าง random dataset — seed, num_orders, num_vehicles, depot, radius, capacity range, demand range, tag_coverage, TW settings |
| `buildRequest()` | สร้าง `SolveRequest` จาก BenchConfig — สุ่มพิกัด nodes, demand, capacity, tags, TW → สร้าง distance/duration matrices แบบ Euclidean |

### `bench_solve.cpp`
รัน benchmark 23 scenarios บันทึกผลลัพธ์เป็น JSON ที่ `test/bench_results.json`

### `bench_compare.cpp`
V1 vs V2 comparison harness — Dobj%=0 บน algorithm เดียวกัน เพื่อยืนยันว่า harness ถูกต้อง

---

## 12. Time Representation Convention

เวลาทั้งหมดในระบบใช้ **"นาทีนับจาก midnight"** (minutes from midnight):

| เวลา | ค่า |
|------|-----|
| 00:00 | 0 |
| 08:00 | 480 |
| 12:00 | 720 |
| 17:00 | 1020 |
| 24:00 | 1440 |

- `tw_start`, `tw_end` — time window ของ order
- `shift_start`, `shift_end` — shift ของพนักงานขับรถ
- `break_start`, `break_end` — break window
- `deadline_min` — deadline; 0 = ไม่มี deadline (sort ท้ายสุด)
- `service_time` — เวลาบริการที่ node (นาที)
- `travel_time` — จาก duration matrix

---

## 13. Cost Model

```
route_cost = fixed_cost_per_vehicle + (total_distance_meters / 1000) × cost_per_km
total_objective = sum(route_cost) + 1000 × unassigned_count
```

Default:
- `fixed_cost_per_vehicle` = 550 THB
- `cost_per_km` = 4.0003 THB
- `penalty_per_unassigned` = 1000

Vehicle สามารถ override fixed_cost และ cost_per_km ของตัวเองได้ (ถ้า > 0)
