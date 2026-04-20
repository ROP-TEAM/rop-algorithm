# rop-algorithm

Pure Go module สำหรับ Vehicle Routing Problem (VRP) optimization

---

## โครงสร้าง Module

```
rop-algorithm/
├── model/
│   ├── matrix.go           — Location, MatrixOptions, DistanceMatrix{Request,Result,Response,Row,Element}, ValueText, TransitFare
│   ├── matrix_cache.go     — CachePolicy, MatrixCacheConfig, MatrixCacheKeyParts
│   ├── matrix_event.go     — MatrixEvent
│   └── car.go              — placeholder (Phase 2)
├── graph/
│   ├── matrix_service.go   — GoogleMapsMatrix struct + options, ExecuteMatrix, BuildMatrix, cache/emit orchestration
│   ├── matrix_request.go   — BuildDistanceMatrixQuery, ValidateDistanceMatrixRequest, buildDistanceMatrixRequest, locationRequestValue
│   ├── matrix_http.go      — doDistanceMatrixRequest, HTTP execution, response decode, API status/shape checks
│   ├── matrix_cache.go     — ResolveCachePolicy, BuildMatrixCacheKey, MatrixCacheTTL, MatrixTrafficSlot, key hashing
│   ├── matrix_cache_memory.go — MatrixCache interface, MemoryMatrixCache, clone helpers
│   ├── matrix_observability.go — MatrixEventHook, MatrixMetricsCollector, MatrixMetrics
│   ├── matrix_compat.go    — type aliases re-exporting model types (backward compat)
│   └── pathFinder.go       — placeholder (Phase 2)
├── test/
│   ├── matrix_test.go      — integration + HTTP + query-building tests
│   └── matrix_cache_test.go — cache key/TTL/policy unit tests
├── core/
│   ├── constraint/         — feasibility checker (Phase 2)
│   ├── priority/           — node sorting (Phase 2)
│   └── timeWindow/         — time window validation (Phase 2)
└── solver/                 — ALNS main loop (Phase 2)
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

### เปิดใช้งาน

```go
// in-memory (single process)
cfg := graph.DefaultMatrixCacheConfig()
cfg.Enabled = true
m.EnableInMemoryCache(cfg)

// หรือ inject backend อื่น (Redis ฯลฯ)
m.SetCache(myRedisCache, cfg)
```

interface ที่ต้อง implement:

```go
type MatrixCache interface {
    Get(ctx context.Context, key string) (*DistanceMatrixResult, bool, error)
    Set(ctx context.Context, key string, value *DistanceMatrixResult, ttl time.Duration) error
}
```

### Cache Policy

`ResolveCachePolicy` แบ่ง request เป็น 2 policy อัตโนมัติ:

| Policy | เงื่อนไข | Cache Key | TTL |
|---|---|---|---|
| `static` | ไม่มี `DepartureTime`, `DepartureTimeNow`, `TrafficModel` | `namespace:SHA256(locations+mode+units+avoid+…)` | ตาม mode (ดูด้านล่าง) |
| `traffic` | มี field ใดก็ได้ข้างต้น | `namespace:YYYY-MM-DD:slot:SHA256(…)` | ตาม time slot |

### Cache Key (traffic policy)

traffic key ผูกกับ **วันที่ + time slot** เสมอ ทำให้วันหยุดกับวันธรรมดาไม่ปะปนกัน และ replan หลายรอบในวันเดียวได้ทันที:

```
matrix:traffic:v1 : 2026-04-21 : morning : <hash>
```

`departure_time` ของ request ถูกใช้เป็น reference time ก่อน — ถ้าไม่มีถึงจะใช้ `time.Now()`

### Time Slots (traffic policy)

| Slot | ช่วงเวลา | TTL default |
|---|---|---|
| `morning` | 06:00–09:00 | 1h |
| `midday` | 09:00–16:00 | 4h |
| `evening` | 16:00–20:00 | 1h |
| `night` | 20:00–06:00 | 8h |

### TTL (static policy)

| Mode | TTL default |
|---|---|
| `driving` | 24h |
| `walking` | 7d |
| `bicycling` | 7d |
| `transit` | 6h |

### Dev Config

```go
// เปิด cache ทุก field, TTL 30 วัน — ลด API call ระหว่าง develop
cfg := graph.DevMatrixCacheConfig()
m.EnableInMemoryCache(cfg)
```

### หมายเหตุ

- `DefaultMatrixCacheConfig()` — `Enabled=false`, `TrafficEnabled=false` (ต้องเปิดเองเสมอ)
- เมื่อ `TrafficEnabled=false` request traffic ยิง API ได้ปกติ แต่ไม่ถูก cache
- avoid array order ไม่กระทบ cache key (normalize + sort ก่อน hash)

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

## Environment Variables

```env
GOOGLE_MAPS_API_KEY=   # ใน rop-backend/.env
```

API Key คนละตัวกับ `GOOGLE_CLIENT_ID` (OAuth) — ต้องเปิด Distance Matrix API แยก
