# Scoring Formulas — rop-algorithm/core

หลักการคิดคะแนนของทุกฟังก์ชันที่ใช้ในการ optimize เรียงตาม flow การทำงาน

---

## 1. Cost Model พื้นฐาน

```
route_cost  = fixed_cost + (distance_meters / 1000) × cost_per_km
objective   = Σ route_cost  +  1000 × unassigned_count
```

| Parameter | Default | ความหมาย |
|-----------|---------|----------|
| `fixed_cost` | 550 THB | ต้นทุนคงที่ต่อคันรถ |
| `cost_per_km` | 4.0003 THB | ต้นทุนผันแปรต่อกิโลเมตร |
| `penalty_per_unassigned` | 1000 | penalty ต่อ 1 order ที่จัดไม่ได้ |

---

## 2. Construction — Insertion Scoring

### เกณฑ์เลือกตำแหน่งแทรก

```
score = delta_cost - priorityBonus(node)
```

เลือกตำแหน่งที่ **score ต่ำสุด** และผ่าน feasibility check ทั้ง 6 ข้อ

### priorityBonus()

```
priorityBonus = (priority × 10)      // critical=40, high=30, medium=20, low=10, none=0
              + (has_deadline ? 5 : 0)
              + (must_serve   ? 100 : 0)
```

| Element | ค่า | เหตุผล |
|---------|-----|--------|
| `priority × 10` | 0–40 | high-priority ได้เปรียบตอนแทรก |
| deadline bonus | 5 | order ที่มี deadline ได้ priority เหนือ order ไม่มี deadline |
| must_serve bonus | 100 | การันตีว่า must_serve จะถูกแทรกก่อนเสมอ |

### delta_cost

```
delta_cost = cost_after_insertion - cost_before_insertion
```

คือต้นทุนที่เพิ่มขึ้นจากการแทรก node เข้าไปใน route

### ตัวอย่าง

```
node A: priority=4 (critical), deadline=1020, demand=5
node B: priority=0 (none),     deadline=0,    demand=5

delta_cost สำหรับทั้งคู่ = 50 THB (เท่ากัน)

score(A) = 50 - (40 + 5 + 0) = 5   ← ถูกเลือกก่อน
score(B) = 50 - (0  + 0 + 0) = 50
```

---

## 3. ALNS — Acceptance Criteria (Simulated Annealing)

### อุณหภูมิ

```
T₀     = 100.0                          อุณหภูมิเริ่มต้น
T      = T × 0.9995                    ลดลงทุก iteration
T_min  = 1.0                            อุณหภูมิต่ำสุด (ถึงแล้ว reheat)
T_reheat = 50.0                          อุณหภูมิหลัง reheat
```

### การยอมรับ solution ใหม่

```
delta = candidate.objective - current.objective

accept = (delta < 0)                           // ดีขึ้น → รับเสมอ
      OR (random(0,1) < exp(-delta / T))       // แย่ลง → รับด้วยความน่าจะเป็น
```

| T | delta=+10 | delta=+50 | delta=+100 |
|---|-----------|-----------|------------|
| 100 | 90.5% | 60.7% | 36.8% |
| 50 | 81.9% | 36.8% | 13.5% |
| 10 | 36.8% | 0.7% | 0.005% |
| 1 | 0.005% | ~0% | ~0% |

> ช่วงแรก (T สูง) รับ solution แย่ได้บ่อย → สำรวจ search space กว้าง
> ช่วงหลัง (T ต่ำ) รับแต่ solution ดี → converge สู่ local optimum

---

## 4. ALNS — Adaptive Operator Weight

### การให้คะแนน operator

ทุก iteration ที่ solution ถูก accept:

| Condition | คะแนน |
|-----------|-------|
| ได้ global best ใหม่ | `score_best = 33` |
| ดีขึ้นกว่า current (แต่ไม่ใช่ best) | `score_better = 9` |
| แย่ลงแต่ accept ได้ | `score_accepted = 13` |
| ไม่ถูก accept | 0 |

### การปรับน้ำหนัก (ทุก 40 iterations)

```
สำหรับแต่ละ operator i:
  ถ้า destroy_use[i] > 0:
    avg_score = destroy_score_sum[i] / destroy_use[i]
    w_new = (1 - 0.1) × w_old  +  0.1 × avg_score
```

| Parameter | Value | ความหมาย |
|-----------|-------|----------|
| `segment_size` | 40 | ปรับน้ำหนักทุกกี่ iteration |
| `reaction_factor` | 0.1 | น้ำหนักใหม่มีผล 10% (EMA — exponential moving average) |

---

## 5. ALNS — Adaptive Penalty

### เป้าหมาย

กดดันให้ ALNS หา feasible solution (unassigned = 0) ก่อน แล้วค่อย optimize cost

### กลไก

```
target_feasible_ratio = 0.20   // ต้องการให้ 20% ของการ accept เป็น feasible

ทุก segment:
  feasible_ratio = seg_feasible / seg_total

  ถ้า feasible_ratio < 0.15  →  penalty × 1.2   (กดดันให้ reduce unassigned)
  ถ้า feasible_ratio > 0.25  →  penalty × 0.85   (ผ่อน penalty เน้น optimize cost)
  อื่น ๆ                     →  คงเดิม
```

### Penalty Floors (ค่อยๆ ลดลงทุก segment)

```
min_cap  = max(10, min_cap  × 0.98)
min_tw   = max(10, min_tw   × 0.98)
min_ot   = max(5,  min_ot   × 0.98)
```

### Objective พร้อม Adaptive Penalty

```
ALNS_objective = Σ trip.cost  +  penalty_coefficient × unrouted_count
```

โดย `penalty_coefficient` เริ่มที่ 1000 และปรับตาม `cap_pen_`

---

## 6. Destroy Operators — Scoring

### 6.1 randomRemoval
```
ไม่มีการคิดคะแนน — สุ่มล้วน
เลือก: shuffle ทุก node → เอา q ตัวแรก
```

### 6.2 worstRemoval
```
สำหรับทุก node n ใน solution:
  cost_without_n = evaluate(route - {n})
  delta[n]       = trip_cost - cost_without_n

เรียง delta จากมากไปน้อย → เอา q ตัวแรก
```
> เอา node ที่ "cost สูง" ออกก่อน — การมีอยู่ของมันทำให้ route แพง

### 6.3 shawRemoval
```
1. สุ่ม seed node
2. สำหรับทุก node อื่น:
     similarity = travel_time(seed, node)
                + |tw_end(seed) - tw_end(node)|
                + |demand(seed) - demand(node)|
3. เรียง similarity จากน้อยไปมาก → เอา q ตัวแรก
```
> เอา node ที่ "คล้ายกัน" ออก → repair มีโอกาสสลับที่กันได้ดี

### 6.4 priorityAwareRemoval
```
เรียงตาม priority จากน้อยไปมาก → เอา q ตัวแรก
```
> เอา low-priority ออกก่อน — โอกาสถูกทิ้งไว้ unassigned

### 6.5 routeConsolidationDestroy
```
สำหรับทุก vehicle:
  total_nodes[v] = Σ trip.nodes.size()

เลือก 2 vehicles ที่ total_nodes น้อยสุด → ลบทุก node ใน 2 คันนั้น
```
> พยายามกำจัดรถที่มีงานน้อย → repair จะย้าย node ไปคันอื่น

### 6.6 tripRemoval
```
เลือก trip ที่มี nodes.size() น้อยสุด (แต่ > 0) → ลบทั้ง trip
```
> กำจัด trip ที่ไม่คุ้มค่า

### 6.7 tagViolationRemoval
```
สำหรับทุก node:
  ถ้า !isTagCompatible(vehicle, node) → เอาออกทันที (ทุกตัว)
```
> ไม่ใช้ q — เอาออกทั้งหมดที่ tag ไม่ตรง

### 6.8 lateCustomerRemoval
```
สำหรับทุก node:
  arrival = forward_labels[pos].earliest_arrival
  tw_end  = node.tw_end (default 1440)
  lateness = arrival - tw_end

เฉพาะ node ที่ lateness > 0: เรียงจากมากไปน้อย → เอา q ตัวแรก
```
> เอา node ที่ "มาสาย" ออกก่อน

### 6.9 sectorRemoval
```
1. คำนวณมุม (bearing) จาก depot ไปทุก node
2. สุ่ม seed₁
3. เอา q/2 nodes ที่มุมใกล้ seed₁ ที่สุด (angular distance)
4. หา seed₂ = node ที่มุมห่างจาก seed₁ มากสุดในบรรดาที่เหลือ
5. เอา q - q/2 nodes ที่มุมใกล้ seed₂ ที่สุด
```
> เอาออกเป็น sector ทางภูมิศาสตร์ — repair มีโอกาส regroup ดี

---

## 7. Repair Operators — Scoring

### แกนกลาง: findBestInsertion()

```
สำหรับทุก vehicle × trip × position:
  delta_cost = cost_after - cost_before
  score      = delta_cost - priorityBonus(node)

  (สำหรับ new trip/new vehicle ด้วย — ผ่าน validateTrips ทั้ง schedule)

เลือกตำแหน่งที่ score ต่ำสุด
```

### 7.1 greedyRepair
```
1. เรียง unrouted: deadline ASC → priority DESC
2. สำหรับแต่ละ node (ตามลำดับ):
     opt = findBestInsertion(sol, node)
     insert(node, opt.best)
```
> ใส่ตามลำดับ priority/deadline — คล้าย construction

### 7.2 priorityFirstRepair
```
1. เรียง unrouted: priority DESC อย่างเดียว
2. สำหรับแต่ละ node:
     opt = findBestInsertion(sol, node)
     insert(node, opt.best)
```
> ใส่ high-priority ก่อนเสมอ — deadline ไม่มีผลต่อลำดับ

### 7.3 regret2Repair
```
while (มี unrouted):
  สำหรับทุก node ใน unrouted:
    หา best₁ (ตำแหน่งที่ score ต่ำสุด) และ best₂ (ต่ำสุดอันดับ 2)
    regret = best₂ - best₁

  เลือก node ที่ regret สูงสุด → insert → วนต่อ
```
> "ถ้าไม่ใส่ node นี้ตอนนี้ จะเสียใจทีหลัง" — ป้องกัน greedy myopic

### 7.4 regret3Repair
```
while (มี unrouted):
  สำหรับทุก node:
    หา best₁, best₂, best₃
    regret = (best₂ - best₁) + (best₃ - best₁)

  เลือก node ที่ regret สูงสุด → insert → วนต่อ
```
> เพิ่ม best₃ เพื่อความแม่นยำมากขึ้น — ต้นทุนคำนวณสูงกว่า

### 7.5 proactiveBreakInsertion
```
สำหรับทุก trip ในทุก vehicle:
  cumulative_drive = 0
  สำหรับทุก edge (i-1 → i):
    cumulative_drive += travel_time(i-1, i)
    ถ้า cumulative_drive > 210 นาที:
      excess = cumulative_drive - 210
      จำตำแหน่งที่ excess สูงสุด (worst_pos)

  ถ้า worst_pos มีค่า:
    แยก trip เป็น 2 ส่วนที่ worst_pos
    evaluate ทั้ง 2 ส่วน → feasible ทั้งคู่ → apply
```
> แทรก break โดยอ้อม — แยก trip รถจะได้พักระหว่าง trip

---

## 8. Local Search — Scoring

ทุกตัวใช้หลักการ **First Improvement** — รับทันทีที่ cost ลด > 1e-6

### 8.1 tripMerge
```
ลอง merge 2 trips ในรถคันเดียวกัน:
  merged_nodes = trip₁.nodes + trip₂.nodes
  ลอง 2-opt บน merged
  new_cost = opt.cost

  accept ถ้า: new_cost < (trip₁.cost + trip₂.cost) - 1e-6
```

### 8.2 customerMoveAcrossTrips
```
ลองย้าย 1 node ระหว่าง 2 trips ในรถเดียวกัน:
  src'  = trip₁ - {node}
  dst'  = trip₂ + {node} (ลองทุกตำแหน่งแทรก)
  new_cost = evaluate(src') + evaluate(dst')

  accept ถ้า: new_cost < (trip₁.cost + trip₂.cost) - 1e-6
```

### 8.3 twoOptStar (2-opt*)
```
ลองสลับ tail segment ระหว่าง 2 คันรถ:
  new_a = A[0..cut_a) + B[cut_b..end]
  new_b = B[0..cut_b) + A[cut_a..end]

  new_cost = evaluate(new_a) + evaluate(new_b)
  old_cost = trip_a.cost + trip_b.cost

  accept ถ้า: new_cost < old_cost - 1e-6
```

### 8.4 relocateAcrossVehicles
```
สำหรับทุก node ในรถ A:
  ลองแทรกที่ทุกตำแหน่งในรถ B:
    src'  = trip_a - {node}
    dst'  = trip_b + {node}
    new_cost = evaluate(src') + evaluate(dst')
    old_cost = trip_a.cost + trip_b.cost

    accept ทันทีที่: new_cost < old_cost - 1e-6
```

### 8.5 consolidateVehicles
```
1. เรียง vehicles ตาม total_nodes ASC (น้อยสุดก่อน)
2. เลือก vehicle ที่มี node น้อยสุด → ลบทั้งคัน
3. ทุก node จากคันที่ถูกลบ → greedy insert เข้ารถที่เหลือ
4. new_objective = computeObjective(ใหม่)
5. accept ถ้า: new_objective < old_objective - 1e-6
```

### 8.6 swapStar
```
ลองสลับ node_a (รถ A) กับ node_b (รถ B):

  A' = (A - {node_a}) + {node_b}  (หาตำแหน่งดีสุดใน A)
  B' = (B - {node_b}) + {node_a}  (หาตำแหน่งดีสุดใน B)

  new_cost = evaluate(A') + evaluate(B')
  old_cost = sum(trips ใน A) + sum(trips ใน B)

  accept ถ้า: new_cost < old_cost - 1e-6
```

---

## 9. Route Optimization — Scoring

### 9.1 twoOptTrip (2-opt intra-route)

```
while improved:
  สำหรับทุกคู่ edge (i, i+1) และ (j, j+1) ที่ j > i+1:
    candidate = reverse(nodes[i+1 .. j])
    new_cost  = evaluateRouteState(candidate).cost

    accept ถ้า: new_cost < current_cost - 1e-6
    (first improvement — รับทันทีแล้วเริ่มใหม่)
```

### 9.2 orOptRelocate (Or-opt inter-route)

```
while improved:
  สำหรับทุก node ในทุก trip ของทุก vehicle:
    // Step 1: เอา node ออก
    src_after     = removeNode(src, node)
    src_cost_after = evaluate(src_after)
    removal_gain   = src.cost - src_cost_after

    // Step 2: ลองแทรกที่ทุกตำแหน่งในทุก vehicle
    สำหรับทุก dst vehicle × trip × position:
      dst_after      = insertNode(dst, node, position)
      dst_cost_after = evaluate(dst_after)
      insertion_cost = dst_cost_after - dst.cost

      net_gain = removal_gain - insertion_cost
      accept ถ้า: net_gain > 1e-6
```

---

## 10. SlenderSolver — Clustering Scoring

### 10.1 Slender Matrix (ความคล้าย)

```
theta_diff(i,j) = (π - |π - |θᵢ - θⱼ||) / π    // angular distance, normalized [0,1]
rho_diff(i,j)   = |ρᵢ - ρⱼ| / max_rho           // radial distance, normalized [0,1]

delta[i][j] = 0.9 × theta_diff  +  0.1 × rho_diff
```

### 10.2 Macro Node Merge — tryMerge()

```
State: E (earliest departure), L (latest departure), S (accum time)

Case 1 (infeasible):  E + S + t > l_i           → node i ถึงไม่ทัน TW ปิด
Case 2 (wait):        L + S + t < e_i           → ต้องรอ → E' = L' = L
Case 3 (overlap):     [E,L] ∩ [e_i-S-t, l_i-S-t] ≠ ∅

w_plus    = max(0, e_i - (L + S + t))           // mandatory wait time
flex_loss = max(0, (e_i - S - t) - E)           // window หดจากด้านซ้าย
          + max(0, L - (l_i - S - t))            // window หดจากด้านขวา
```

### 10.3 Assignment Score

```
assignScore = delta[node][medoid]
            + 0.01 × (w_plus    / 480)    // BETA:  penalty จาก wait time
            + 0.10 × (flex_loss / 480)    // ALPHA: penalty จาก flexibility loss
            + 0.02 × (4 - priority)       // LAMBDA: penalty จาก priority ต่ำ
```

| Coefficient | Value | ความหมาย |
|-------------|-------|----------|
| BETA | 0.01 | น้ำหนัก wait time (ต่ำ — wait นิดหน่อยไม่เป็นไร) |
| ALPHA | 0.10 | น้ำหนัก flexibility loss (สูง — ไม่อยากให้ window หด) |
| LAMBDA | 0.02 | น้ำหนัก priority (กลาง — priority มีผลแต่ไม่ dominate) |
| TIME_SCALE | 480 | normalize เวลา (8 ชม.) ให้อยู่ในช่วง [0,1] |

### 10.4 K-medoids Selection

```
สำหรับแต่ละ cluster:
  center ใหม่ = node ที่ minimize Σ delta[candidate][member] สำหรับทุก member ใน cluster

  iterate จน center ไม่เปลี่ยน หรือครบ 50 รอบ
```

### 10.5 Multi-Restart Selection

```
รัน 10 restarts ด้วย random seed ต่างกัน
เลือก restart ที่:
  1. assigned_nodes สูงสุด
  2. ถ้าเท่ากัน → total_delta_to_medoid ต่ำสุด
```

---

## 11. 2-opt TSP (route_optimizer.h)

```
edge_swap_gain = adj[a][c] + adj[b][d] - adj[a][b] - adj[c][d]

accept ถ้า: adj[a][c] + adj[b][d] < adj[a][b] + adj[c][d] - 1e-9
```

โดย a=route[i], b=route[i+1], c=route[j], d=route[j+1]

---

## 12. สรุป Tolerance/Threshold

| จุด | Threshold | หมายเหตุ |
|-----|-----------|----------|
| ALNS acceptance | delta < 0 (exact) | ไม่มี tolerance |
| Local search | 1e-6 | cost ต้องลดอย่างมีนัยสำคัญ |
| 2-opt | 1e-6 | ป้องกัน infinite loop จาก floating point |
| Or-opt | 1e-6 | net_gain ต้อง > 1e-6 |
| TSP 2-opt | 1e-9 | tighter tolerance (ระยะทางใน matrix) |
| ALNS temperature | T_min = 1.0 | ต่ำกว่านี้ → reheat |
