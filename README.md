# rop-algorithm

Pure Go module สำหรับ Vehicle Routing Problem (VRP) optimization

---

## โครงสร้าง Module

```
rop-algorithm/
├── graph/
│   ├── matrix.go          — Raw HTTP executor + compatibility wrapper
│   ├── type_aliases.go    — Compatibility aliases to model types
│   ├── matrix_cache.go    — Cache logic + in-memory cache
│   └── matrix_observability.go — Event hook + metrics collector
├── model/
│   ├── matrix.go          — Distance matrix request/response/result structs
│   ├── matrix_cache.go    — Cache config/key policy structs
│   └── car.go
├── test/
│   ├── matrix_test.go     — Graph integration/unit tests
│   └── matrix_cache_test.go
├── core/
│   ├── constraint/        — Feasibility checker (Phase 2)
│   ├── priority/          — Node sorting (Phase 2)
│   └── timeWindow/        — Time window validation (Phase 2)
└── solver/                — ALNS main loop (Phase 2)
```

---

## Distance Matrix API (graph/)

ใช้ Google Maps Distance Matrix API เพื่อดึง travel time และ distance จริงบนถนน
แทนการคำนวณ haversine (เส้นตรง) ที่ไม่สะท้อนสภาพจราจรจริง

### API หลัก: `ExecuteMatrix`

```go
import "github.com/ROP-TEAM/rop-algorithm/graph"

m, err := graph.NewGoogleMapsMatrix(apiKey)
if err != nil { ... }

req := graph.DistanceMatrixRequest{
    Origins:      []string{"place_id:ChIJTydCFXdnHTERB3oVT1UZDRI"},
    Destinations: []string{"13.746900,100.534600", "heading=90:13.730800,100.541800"},
    Language:     "th",
    Region:       "th",
    Mode:         "driving",
}

result, err := m.ExecuteMatrix(ctx, req)
if err != nil { ... }

durations := result.Durations
distances := result.Distances
raw := result.Response
sent := result.Request
```

- `durations[i][j]` = เวลาเดินทางจาก i → j (**นาที**)
- `distances[i][j]` = ระยะทางจาก i → j (**เมตร**)
- `raw` = response เต็มจาก Google Distance Matrix API
- `sent` = request ที่ใช้ยิงจริงหลังประกอบค่าเรียบร้อย

### Compatibility Wrapper

`BuildMatrix(ctx, []Location, MatrixOptions)` ยังใช้งานได้เหมือนเดิม
แต่ตอนนี้เป็น compatibility wrapper ที่แปลงเป็น `DistanceMatrixRequest`
แล้วเรียก `ExecuteMatrix(...)` ภายใน

เหมาะเมื่อ caller ยังทำงานแบบ square matrix จาก `[]Location`
และต้องการคืนแค่ `durations`, `distances`

```go
locs := []graph.Location{
    {Lat: 13.7563, Lng: 100.5018},
    {Lat: 13.7469, Lng: 100.5346},
    {Lat: 13.7308, Lng: 100.5418},
}

durations, distances, err := m.BuildMatrix(ctx, locs, graph.MatrixOptions{})
if err != nil { ... }
```

---

## Caching

`GoogleMapsMatrix` รองรับ cache hook แล้วในชั้น executor:

```go
cfg := graph.DefaultMatrixCacheConfig()
cfg.Enabled = true

m.EnableInMemoryCache(cfg)
```

ถ้าต้องการ backend อื่น เช่น Redis สามารถ implement interface นี้แล้ว inject เข้าไปได้:

```go
type MatrixCache interface {
    Get(ctx context.Context, key string) (*DistanceMatrixResult, bool, error)
    Set(ctx context.Context, key string, value *DistanceMatrixResult, ttl time.Duration) error
}
```

แนวทาง policy ปัจจุบัน:
- `static` สำหรับ request ที่ไม่มี traffic fields
- `traffic` สำหรับ request ที่มี `DepartureTime`, `DepartureTimeNow`, หรือ `TrafficModel`

ค่า default:
- static namespace: `matrix:static:v1`
- traffic namespace: `matrix:traffic:v1`
- static driving TTL: `24h`
- walking/bicycling TTL: `7d`
- transit TTL: `6h`
- traffic TTL:
  - `morning`: `1h`
  - `midday`: `4h`
  - `evening`: `1h`
  - `night`: `8h`

หมายเหตุ:
- default config เปิด cache เป็น `false`
- default config เปิด traffic cache เป็น `false`
- เมื่อ `TrafficEnabled=false` request traffic ยังยิง API ได้ปกติ แต่จะไม่ใช้ cache

### Cache Observability

`GoogleMapsMatrix` รองรับ hook สำหรับ logging/metrics แล้ว:

```go
metrics := graph.NewMatrixMetrics()

m.SetEventHook(func(ctx context.Context, event graph.MatrixEvent) {
    log.Printf("matrix event=%s policy=%s reason=%s key=%s err=%s",
        event.Name, event.Policy, event.Reason, event.CacheKey, event.Error)
})

m.SetMetricsCollector(metrics)
```

event ที่ปล่อยตอนนี้ครอบคลุม:
- `cache_hit`
- `cache_miss`
- `cache_store`
- `cache_bypass`
- `cache_lookup_error`
- `cache_store_error`
- `api_request`
- `api_request_error`
- `api_response_error`
- `api_decode_error`
- `api_status_error`
- `api_shape_error`

---

## Result Shape

`ExecuteMatrix` คืน `DistanceMatrixResult`:

```go
type DistanceMatrixResult struct {
    Request   DistanceMatrixRequest
    Response  DistanceMatrixResponse
    Durations [][]int
    Distances [][]int
}
```

เหมาะกับงานที่ต้องการทั้ง:
- matrix ที่พร้อมใช้ใน algorithm
- raw payload จาก Google เพื่อ debug หรือเก็บ log
- request metadata ที่ส่งจริง

---

## DistanceMatrixRequest — Parameters ที่รองรับ

| Field | Type | Default | หมายเหตุ |
|---|---|---|---|
| `Origins` | `[]string` | required | address, lat/lng, `place_id:...`, plus code, encoded polyline |
| `Destinations` | `[]string` | required | รองรับรูปแบบเดียวกับ `Origins` |
| `Mode` | `string` | `"driving"` | `"driving"`, `"walking"`, `"bicycling"`, `"transit"` |
| `Units` | `string` | `"metric"` | `"metric"`, `"imperial"` |
| `Language` | `string` | `""` | BCP-47 เช่น `"th"` |
| `Region` | `string` | `""` | ccTLD เช่น `"th"` |
| `Avoid` | `[]string` | `nil` | `"tolls"`, `"highways"`, `"ferries"`, `"indoor"` |
| `DepartureTime` | `int64` | `0` | Unix timestamp |
| `DepartureTimeNow` | `bool` | `false` | ส่ง `departure_time=now` |
| `ArrivalTime` | `int64` | `0` | ใช้กับ transit; ห้ามใช้พร้อม departure time |
| `TrafficModel` | `string` | `""` | `"best_guess"`, `"pessimistic"`, `"optimistic"` |
| `TransitMode` | `[]string` | `nil` | `"bus"`, `"subway"`, `"train"`, `"tram"`, `"rail"` |
| `TransitRoutingPreference` | `string` | `""` | `"less_walking"`, `"fewer_transfers"` |

### Raw Location Strings

`Origins` และ `Destinations` รองรับ location strings แบบ docs โดยตรง เช่น:

```go
[]string{
  "13.756300,100.501800",
  "place_id:ChIJTydCFXdnHTERB3oVT1UZDRI",
  "side_of_road:13.746900,100.534600",
  "heading=90:13.730800,100.541800",
}
```

### Validation Rules

- ต้องมี `Origins` อย่างน้อย 1 ค่า
- ต้องมี `Destinations` อย่างน้อย 1 ค่า
- ห้ามใช้ `DepartureTime` พร้อม `DepartureTimeNow`
- ห้ามใช้ `DepartureTime` หรือ `DepartureTimeNow` พร้อม `ArrivalTime`

### MatrixOptions

`MatrixOptions` ยังมีไว้สำหรับ compatibility wrapper `BuildMatrix(...)`
โดย map ไปเป็น `DistanceMatrixRequest` ภายใน

### TrafficModel

| Value | ความหมาย |
|---|---|
| `best_guess` | ผสม historical + live traffic |
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

`ExecuteMatrix` และ `BuildMatrix` จัดการ batching อัตโนมัติ
และรองรับทั้ง matrix แบบสี่เหลี่ยม (`origins != destinations`)
และ square matrix (`origins == destinations`)

---

## การรัน Tests

```powershell
cd rop-algorithm
$env:GOOGLE_MAPS_API_KEY="<your-key>"
go test ./... -v -timeout 60s
```

ต้องเปิด **Distance Matrix API** ใน Google Cloud Console และเปิด Billing

ชุดเทสปัจจุบันมีทั้ง:
- integration test ที่ยิง Google API จริงเมื่อมี `GOOGLE_MAPS_API_KEY`
- unit tests ที่ใช้ `httptest` เพื่อตรวจ query building, batching, validation, และ error handling

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
