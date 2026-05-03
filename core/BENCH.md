# Benchmark Harness

เครื่องมือสำหรับรันสถานการณ์สมมติและเก็บผลลัพธ์ของ solver เพื่องานวิจัย

---

## Quick Start

```bash
cd rop-algorithm/core
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target bench_solve
./build/bench_solve
# ผลลัพธ์อยู่ที่ test/bench_results.json
```

---

## โครงสร้างไฟล์

```
core/
├── bench_gen.h          ← BenchConfig struct + buildRequest() — ตัว "plug" หลัก
├── bench_gen.cpp        ← random data generator (ไม่ต้องแก้)
├── bench_solve.cpp      ← runner ตัวอย่าง พร้อม 23 scenarios
└── test/
    └── bench_results.json
```

---

## วิธีเสียบ Research ใหม่

สร้างไฟล์ `my_research_bench.cpp` ใน `core/` แล้วเพิ่ม target ใน `CMakeLists.txt`

### 1. สร้างไฟล์ bench ของตัวเอง

```cpp
// my_research_bench.cpp
#include "bench_gen.h"
#include "solver_service.h"
#include <chrono>
#include <iostream>

int main() {
    // สร้าง scenario ด้วย BenchConfig
    BenchConfig cfg{
        /*seed*/         42,
        /*num_orders*/   100,
        /*num_vehicles*/ 10,
        /*depot_lat*/    16.4442,
        /*depot_lon*/    102.8352,
        /*radius_deg*/   0.09,
        /*cap_min*/      50,
        /*cap_max*/      80,
        /*demand_min*/   2,
        /*demand_max*/   8,
        /*max_dist_frac*/0.0,
        /*max_dist_m*/   0.0,
        /*tag_coverage*/ 1.0,
        /*enable_tw*/    false,
        /*tw_width_min*/ 0,
        /*tw_width_max*/ 0,
    };

    auto req = buildRequest(cfg);   // ← ได้ SolveRequest พร้อม matrices

    SolverServiceImpl service;
    solver::SolveResponse resp;

    auto t0 = std::chrono::steady_clock::now();
    service.Solve(nullptr, &req, &resp);
    double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();

    // วัดผลอะไรก็ได้ที่ต้องการวิจัย
    std::cout << resp.status() << " | " << ms << " ms\n";
    return 0;
}
```

### 2. เพิ่ม target ใน CMakeLists.txt

```cmake
add_executable(my_research_bench
    my_research_bench.cpp
    bench_gen.cpp
    solver_service.cpp
    priority_shape_clustering/slender_utils.cpp
    priority_shape_clustering/slender_solver.cpp
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)
target_link_libraries(my_research_bench PRIVATE gRPC::grpc++ protobuf::libprotobuf)
target_include_directories(my_research_bench PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
```

### 3. Build และรัน

```bash
cmake --build build --target my_research_bench
./build/my_research_bench
```

---

## BenchConfig Fields

| Field | Type | คำอธิบาย |
|-------|------|-----------|
| `seed` | int | random seed — ค่าเดิมให้ผลเดิมทุกครั้ง |
| `num_orders` | int | จำนวน order ที่สร้าง |
| `num_vehicles` | int | จำนวนรถ |
| `depot_lat/lon` | double | พิกัด depot |
| `radius_deg` | double | รัศมีกระจาย order รอบ depot (หน่วย degree) |
| `cap_min/max` | int | ช่วง capacity ของรถ (สุ่มแบบ uniform) |
| `demand_min/max` | int | ช่วง demand ของแต่ละ order |
| `max_dist_frac` | double | สัดส่วนรถที่มี max_distance constraint (0.0–1.0) |
| `max_dist_m` | double | ค่า max_distance ในหน่วยเมตร |
| `tag_coverage` | double | ความน่าจะเป็นที่รถแต่ละคันจะรองรับ tag แต่ละประเภท (0.0–1.0) |
| `enable_tw` | bool | เปิด/ปิด time window บน order |
| `tw_width_min/max` | int | ช่วงความกว้างของ time window (นาที) |
| `num_clusters` | int | 0 หรือ 1 = กระจายแบบ uniform; >1 = จัดกลุ่มภูมิศาสตร์ |
| `cluster_spread_deg` | double | รัศมีการกระจายภายใน cluster (degree) |

> Tags ที่ใช้: `fragile`, `heavy`, `express`
> เวลาทั้งหมดเป็น "นาทีนับจาก midnight" เช่น 8:00 = 480

---

## Output Format (test/bench_results.json)

```json
[
  {
    "name": "baseline",
    "n": 100,
    "v": 10,
    "status": "OK",
    "assigned": 100,
    "total": 100,
    "objective": 280384.12,
    "elapsed_ms": 0.660,
    "routes": [
      {"vehicle_id": "V-1", "stops": 10, "distance": 30107.01, "duration": 129},
      ...
    ],
    "unassigned": []
  },
  ...
]
```

| Field | คำอธิบาย |
|-------|-----------|
| `status` | `OK` = assign ครบ, `INFEASIBLE` = มี order ค้าง |
| `assigned` | จำนวน order ที่ assign ได้ |
| `objective` | total distance (เมตร) |
| `elapsed_ms` | เวลา solve ในหน่วย ms |
| `routes[].stops` | จำนวน stop ในเส้นทางนั้น |
| `routes[].duration` | เวลารวมของเส้นทาง (นาที) |
| `unassigned` | รายการ order ID ที่ไม่ถูก assign |
