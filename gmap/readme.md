# gmap — Google Maps Distance Matrix

`package gmap` — data layer สำหรับดึง travel time และ distance จริงบนถนนจาก Google Maps Distance Matrix API

ข้อมูลที่ได้ (nodes + edges) ถูกส่งต่อไปยัง `graph/` และ `core/` เพื่อใช้คิด routing algorithm

---

## ไฟล์ในแพ็กเกจ

| ไฟล์ | หน้าที่ |
|---|---|
| `matrix_service.go` | `GoogleMapsMatrix` struct, constructor, `ExecuteMatrix`, `BuildMatrix`, cache/event orchestration |
| `matrix_request.go` | `BuildDistanceMatrixQuery`, `ValidateDistanceMatrixRequest`, `buildDistanceMatrixRequest`, `locationRequestValue` |
| `matrix_http.go` | HTTP execution, response decode, API status/shape checks |
| `matrix_cache.go` | `ResolveCachePolicy`, `BuildMatrixCacheKey`, `MatrixCacheTTL`, `MatrixTrafficSlot`, key hashing |
| `matrix_cache_memory.go` | `MatrixCache` interface, `MemoryMatrixCache`, clone helpers |
| `matrix_observability.go` | `MatrixEventHook`, `MatrixMetricsCollector`, `MatrixMetrics` |
| `matrix_compat.go` | type aliases re-exporting จาก `model/` (backward compat) |

---

## Quick Start

```go
import "github.com/ROP-TEAM/rop-algorithm/gmap"

m, err := gmap.NewGoogleMapsMatrix(apiKey)
if err != nil { ... }

req := gmap.DistanceMatrixRequest{
    Origins:      []string{"place_id:ChIJTydCFXdnHTERB3oVT1UZDRI"},
    Destinations: []string{"13.746900,100.534600", "heading=90:13.730800,100.541800"},
    Language:     "th",
    Region:       "th",
    Mode:         "driving",
}

result, err := m.ExecuteMatrix(ctx, req)
if err != nil { ... }

durations := result.Durations  // [i][j] = นาที  → ส่งต่อ algo
distances := result.Distances  // [i][j] = เมตร   → ส่งต่อ algo
```

`BuildMatrix` — wrapper สำหรับ square matrix จาก `[]Location`:

```go
locs := []gmap.Location{
    {Lat: 13.7563, Lng: 100.5018},
    {Lat: 13.7469, Lng: 100.5346},
}
durations, distances, err := m.BuildMatrix(ctx, locs, gmap.MatrixOptions{})
```

---

## Caching

```go
// dev — TTL 30 วัน ประหยัด quota
m.EnableInMemoryCache(gmap.DevMatrixCacheConfig())

// prod — เปิด cache ตามต้องการ
cfg := gmap.DefaultMatrixCacheConfig()
cfg.Enabled = true
cfg.TrafficEnabled = true
m.EnableInMemoryCache(cfg)

// custom backend (Redis ฯลฯ)
m.SetCache(myRedisCache, cfg)
```

Cache policy แบ่งอัตโนมัติ:

| Policy | เงื่อนไข | Key format |
|---|---|---|
| `static` | ไม่มี traffic fields | `namespace:SHA256(…)` |
| `traffic` | มี `DepartureTime` / `DepartureTimeNow` / `TrafficModel` | `namespace:YYYY-MM-DD:slot:SHA256(…)` |

Time slots (traffic): `morning` 1h · `midday` 4h · `evening` 1h · `night` 8h

---

## Observability

```go
m.SetEventHook(func(ctx context.Context, event gmap.MatrixEvent) {
    log.Printf("event=%s policy=%s key=%s err=%s",
        event.Name, event.Policy, event.CacheKey, event.Error)
})

metrics := gmap.NewMatrixMetrics()
m.SetMetricsCollector(metrics)
snapshot := metrics.Snapshot() // map[string]int64
```

---

## Wire กับ rop-backend

`GoogleMapsMatrix` ต้องสร้างครั้งเดียวตอน startup — `MemoryMatrixCache` อยู่ภายใน instance

```go
// main.go
import "github.com/ROP-TEAM/rop-algorithm/gmap"

matrix, err := gmap.NewGoogleMapsMatrix(cfg.GOOGLE_MAPS_API_KEY)
if err != nil { log.Fatal(err) }

matrix.EnableInMemoryCache(gmap.DevMatrixCacheConfig()) // dev
// matrix.SetCache(redisCache, prodCfg)                // prod multi-instance

planningService := services.NewPlanningService(db, matrix)
```

```go
// internal/services/planning.go
type PlanningService struct {
    db     *gorm.DB
    matrix gmap.DistanceMatrix  // interface — testable, swappable
}

func (s *PlanningService) buildProblem(ctx context.Context, job Job) error {
    durations, distances, err := s.matrix.BuildMatrix(ctx, locs, gmap.MatrixOptions{
        DepartureTime: job.PlannedDepartureUnix,
    })
    // ส่ง durations/distances → graph algorithm
}
```

Redis (multi-instance):

```go
// internal/infra/matrix_redis_cache.go
type RedisMatrixCache struct{ client *redis.Client }

func (c *RedisMatrixCache) Get(ctx context.Context, key string) (*gmap.DistanceMatrixResult, bool, error) { ... }
func (c *RedisMatrixCache) Set(ctx context.Context, key string, value *gmap.DistanceMatrixResult, ttl time.Duration) error { ... }
```

---

## Environment Variables

```env
GOOGLE_MAPS_API_KEY=   # ใน rop-backend/.env
```

ต้องเปิด **Distance Matrix API** แยกใน Google Cloud Console — คนละตัวกับ `GOOGLE_CLIENT_ID` (OAuth)
