# rop-algorithm

Pure Go module สำหรับ Vehicle Routing Problem (VRP) optimization

---

## โครงสร้าง Module

```
rop-algorithm/
├── graph/
│   ├── matrix.go          — DistanceMatrix interface + GoogleMapsMatrix
│   ├── matrix_types.go    — Request/Response structs ครบทุก field
│   └── matrix_test.go     — Integration tests (ต้องการ API Key)
├── core/
│   ├── constraint/        — Feasibility checker (Phase 2)
│   ├── priority/          — Node sorting (Phase 2)
│   └── timeWindow/        — Time window validation (Phase 2)
├── model/                 — Vehicle/Node models (Phase 2)
└── solver/                — ALNS main loop (Phase 2)
```

---

## Distance Matrix API (graph/)

ใช้ Google Maps Distance Matrix API เพื่อดึง travel time และ distance จริงบนถนน
แทนการคำนวณ haversine (เส้นตรง) ที่ไม่สะท้อนสภาพจราจรจริง

### การใช้งาน

```go
import "github.com/ROP-TEAM/rop-algorithm/graph"

m, err := graph.NewGoogleMapsMatrix(apiKey)
if err != nil { ... }

locs := []graph.Location{
    {Lat: 13.7563, Lng: 100.5018}, // Siam
    {Lat: 13.7469, Lng: 100.5346}, // Asok
    {Lat: 13.7308, Lng: 100.5418}, // On Nut
}

// ไม่คำนวณ traffic (duration ปกติ)
durations, distances, err := m.BuildMatrix(ctx, locs, graph.MatrixOptions{})

// คำนวณ traffic ณ เวลาที่ระบุ (departure_time)
opts := graph.MatrixOptions{
    DepartureTime: time.Date(2026, 4, 21, 8, 0, 0, 0, bangkokTZ).Unix(),
    TrafficModel:  "best_guess",
}
durations, distances, err := m.BuildMatrix(ctx, locs, opts)
```

- `durations[i][j]` = เวลาเดินทางจาก i → j (**นาที**)
- `distances[i][j]` = ระยะทางจาก i → j (**เมตร**)
- diagonal `[i][i]` = 0 เสมอ

---

## MatrixOptions — parameters ที่รองรับ

| Field | Type | Default | หมายเหตุ |
|---|---|---|---|
| `DepartureTime` | `int64` | `0` | Unix timestamp; 0 = ใช้ duration ปกติ |
| `TrafficModel` | `string` | `"best_guess"` | ใช้คู่กับ DepartureTime |
| `Avoid` | `[]string` | `nil` | `"tolls"`, `"highways"`, `"ferries"` |
| `Mode` | `string` | `"driving"` | `"driving"`, `"walking"`, `"bicycling"` |

### TrafficModel
| Value | ความหมาย |
|---|---|
| `best_guess` | ผสม historical + live traffic (default) |
| `pessimistic` | เวลามากสุด (worst case) |
| `optimistic` | เวลาน้อยสุด (best case) |

---

## API Limits

| เงื่อนไข | Limit |
|---|---|
| ไม่มี `DepartureTime` | 25 × 25 = 625 elements/request |
| มี `DepartureTime` | **10 × 10 = 100 elements/request** |
| Rate limit | 60,000 elements/นาที |
| ราคา | $5 / 1,000 elements (10,000 ฟรี/เดือน) |

BuildMatrix จัดการ batching อัตโนมัติ — ส่ง locs ได้ไม่จำกัดจำนวน

---

## การรัน Tests

```bash
cd rop-algorithm
$env:GOOGLE_MAPS_API_KEY="<your-key>"
go test ./graph/... -v -timeout 60s
```

ต้องเปิด **Distance Matrix API** ใน Google Cloud Console และเปิด Billing

---

## Cache Strategy — การเลือก approach สำหรับทีม

> เนื่องจาก `BuildMatrix` สำหรับ 30 locations ใช้เวลา ~10 วินาที (9 API requests)
> และ traffic เปลี่ยนตามช่วงเวลา/วัน จึงต้องการ caching strategy ที่เหมาะสม

### กรณีใช้งาน: วางแผนวันนี้ ส่งพรุ่งนี้

```
departure_time = unix timestamp ของเวลาออกเดินทางพรุ่งนี้จริงๆ
เช่น "21 เม.ย. 08:00 น." → API คืน duration จาก historical traffic ของช่วงนั้น
```

---

### แนวทาง 1 — ไม่ Cache (เรียก API ทุกครั้ง)

```
ข้อดี:  ข้อมูลแม่นที่สุด real-time
ข้อเสีย: ช้า (~10s/plan), ค่าใช้จ่ายสูงถ้า replan บ่อย
เหมาะกับ: prototype / traffic น้อย
```

---

### แนวทาง 2 — Cache + TTL เดียว (Simple TTL)

```
cache key:  SHA256(locations + mode + avoid)
TTL:        2-4 ชั่วโมง

ข้อดี:   ง่าย implement
ข้อเสีย: ไม่แยก traffic ตามเวลา — cache เช้าอาจถูกใช้ตอนเย็น (rush hour)
เหมาะกับ: ไม่ต้องการ traffic-aware routing
```

---

### แนวทาง 3 — Cache แยก Time Slot (4 ช่วง)

```
cache key:  SHA256(locations + slot + mode + avoid)
slots:      morning(06-09) → TTL 1h
            midday(09-16)  → TTL 4h
            evening(16-20) → TTL 1h
            night(20-06)   → TTL 8h

ข้อดี:   ใช้ซ้ำข้ามวันได้ (วันธรรมดา traffic ซ้ำกัน)
ข้อเสีย: วันธรรมดา ≠ วันหยุด อาจคลาดเคลื่อน
เหมาะกับ: routing ประจำสัปดาห์ที่ traffic คาดเดาได้
```

---

### แนวทาง 4 — Cache แยก Slot + Date (แนะนำ)

```
cache key:  SHA256(locations + "2026-04-21-morning" + mode + avoid)
TTL:        เหมือน Approach 3 แต่ผูกกับวันที่จริง

ข้อดี:   แม่นที่สุดในบรรดา cache approaches
         replan วันเดิมหลายครั้ง → cache hit ทันที
         วันหยุด vs วันธรรมดาไม่ปะปนกัน
ข้อเสีย: cache hit ข้ามวันไม่ได้ → storage โตขึ้นทุกวัน (ต้องมี cleanup job)
เหมาะกับ: next-day planning ที่ต้องการความแม่นยำ
```

---

### เปรียบเทียบสรุป

| | ไม่ Cache | Simple TTL | Time Slot | Slot + Date |
|---|---|---|---|---|
| ความแม่น | สูงสุด | ต่ำ | ปานกลาง | สูง |
| Speed (replan) | ช้า | เร็ว | เร็ว | เร็ว |
| Cost | สูง | ต่ำ | ต่ำ | ต่ำ |
| Complexity | ต่ำ | ต่ำ | ปานกลาง | ปานกลาง |
| Storage | ไม่ใช้ | น้อย | น้อย | โตทุกวัน |
| Cross-day reuse | — | ✅ | ✅ | ❌ |

---

## Environment Variables

```env
GOOGLE_MAPS_API_KEY=   # ใน rop-backend/.env
```

API Key คนละตัวกับ `GOOGLE_CLIENT_ID` (OAuth) — ต้องเปิด Distance Matrix API แยก
