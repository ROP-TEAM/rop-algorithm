# Solve Flow Analysis — rop-algorithm

## ภาพรวม Flow

```
Go: Solver.Solve()
  → GRPCSolver.Solve()  [grpc/client.go:24]
    → gRPC ส่งไป C++
      → solveRequest()  [solver_service.cpp:174]
        │
        ├── Phase 1: adaptiveConstruct()  [adaptive_constructor.cpp:316]
        │     └── จัดรถให้ทุก order (greedy insertion)
        │
        ├── Phase 2: Optimization
        │     ├── ถ้า enableALNS=true  → ALNSSolver::solve()  [alns.cpp:48]
        │     └── ถ้า enableALNS=false → applyTwoOpt() + orOptRelocate()
        │
        ├── Phase 3: Post-process → orOptRelocate()  [or_opt.cpp:77]
        │
        └── Phase 4: writeResponse()  [solver_service.cpp:67]
```

---

## Phase 1: Construction — จุดที่จัดรถให้ Orders

อยู่ที่ `adaptive_constructor.cpp:316` ฟังก์ชัน `adaptiveConstruct()`

### Step 1: เรียงลำดับ orders

`priorityOrder()` บรรทัด 29

| เกณฑ์ | ทิศทาง |
|---|---|
| Deadline | น้อย → มาก (0 = ไม่มี deadline, ไว้ท้ายสุด) |
| Priority | สูง → ต่ำ (critical=4, high=3, medium=2, low=1, ""=0) |
| node.id | น้อย → มาก (tie-break) |

### Step 2: จัดรถทีละ order ตามลำดับ

| ประเภท Order | ฟังก์ชัน | บรรทัด |
|---|---|---|
| Pickup-Delivery pair | `bestPairInsertion()` | 240-277 |
| Single node | `bestSingleInsertion()` | 64-97 |
| Multi-trip | `bestMultiTripInsertion()` | 126-203 |

### วิธีการเลือกว่าจะใส่รถคันไหน

- **Brute-force ทุกตำแหน่งในรถทุกคัน** — enumerate all vehicles × all insertion positions
- คำนวณ `delta_cost - priorityBonus(node)` สำหรับแต่ละตำแหน่ง
- `priorityBonus` = 1000 × priority_rank → high-priority orders ได้เปรียบตอนแทรก
- เลือกตำแหน่งที่ **cost ต่ำสุด** และผ่าน feasibility check
- ถ้าไม่มีรถคันไหนรับได้ → node กลายเป็น unassigned

### Feasibility Check (6 ข้อ)

`route_validator.cpp:102`

| Constraint | รายละเอียด |
|---|---|
| TAG | tag ของ order ต้องตรงกับ tag ของรถ |
| CAPACITY | น้ำหนักรวมระหว่างทางต้องไม่เกินความจุรถ |
| TIME_WINDOW | ถึงจุดรับ/ส่งภายในกรอบเวลา (TW) |
| BREAK | พักเบรกตามกฎหมาย |
| MAX_TASKS | ไม่เกินจำนวน task สูงสุดต่อเที่ยว |
| MAX_DISTANCE | ระยะทางรวมไม่เกินลิมิต |
| PD_PRECEDENCE | pickup ต้องมาก่อน delivery (สำหรับ pair orders) |

---

## Phase 2: ALNS Optimization (ถ้าเปิด)

`alns.cpp:48` — `ALNSSolver::solve()`

### พารามิเตอร์

| Parameter | Value |
|---|---|
| Initial temperature T | 100.0 |
| Cooling rate | 0.9995 |
| Min temperature | 1.0 (reheat → 10.0) |
| Segment size | 100 iterations |
| Reaction factor | 0.1 |

### แต่ละ iteration

1. **Destroy** — สุ่มเลือก 1 ใน 8 destroy operators เอา q nodes ออก
2. **Repair** — สุ่มเลือก 1 ใน 4 repair operators ใส่ unassigned nodes กลับ

### 8 Destroy Operators

`destroy.cpp`

| Operator | บรรทัด | เอาอะไรออก |
|---|---|---|
| `randomRemoval` | 47 | สุ่ม q nodes |
| `worstRemoval` | 67 | q nodes ที่ removal save cost สูงสุด |
| `shawRemoval` | 108 | q nodes ที่คล้ายกันมากสุด (เวลารับส่ง, ระยะทาง, ปริมาณ) |
| `priorityAwareRemoval` | 150 | q nodes ที่ priority ต่ำสุด |
| `routeConsolidationDestroy` | 171 | รถ 2 คันที่มี node น้อยสุด — ล้างทั้งคัน |
| `tripRemoval` | 204 | trip ที่มี node น้อยสุด — ลบทั้ง trip |
| `tagViolationRemoval` | 226 | nodes ที่ tag ไม่ตรงกับรถ |
| `lateCustomerRemoval` | 249 | q nodes ที่มาสายสุด (arrival > tw_end) |

### 4 Repair Operators

`repair.cpp`

| Operator | บรรทัด | ใส่ยังไง |
|---|---|---|
| `greedyRepair` | 171 | เรียงตาม deadline/priority → ใส่แบบ greedy |
| `priorityFirstRepair` | 184 | เรียงตาม priority อย่างเดียว → ใส่แบบ greedy |
| `regret2Repair` | 200 | เลือก node ที่มี regret สูงสุด (cost2 − cost1) — ป้องกันเสียใจทีหลัง |
| `proactiveBreakInsertion` | 324 | ไม่ได้ใส่ node ใหม่ — แทรกเบรกเมื่อขับเกิน 210 นาที |

### `findBestInsertion()` — แกนหลักของ repair

`repair.cpp:45-130`

- Enumerate ทุก vehicle × trip × position (เหมือน construction)
- เลือก `delta_cost - priorityBonus` ต่ำสุดที่ feasible

### Acceptance Criteria (Simulated Annealing)

- `delta < 0` → รับเสมอ (ดีขึ้น)
- `delta > 0` → รับด้วยความน่าจะเป็น `exp(-delta / T)`
- เก็บ best solution แยกไว้ตลอด

### Adaptive Weight Update

ทุก 100 iterations:

- คะแนนตามคุณภาพ: global best > better than current > accepted worse
- ปรับน้ำหนัก operator ด้วย reaction factor = 0.1
- ปรับ adaptive penalty จากสัดส่วน feasible solutions

---

## Phase 3: Post-optimization

### 2-opt

`routeOpt/two_opt.cpp:8`

- สำหรับทุก trip, ลองสลับ edge `(i, i+1)` กับ `(j, j+1)` แล้ว reverse segment ระหว่างนั้น
- ใช้เฉพาะกรณี `enableALNS=false` (ALNS มี 2-opt ในตัวอยู่แล้ว)

### Or-opt

`routeOpt/or_opt.cpp:77`

- ดึงแต่ละ node ออกจาก trip แล้วลองใส่ที่ตำแหน่งอื่น (รวมถึงข้ามรถ)
- Iterate จนไม่มีการปรับปรุง
- ใช้ทั้งสอง path (ALNS และ non-ALNS)

---

## Phase 4: Scoring & Output

`writeResponse()` — `solver_service.cpp:67`

```
TotalCost = sum(route_costs) + 1000 × unassigned_count
```

Route cost = `fixed_cost(550) + distance_km × cost_per_km(4.0003)`

### Status

| Condition | Status |
|---|---|
| Construction timeout | `TIMEOUT` |
| 0 routes + มี unassigned | `INFEASIBLE` |
| ปกติ | `OK` |

---

## สรุปจุดที่มีการจัดรถ

| จุด | ไฟล์:บรรทัด | วิธี |
|---|---|---|
| Construction | `adaptive_constructor.cpp:316` | Greedy insertion — enumerate ทุกคัน, เลือก cost ต่ำสุด |
| ALNS repair | `repair.cpp:45` | `findBestInsertion()` — เหมือน construction + regret-based |
| Or-opt | `routeOpt/or_opt.cpp:77` | Relocate ข้ามคัน — เอาออกแล้วหาที่ใหม่ที่ดีกว่า |
