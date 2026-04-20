# Google Maps Distance Matrix API

ใช้ Google Maps Distance Matrix API เพื่อดึง travel time และ distance จริงบนถนน
แทนการคำนวณ haversine (เส้นตรง) ที่ไม่สะท้อนสภาพจราจรจริง

---

## Quick Start

### `ExecuteMatrix` — API หลัก

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

durations := result.Durations  // [i][j] = นาที
distances := result.Distances  // [i][j] = เมตร
raw := result.Response         // full Google API response
sent := result.Request         // request ที่ส่งจริง
```

### `BuildMatrix` — Compatibility Wrapper

ใช้เมื่อทำงานแบบ square matrix จาก `[]Location` และต้องการแค่ durations + distances:

```go
locs := []graph.Location{
    {Lat: 13.7563, Lng: 100.5018},
    {Lat: 13.7469, Lng: 100.5346},
    {Lat: 13.7308, Lng: 100.5418},
}

durations, distances, err := m.BuildMatrix(ctx, locs, graph.MatrixOptions{})
```

`BuildMatrix` แปลง `[]Location` → `DistanceMatrixRequest` แล้วเรียก `ExecuteMatrix` ภายใน

---

## Result Shape

```go
type DistanceMatrixResult struct {
    Request   DistanceMatrixRequest   // request ที่ส่งจริง
    Response  DistanceMatrixResponse  // raw Google API response
    Durations [][]int                 // [i][j] นาที
    Distances [][]int                 // [i][j] เมตร
}
```

---

## DistanceMatrixRequest — Parameters

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

`Origins` และ `Destinations` รองรับ location strings แบบ Google Docs โดยตรง:

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

### TrafficModel

| Value | ความหมาย |
|---|---|
| `best_guess` | ผสม historical + live traffic |
| `pessimistic` | เวลามากสุด (worst case) |
| `optimistic` | เวลาน้อยสุด (best case) |

---

## API Limits & Batching

| เงื่อนไข | Limit |
|---|---|
| ไม่มี `DepartureTime` | 25 × 25 = 625 elements/request |
| มี `DepartureTime` | **10 × 10 = 100 elements/request** |
| Rate limit | 60,000 elements/นาที |
| ราคา | $5 / 1,000 elements (10,000 ฟรี/เดือน) |

`ExecuteMatrix` และ `BuildMatrix` จัดการ batching อัตโนมัติ รองรับทั้ง rectangular matrix (`origins != destinations`) และ square matrix

---

## Caching

### เปิดใช้งาน

```go
// in-memory (single process)
cfg := graph.DefaultMatrixCacheConfig()
cfg.Enabled = true
m.EnableInMemoryCache(cfg)

// inject backend อื่น (Redis ฯลฯ)
m.SetCache(myRedisCache, cfg)
```

interface ที่ต้อง implement สำหรับ custom backend:

```go
type MatrixCache interface {
    Get(ctx context.Context, key string) (*DistanceMatrixResult, bool, error)
    Set(ctx context.Context, key string, value *DistanceMatrixResult, ttl time.Duration) error
}
```

### Cache Policy

`ResolveCachePolicy` แบ่ง request เป็น 2 policy อัตโนมัติ:

| Policy | เงื่อนไข | Cache Key Format |
|---|---|---|
| `static` | ไม่มี `DepartureTime`, `DepartureTimeNow`, `TrafficModel` | `namespace:SHA256(locations+mode+units+avoid+…)` |
| `traffic` | มี field ใดก็ได้ข้างต้น | `namespace:YYYY-MM-DD:slot:SHA256(…)` |

traffic key ผูกกับ **วันที่ + time slot** เสมอ — วันหยุดกับวันธรรมดาไม่ปะปนกัน และ replan หลายรอบในวันเดียวได้ทันที  
`departure_time` ของ request ถูกใช้เป็น reference time ก่อน ถ้าไม่มีจึงใช้ `time.Now()`

### Time Slots & TTL (traffic policy)

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
// TTL 30 วันทุก policy — ลด API call ระหว่าง develop
cfg := graph.DevMatrixCacheConfig()
m.EnableInMemoryCache(cfg)
```

### หมายเหตุ

- `DefaultMatrixCacheConfig()` — `Enabled=false`, `TrafficEnabled=false` (ต้องเปิดเองเสมอ)
- เมื่อ `TrafficEnabled=false` request traffic ยิง API ได้ปกติ แต่ไม่ถูก cache
- avoid array order ไม่กระทบ cache key (normalize + sort ก่อน hash)
- `MemoryMatrixCache` ไม่ share ข้าม process — ถ้า deploy หลาย instance ต้องใช้ Redis

---

## Cache Observability

```go
metrics := graph.NewMatrixMetrics()

m.SetEventHook(func(ctx context.Context, event graph.MatrixEvent) {
    log.Printf("matrix event=%s policy=%s reason=%s key=%s err=%s",
        event.Name, event.Policy, event.Reason, event.CacheKey, event.Error)
})

m.SetMetricsCollector(metrics)

// อ่าน snapshot
snapshot := metrics.Snapshot() // map[string]int64
```

Events ที่ปล่อยออกมา:

| กลุ่ม | Events |
|---|---|
| Cache | `cache_hit`, `cache_miss`, `cache_store`, `cache_bypass`, `cache_lookup_error`, `cache_store_error` |
| API | `api_request`, `api_request_error`, `api_response_error`, `api_decode_error`, `api_status_error`, `api_shape_error` |

---

## Wire กับ rop-backend

### หลักการ

`GoogleMapsMatrix` ต้องสร้างครั้งเดียวตอน startup แล้วส่งต่อผ่าน dependency injection เพราะ `MemoryMatrixCache` อยู่ภายใน instance — ถ้าสร้างใหม่ทุก request cache จะว่างเสมอ

```
main.go
  → NewGoogleMapsMatrix(cfg.GOOGLE_MAPS_API_KEY)
  → EnableInMemoryCache / SetCache
  → SetEventHook / SetMetricsCollector   (optional)
  → NewPlanningService(db, matrix)
      → ใช้ตลอด lifetime ของ process
```

### `main.go` — init และ inject

```go
import "github.com/ROP-TEAM/rop-algorithm/graph"

matrix, err := graph.NewGoogleMapsMatrix(cfg.GOOGLE_MAPS_API_KEY)
if err != nil {
    log.Fatal(err)
}

// dev: TTL 30 วัน ประหยัด quota
matrix.EnableInMemoryCache(graph.DevMatrixCacheConfig())

// prod: static cache เปิด, traffic ตามต้องการ
// prodCfg := graph.DefaultMatrixCacheConfig()
// prodCfg.Enabled = true
// prodCfg.TrafficEnabled = true
// matrix.EnableInMemoryCache(prodCfg)

// optional: logging + metrics
matrix.SetEventHook(func(ctx context.Context, event graph.MatrixEvent) {
    slog.InfoContext(ctx, "matrix", "event", event.Name, "policy", event.Policy,
        "key", event.CacheKey, "err", event.Error)
})

planningService := services.NewPlanningService(db, matrix)
```

### `internal/services/planning.go` — รับ interface

```go
import "github.com/ROP-TEAM/rop-algorithm/graph"

type PlanningService struct {
    db     *gorm.DB
    matrix graph.DistanceMatrix  // interface — ทดสอบง่าย, swap Redis ได้
}

func NewPlanningService(db *gorm.DB, matrix graph.DistanceMatrix) *PlanningService {
    return &PlanningService{db: db, matrix: matrix}
}

func (s *PlanningService) buildProblem(ctx context.Context, job Job) (vrp.Problem, error) {
    durations, distances, err := s.matrix.BuildMatrix(ctx, locs, graph.MatrixOptions{
        DepartureTime: job.PlannedDepartureUnix, // หรือ DepartureTimeNow: true
    })
    if err != nil {
        return vrp.Problem{}, err
    }
    // แปลง durations/distances → vrp.Problem
}
```

### Request flow

```
HTTP → PlanningHandler
  → PlanningService.Plan(jobID)
      → buildProblem(ctx, job)
          → matrix.BuildMatrix(...)
              → [cache hit]  คืน durations/distances ทันที
              → [cache miss] ยิง Google API → store cache → คืน result
      → vrp.Solver.Solve(problem)
      → saveSolution(jobID, solution)
```

### Config ตาม environment

| Environment | Config | Enabled | TrafficEnabled | หมายเหตุ |
|---|---|---|---|---|
| local / dev | `DevMatrixCacheConfig()` | ✅ | ❌ | TTL 30 วัน ประหยัด quota |
| staging | `DefaultMatrixCacheConfig()` + `Enabled=true` | ✅ | ❌ | TTL จริง traffic ยังปิด |
| prod (single) | `DefaultMatrixCacheConfig()` + เปิดทั้งคู่ | ✅ | ✅ | MemoryMatrixCache |
| prod (multi) | Redis impl + เปิดทั้งคู่ | ✅ | ✅ | implement `MatrixCache` interface |

### Redis Cache (multi-instance)

```go
// rop-backend/internal/infra/matrix_redis_cache.go
type RedisMatrixCache struct{ client *redis.Client }

func (c *RedisMatrixCache) Get(ctx context.Context, key string) (*graph.DistanceMatrixResult, bool, error) {
    // json.Unmarshal จาก Redis
}

func (c *RedisMatrixCache) Set(ctx context.Context, key string, value *graph.DistanceMatrixResult, ttl time.Duration) error {
    // json.Marshal → Redis SET EX
}

// main.go
matrix.SetCache(&RedisMatrixCache{client: redisClient}, prodCfg)
```

---

## การรัน Tests

```powershell
cd rop-algorithm
$env:GOOGLE_MAPS_API_KEY="<your-key>"
go test ./... -v -timeout 60s
```

ต้องเปิด **Distance Matrix API** ใน Google Cloud Console และเปิด Billing

ชุดเทสปัจจุบัน:
- integration tests ยิง Google API จริงเมื่อมี `GOOGLE_MAPS_API_KEY` (skip ถ้าไม่มี)
- unit tests ใช้ `httptest` ตรวจ query building, batching, validation, error handling
- unit tests ตรวจ cache key/TTL/policy โดยไม่ต้องการ API key

---

## Environment Variables

```env
GOOGLE_MAPS_API_KEY=   # ใน rop-backend/.env
```

API Key คนละตัวกับ `GOOGLE_CLIENT_ID` (OAuth) — ต้องเปิด Distance Matrix API แยกใน Google Cloud Console
