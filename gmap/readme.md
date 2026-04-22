# gmap — Google Maps Distance Matrix

`package gmap` — data layer สำหรับดึง travel time และ distance จริงบนถนนจาก Google Maps Distance Matrix API

ค่าที่ได้กลับมา: **n×n matrix** ทั้งสองชุด
- `durations[i][j]` = เวลาเดินทาง i→j **(นาที)**
- `distances[i][j]` = ระยะทาง i→j **(เมตร)**

ข้อมูลเหล่านี้ถูกส่งต่อไปยัง `graph/` และ `core/` เพื่อคิด routing algorithm

---

## Prerequisites

1. สร้าง API key ใน [Google Cloud Console](https://console.cloud.google.com/)
2. เปิด **Distance Matrix API** ใน Console (คนละตัวกับ Maps JavaScript API หรือ OAuth)
3. ใส่ key ใน `rop-backend/.env`:
   ```
   GOOGLE_MAPS_API_KEY=AIza...
   ```

---

## Quick Start

```go
import "github.com/ROP-TEAM/rop-algorithm/gmap"

// สร้าง client ครั้งเดียวตอน startup — cache อยู่ภายใน instance
m, err := gmap.NewGoogleMapsMatrix(
    apiKey,
    gmap.WithInMemoryCache(gmap.DevMatrixCacheConfig()),
)
if err != nil {
    return err // apiKey ว่างจะ error ทันที
}

locs := []gmap.Location{
    gmap.NewLatLngLocation(13.7563, 100.5018), // depot
    gmap.NewLatLngLocation(13.7469, 100.5346),
    gmap.NewLatLngLocation(13.7308, 100.5418),
}

durations, distances, err := m.BuildMatrix(ctx, locs, gmap.MatrixOptions{})
if err != nil {
    return err
}
// durations[i][j] = เวลาเดินทาง i→j (นาที)
// distances[i][j] = ระยะทาง i→j (เมตร)
```

---

## รูปแบบ Location

```go
// lat/lng — รูปแบบปกติ
gmap.NewLatLngLocation(13.7563, 100.5018)

// raw string — ส่งตรงไปยัง API (ข้าม lat/lng)
gmap.NewRawLocation("place_id:ChIJTydCFXdnHTERB3oVT1UZDRI")
gmap.NewRawLocation("Central World, Bangkok")     // geocode ที่ฝั่ง API
gmap.NewRawLocation("7P3Q+QJ Bangkok")            // plus code
gmap.NewRawLocation("side_of_road:13.7563,100.5018")
gmap.NewRawLocation("heading=90:13.7563,100.5018")
gmap.NewRawLocation("enc:polyline_value:")
```

Origins/Destinations ใน `DistanceMatrixRequest` รับ string ได้ทุกรูปแบบข้างต้น และผสมกันได้ในคำขอเดียว

---

## Options

### Mode (วิธีเดินทาง)

```go
gmap.MatrixOptions{Mode: gmap.ModeDriving}   // default ถ้าไม่ระบุ
gmap.MatrixOptions{Mode: gmap.ModeWalking}   // ไม่มี traffic
gmap.MatrixOptions{Mode: gmap.ModeBicycling}
gmap.MatrixOptions{Mode: gmap.ModeTransit}   // ต้องตั้งเวลาเดินทางด้วย
```

### Traffic (สภาพจราจรจริง)

`DurationInTraffic` ถูกใช้แทน `Duration` เมื่อ **mode = driving** และ **ตั้ง departure_time** เท่านั้น

```go
// ใช้สภาพจราจร ณ เวลาออกเดินทางจริง
var opts gmap.MatrixOptions
opts.SetDepartureTime(time.Now().Add(30 * time.Minute))
opts.TrafficModel = gmap.TrafficModelPessimistic // time-critical delivery

// ใช้สภาพจราจรขณะนี้เลย
gmap.MatrixOptions{DepartureTimeNow: true}
```

TrafficModel ที่รองรับ: `TrafficModelBestGuess` (default) · `TrafficModelPessimistic` · `TrafficModelOptimistic`

> **ข้อควรระวัง:** `DepartureTime` ในอดีตทำให้ API คืน `INVALID_REQUEST`

### Avoid (หลีกเลี่ยงเส้นทาง)

ใช้กับ `driving` และ `bicycling` เท่านั้น:

```go
gmap.MatrixOptions{
    Mode:  gmap.ModeDriving,
    Avoid: []string{gmap.AvoidTolls, gmap.AvoidHighways},
}
gmap.MatrixOptions{Mode: gmap.ModeWalking, Avoid: []string{gmap.AvoidIndoor}}
```

Avoid constants: `AvoidTolls` · `AvoidHighways` · `AvoidFerries` · `AvoidIndoor`

### Transit (ขนส่งสาธารณะ)

```go
// ออกเดินทางเวลาที่กำหนด
var opts gmap.MatrixOptions
opts.SetDepartureTime(time.Date(2026, 4, 23, 8, 0, 0, 0, bangkokLoc))
opts.Mode = gmap.ModeTransit

// ต้องถึงปลายทางภายในเวลา (ใช้แทน SetDepartureTime — ไม่ใช้พร้อมกัน)
var opts gmap.MatrixOptions
opts.SetArrivalTime(time.Date(2026, 4, 23, 9, 0, 0, 0, bangkokLoc))
opts.Mode = gmap.ModeTransit

// เฉพาะรถไฟฟ้า + รถไฟ
gmap.MatrixOptions{
    Mode:        gmap.ModeTransit,
    TransitMode: []string{"subway", "train"},
}

// ลด walking / เปลี่ยนขบวนน้อย
gmap.MatrixOptions{Mode: gmap.ModeTransit, TransitRoutingPreference: "less_walking"}
gmap.MatrixOptions{Mode: gmap.ModeTransit, TransitRoutingPreference: "fewer_transfers"}
```

TransitMode ที่รองรับ: `"bus"` · `"subway"` · `"train"` · `"tram"` · `"rail"`

---

## การจัดการ Error

### Top-level (response ทั้งคำขอ)

package คืน `error` ทันทีสำหรับทุก status ที่ไม่ใช่ `OK`:

```
INVALID_REQUEST        — พารามิเตอร์ไม่ถูกต้อง (เช่น DepartureTime ในอดีต)
MAX_DIMENSIONS_EXCEEDED — origins หรือ destinations เกิน 25
OVER_DAILY_LIMIT       — เกิน daily quota หรือ API key มีปัญหา
OVER_QUERY_LIMIT       — เกิน QPS — ต้อง retry with back-off
REQUEST_DENIED         — API key ไม่มีสิทธิ์ หรือยังไม่เปิด Distance Matrix API
UNKNOWN_ERROR          — server error ฝั่ง Google — retry ได้
```

### Element-level (origin→destination คู่เดียว)

```
NOT_FOUND              — geocode origin/destination ไม่เจอ
ZERO_RESULTS           — ไม่มีเส้นทางระหว่างสองจุด (เช่น เกาะที่ไม่มีถนนเชื่อม)
MAX_ROUTE_LENGTH_EXCEEDED — เส้นทางยาวเกิน ~6,500 km
```

format ของ error message: `"element [i][j] (origin_string -> dest_string) status: NOT_FOUND"`

### Validation (ก่อนส่ง API)

```go
// error: departure_time และ departure_time=now ใช้พร้อมกันไม่ได้
gmap.DistanceMatrixRequest{DepartureTime: 1234567890, DepartureTimeNow: true}

// error: departure_time และ arrival_time ใช้พร้อมกันไม่ได้
gmap.DistanceMatrixRequest{DepartureTime: 1234567890, ArrivalTime: 1234599999}
```

---

## Element Limit และ Auto-Chunking

Google Maps Distance Matrix API รองรับสูงสุด **100 elements** (origins × destinations) ต่อ 1 request

package นี้ auto-chunk เป็น **10×10 block** แล้ว merge ผลลัพธ์อัตโนมัติ:

```go
// 30 locations → 30×30 = 900 elements → แบ่งเป็น 9 chunk อัตโนมัติ
locs := make([]gmap.Location, 30)
durations, distances, err := m.BuildMatrix(ctx, locs, gmap.MatrixOptions{})
```

---

## Caching

```go
// dev — TTL 30 วัน, ประหยัด quota ระหว่าง develop
m, _ := gmap.NewGoogleMapsMatrix(apiKey, gmap.WithInMemoryCache(gmap.DevMatrixCacheConfig()))

// prod — เปิด cache เองพร้อมกำหนดค่า
cfg := gmap.DefaultMatrixCacheConfig()
cfg.Enabled = true
cfg.TrafficEnabled = true // traffic cache แยก flag เพราะ TTL สั้นกว่ามาก
m, _ := gmap.NewGoogleMapsMatrix(apiKey, gmap.WithInMemoryCache(cfg))

// ตั้ง cache หลัง construction (ยังรองรับ)
m.EnableInMemoryCache(gmap.DevMatrixCacheConfig())

// custom backend — implement MatrixCache interface (2 methods)
m.SetCache(myRedisCache, cfg)
```

Cache policy แบ่งอัตโนมัติ — `static` เมื่อไม่มี traffic fields, `traffic` เมื่อมี:

```go
// policy: static → key = "matrix:static:v1:SHA256(...)"
gmap.MatrixOptions{}
gmap.MatrixOptions{Mode: gmap.ModeWalking}

// policy: traffic → key = "matrix:traffic:v1:2026-04-23:morning:SHA256(...)"
gmap.MatrixOptions{DepartureTimeNow: true}
gmap.MatrixOptions{TrafficModel: gmap.TrafficModelPessimistic}
```

Default TTL (`DefaultMatrixCacheConfig`):

```
static
  driving   → 24h
  walking   → 7d
  bicycling → 7d
  transit   → 6h

traffic (แบ่งตาม time slot ของ departure_time)
  morning (06:00–08:59) → 1h
  midday  (09:00–15:59) → 4h
  evening (16:00–19:59) → 1h
  night   (20:00–05:59) → 8h
```

---

## Observability

```go
// event hook — log ทุก cache hit/miss และ API call
m.SetEventHook(func(ctx context.Context, event gmap.MatrixEvent) {
    log.Printf("event=%-25s policy=%-8s key=%s origins=%d dests=%d err=%v",
        event.Name, event.Policy, event.CacheKey,
        event.ChunkOrigins, event.ChunkDestinations, event.Error)
})

// metrics counter — snapshot เป็น map[string]int64
metrics := gmap.NewMatrixMetrics()
m.SetMetricsCollector(metrics)
snapshot := metrics.Snapshot()
// snapshot["api_request"]    → จำนวน API call จริง
// snapshot["cache_hit"]      → จำนวน cache hit
// snapshot["cache_miss"]     → จำนวน cache miss
```

Event names ทั้งหมด:
`api_request` · `api_request_error` · `api_response_error` · `api_decode_error` · `api_status_error` · `api_shape_error` · `cache_hit` · `cache_miss` · `cache_bypass` · `cache_lookup_error` · `cache_store` · `cache_store_error`

---

## ExecuteMatrix (advanced)

`BuildMatrix` ใช้สำหรับ square n×n matrix `ExecuteMatrix` ใช้เมื่อต้องการ origins/destinations แยกกัน หรือต้องการ raw response:

```go
result, err := m.ExecuteMatrix(ctx, gmap.DistanceMatrixRequest{
    Origins:      []string{"13.756300,100.501800", gmap.NewRawLocation("place_id:...").Raw},
    Destinations: []string{"13.746900,100.534600", "13.730800,100.541800"},
    Language:     "th",
    Region:       "th",
})
// result.Durations, result.Distances — เหมือน BuildMatrix
// result.Response — raw API response (origin addresses, fare, ฯลฯ)
```

---

## Wire กับ rop-backend

`GoogleMapsMatrix` ต้องสร้างครั้งเดียวตอน startup — `MemoryMatrixCache` อยู่ภายใน instance

```go
// main.go
matrix, err := gmap.NewGoogleMapsMatrix(
    cfg.GOOGLE_MAPS_API_KEY,
    gmap.WithInMemoryCache(gmap.DevMatrixCacheConfig()), // dev
    // gmap.WithInMemoryCache(prodCfg),                 // prod single-instance
)
if err != nil {
    log.Fatal(err)
}
// matrix.SetCache(redisCache, prodCfg)  // prod multi-instance

planningService := services.NewPlanningService(db, matrix)
```

```go
// internal/services/planning.go
type PlanningService struct {
    db     *gorm.DB
    matrix gmap.DistanceMatrix // interface — testable, swappable
}

func (s *PlanningService) buildProblem(ctx context.Context, job Job) error {
    var opts gmap.MatrixOptions
    opts.SetDepartureTime(job.PlannedDepartureTime)
    opts.TrafficModel = gmap.TrafficModelPessimistic // time-critical delivery

    durations, distances, err := s.matrix.BuildMatrix(ctx, locs, opts)
    // ส่ง durations/distances → graph algorithm
}
```

Redis (multi-instance prod):

```go
// internal/infra/matrix_redis_cache.go
type RedisMatrixCache struct{ client *redis.Client }

func (c *RedisMatrixCache) Get(ctx context.Context, key string) (*gmap.DistanceMatrixResult, bool, error) {
    data, err := c.client.Get(ctx, key).Bytes()
    if errors.Is(err, redis.Nil) {
        return nil, false, nil
    }
    if err != nil {
        return nil, false, err
    }
    var result gmap.DistanceMatrixResult
    if err := json.Unmarshal(data, &result); err != nil {
        return nil, false, err
    }
    return &result, true, nil
}

func (c *RedisMatrixCache) Set(ctx context.Context, key string, value *gmap.DistanceMatrixResult, ttl time.Duration) error {
    data, err := json.Marshal(value)
    if err != nil {
        return err
    }
    return c.client.Set(ctx, key, data, ttl).Err()
}
```

---

## ไฟล์ในแพ็กเกจ

| ไฟล์ | หน้าที่ |
|---|---|
| `matrix_service.go` | `GoogleMapsMatrix` struct, setters, `ExecuteMatrix`, `BuildMatrix`, cache orchestration |
| `matrix_options.go` | `NewGoogleMapsMatrix` constructor, `With*` option functions, `WithInMemoryCache` |
| `matrix_chunks.go` | `allocateResult`, `buildChunk`, `mergeChunkIntoResult` — internal chunking helpers |
| `matrix_request.go` | `BuildDistanceMatrixQuery`, `ValidateDistanceMatrixRequest`, location formatting |
| `matrix_http.go` | `executeHTTPRequest`, `decodeAndValidateAPIResponse`, HTTP execution |
| `matrix_cache.go` | `ResolveCachePolicy`, `BuildMatrixCacheKey`, `MatrixCacheTTL`, `MatrixTrafficSlot` |
| `matrix_cache_memory.go` | `MatrixCache` interface, `MemoryMatrixCache`, clone helpers |
| `matrix_observability.go` | `MatrixEventHook`, `MatrixMetricsCollector`, `MatrixMetrics` |
| `matrix_compat.go` | type aliases + constructor re-exports จาก `model/` (backward compat) |
