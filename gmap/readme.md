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
if err != nil {
    log.Fatal(err) // apiKey ว่างจะ error ทันที
}

// square matrix จาก []Location — ใช้กับ routing algorithm โดยตรง
locs := []gmap.Location{
    {Lat: 13.7563, Lng: 100.5018}, // depot
    {Lat: 13.7469, Lng: 100.5346},
    {Lat: 13.7308, Lng: 100.5418},
}
durations, distances, err := m.BuildMatrix(ctx, locs, gmap.MatrixOptions{})
// durations[i][j] = นาที, distances[i][j] = เมตร

// หรือถ้าต้องการ custom origins/destinations แยกกัน
result, err := m.ExecuteMatrix(ctx, gmap.DistanceMatrixRequest{
    Origins:      []string{"13.756300,100.501800"},
    Destinations: []string{"13.746900,100.534600", "13.730800,100.541800"},
    Language:     "th",
    Region:       "th",
})
```

---

## รูปแบบ Location

Origins/Destinations ใน `DistanceMatrixRequest` รับ string ได้หลายรูปแบบ — ผสมกันในคำขอเดียวได้:

```go
req := gmap.DistanceMatrixRequest{
    Origins: []string{
        "13.756300,100.501800",                       // lat,lng
        "place_id:ChIJTydCFXdnHTERB3oVT1UZDRI",       // Google place ID
        "Central World, Bangkok",                      // ที่อยู่ (geocode ที่ฝั่ง API)
        "7P3Q+QJ Bangkok",                             // plus code
    },
    Destinations: []string{
        "side_of_road:13.756300,100.501800",           // snap ไปฝั่งถนนที่ใกล้ที่สุด
        "heading=90:13.756300,100.501800",             // 0–360° — ระบุทิศออกจากจุด (ถนนทางเดียว)
        "enc:polyline_value:",                         // encoded polyline
    },
}
```

เมื่อใช้ `Location` struct กับ `BuildMatrix`:

```go
locs := []gmap.Location{
    {Lat: 13.7563, Lng: 100.5018},                        // → "13.756300,100.501800"
    {Raw: "place_id:ChIJTydCFXdnHTERB3oVT1UZDRI"},        // Raw ส่งตรง ข้าม Lat/Lng
    {Raw: "heading=90:13.756300,100.501800"},
}
```

---

## Mode (วิธีเดินทาง)

```go
// driving — default ถ้าไม่ระบุ
gmap.MatrixOptions{Mode: "driving"}

// walking — ไม่มี traffic, ไม่รองรับ avoid=tolls/highways
gmap.MatrixOptions{Mode: "walking"}

// bicycling — รองรับ avoid=tolls/highways/ferries เท่านั้น
gmap.MatrixOptions{Mode: "bicycling"}

// transit — ต้องตั้ง DepartureTime หรือ ArrivalTime ด้วย, ไม่รองรับ avoid ทั้งหมด
gmap.MatrixOptions{
    Mode:          "transit",
    DepartureTime: time.Now().Add(1 * time.Hour).Unix(),
}
```

---

## Avoid (หลีกเลี่ยงเส้นทาง)

ใช้กับ `driving` และ `bicycling` เท่านั้น:

```go
// หลีกเลี่ยงทางด่วน (tolls) + ทางหลวง (highways)
gmap.MatrixOptions{
    Mode:  "driving",
    Avoid: []string{"tolls", "highways"},
}

// หลีกเลี่ยงเรือข้ามฟาก
gmap.MatrixOptions{
    Mode:  "driving",
    Avoid: []string{"ferries"},
}

// หลีกเลี่ยงเส้นทางในอาคาร — walking เท่านั้น
gmap.MatrixOptions{
    Mode:  "walking",
    Avoid: []string{"indoor"},
}
```

---

## Traffic (สภาพจราจรจริง)

`DurationInTraffic` จะถูกส่งกลับเมื่อ **mode = driving** และ **ตั้ง departure_time** เท่านั้น  
package นี้ใช้ `DurationInTraffic` ก่อน `Duration` เสมอเมื่อมีค่า

```go
// ใช้สภาพจราจร ณ เวลาออกเดินทางจริง (ต้องเป็นปัจจุบันหรืออนาคต)
gmap.MatrixOptions{
    DepartureTime: time.Now().Add(30 * time.Minute).Unix(),
}

// ใช้สภาพจราจรขณะนี้เลย
gmap.MatrixOptions{
    DepartureTimeNow: true,
}

// best_guess — ค่า default ของ Google ถ้าไม่ระบุ TrafficModel
// ประมาณจาก historical + realtime data
gmap.MatrixOptions{
    DepartureTime: time.Now().Add(1 * time.Hour).Unix(),
    TrafficModel:  "best_guess",
}

// pessimistic — เหมาะกับ time-critical delivery (ประมาณสูงกว่าจริง)
gmap.MatrixOptions{
    DepartureTime: time.Now().Add(1 * time.Hour).Unix(),
    TrafficModel:  "pessimistic",
}

// optimistic — เหมาะกับ best-case planning
gmap.MatrixOptions{
    DepartureTime: time.Now().Add(1 * time.Hour).Unix(),
    TrafficModel:  "optimistic",
}
```

> **ข้อควรระวัง:** `DepartureTime` ในอดีตทำให้ API คืน `INVALID_REQUEST`

---

## Transit (ขนส่งสาธารณะ)

```go
// ออกเดินทางเวลาที่กำหนด — API เลือก transit ที่ดีที่สุด
gmap.MatrixOptions{
    Mode:          "transit",
    DepartureTime: time.Date(2026, 4, 23, 8, 0, 0, 0, bangkokLoc).Unix(),
}

// ต้องถึงปลายทางภายในเวลา — ใช้แทน DepartureTime ได้ (ไม่ใช้พร้อมกัน)
gmap.MatrixOptions{
    Mode:        "transit",
    ArrivalTime: time.Date(2026, 4, 23, 9, 0, 0, 0, bangkokLoc).Unix(),
}

// เฉพาะรถไฟฟ้า + รถไฟ (ไม่รวมรถเมล์)
gmap.MatrixOptions{
    Mode:        "transit",
    TransitMode: []string{"subway", "train"},
    DepartureTime: time.Now().Unix(),
}

// ลด walking ให้น้อยที่สุด
gmap.MatrixOptions{
    Mode:                     "transit",
    TransitRoutingPreference: "less_walking",
    DepartureTime:            time.Now().Unix(),
}

// เปลี่ยนขบวนน้อยที่สุด
gmap.MatrixOptions{
    Mode:                     "transit",
    TransitRoutingPreference: "fewer_transfers",
    DepartureTime:            time.Now().Unix(),
}
```

TransitMode ที่รองรับ: `"bus"`, `"subway"`, `"train"`, `"tram"`, `"rail"`

---

## ข้อจำกัดของ Google Maps API

### Element Limit และ Auto-Chunking

Google Maps Distance Matrix API รองรับสูงสุด **100 elements** (origins × destinations) ต่อ 1 request

package นี้ auto-chunk เป็น **10×10 block** แล้ว merge ผลลัพธ์อัตโนมัติ — ส่ง location กี่จุดก็ได้:

```go
// 30 locations → 30×30 = 900 elements → package แบ่งเป็น 9 chunk อัตโนมัติ
locs := make([]gmap.Location, 30)
durations, distances, err := m.BuildMatrix(ctx, locs, gmap.MatrixOptions{})
```

### Validation ที่ package บังคับก่อนส่ง API

```go
// error: origins ว่าง
gmap.DistanceMatrixRequest{Origins: []string{}, Destinations: []string{"..."}}

// error: departure_time และ departure_time=now ใช้พร้อมกันไม่ได้
gmap.DistanceMatrixRequest{DepartureTime: 1234567890, DepartureTimeNow: true}

// error: departure_time และ arrival_time ใช้พร้อมกันไม่ได้
gmap.DistanceMatrixRequest{DepartureTime: 1234567890, ArrivalTime: 1234599999}
```

### API-level Status (top-level response)

package คืน `error` ทันทีสำหรับทุก status ที่ไม่ใช่ `OK`:

```go
result, err := m.ExecuteMatrix(ctx, req)
if err != nil {
    // err.Error() จะมี status และ error_message จาก Google เช่น:
    // "distance matrix API status: REQUEST_DENIED"
    // "distance matrix API status: OVER_QUERY_LIMIT"
    // "distance matrix API status: INVALID_REQUEST (departure_time must not be in the past)"
}

// Status ที่เป็นไปได้:
// OK                    — สำเร็จ
// INVALID_REQUEST       — พารามิเตอร์ไม่ถูกต้อง (เช่น DepartureTime ในอดีต)
// MAX_ELEMENTS_EXCEEDED — origins × destinations เกิน limit (หลัง chunking แปลว่า bug)
// MAX_DIMENSIONS_EXCEEDED — origins หรือ destinations เกิน 25
// OVER_DAILY_LIMIT      — เกิน daily quota หรือ API key มีปัญหา
// OVER_QUERY_LIMIT      — เกิน QPS — ต้อง retry with back-off
// REQUEST_DENIED        — API key ไม่มีสิทธิ์ หรือ Distance Matrix API ยังไม่เปิดใน Console
// UNKNOWN_ERROR         — server error ฝั่ง Google — retry ได้
```

### Element-level Status

package คืน `error` ทันทีเมื่อพบ element ที่ไม่ใช่ `OK`:

```go
// err จะมีรูปแบบ: "element [i][j] status: NOT_FOUND"
result, err := m.ExecuteMatrix(ctx, req)
if err != nil {
    // Status ที่เป็นไปได้ใน element:
    // OK                       — มี distance และ duration
    // NOT_FOUND                — geocode origin/destination ไม่เจอ
    // ZERO_RESULTS             — ไม่มีเส้นทางระหว่างสองจุด (เช่น เกาะที่ไม่มีถนนเชื่อม)
    // MAX_ROUTE_LENGTH_EXCEEDED — เส้นทางยาวเกิน limit (~6500 km สำหรับ driving)
}
```

---

## Caching

```go
// dev — TTL 30 วัน, ประหยัด quota ระหว่าง develop
m.EnableInMemoryCache(gmap.DevMatrixCacheConfig())

// prod — default: cache ปิด, เปิดเองตามต้องการ
cfg := gmap.DefaultMatrixCacheConfig()
cfg.Enabled = true
cfg.TrafficEnabled = true // traffic cache แยก flag เพราะ TTL สั้นกว่ามาก
m.EnableInMemoryCache(cfg)

// custom backend — implement MatrixCache interface (2 methods)
m.SetCache(myRedisCache, cfg)
```

Cache policy แบ่งอัตโนมัติ — `static` เมื่อไม่มี traffic fields, `traffic` เมื่อมี:

```go
// policy: static → key = "matrix:static:v1:SHA256(origins+destinations+mode+...)"
gmap.MatrixOptions{}
gmap.MatrixOptions{Mode: "walking"}

// policy: traffic → key = "matrix:traffic:v1:2026-04-23:morning:SHA256(...)"
gmap.MatrixOptions{DepartureTimeNow: true}
gmap.MatrixOptions{DepartureTime: departureUnix}
gmap.MatrixOptions{TrafficModel: "pessimistic"} // TrafficModel อย่างเดียวก็นับเป็น traffic
```

Default TTL (`DefaultMatrixCacheConfig`):

```go
// static
driving   → 24 * time.Hour
walking   → 7 * 24 * time.Hour
bicycling → 7 * 24 * time.Hour
transit   → 6 * time.Hour

// traffic (แบ่งตาม time slot ของ departure_time)
morning (06:00–08:59) → 1 * time.Hour
midday  (09:00–15:59) → 4 * time.Hour
evening (16:00–19:59) → 1 * time.Hour
night   (20:00–05:59) → 8 * time.Hour
```

Cache key ถูก normalize ก่อน hash — sort `avoid`/`transit_mode`, trim whitespace, fill defaults — ทำให้ request ที่ logical เหมือนกันได้ cache เดียวกันเสมอ

---

## Observability

```go
// event hook — log ทุก cache hit/miss และ API call
m.SetEventHook(func(ctx context.Context, event gmap.MatrixEvent) {
    log.Printf("event=%-25s policy=%-8s key=%s origins=%d dests=%d err=%s",
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
// snapshot["api_status_error"] → จำนวน error จาก Google API
```

Event names ทั้งหมด:
`api_request` · `api_request_error` · `api_response_error` · `api_decode_error` · `api_status_error` · `api_shape_error` · `cache_hit` · `cache_miss` · `cache_bypass` · `cache_lookup_error` · `cache_store` · `cache_store_error`

---

## Wire กับ rop-backend

`GoogleMapsMatrix` ต้องสร้างครั้งเดียวตอน startup — `MemoryMatrixCache` อยู่ภายใน instance

```go
// main.go
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
    matrix gmap.DistanceMatrix // interface — testable, swappable
}

func (s *PlanningService) buildProblem(ctx context.Context, job Job) error {
    durations, distances, err := s.matrix.BuildMatrix(ctx, locs, gmap.MatrixOptions{
        DepartureTime: job.PlannedDepartureUnix,
        TrafficModel:  "pessimistic", // time-critical delivery
    })
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

## Environment Variables

```env
GOOGLE_MAPS_API_KEY=   # ใน rop-backend/.env
```

ต้องเปิด **Distance Matrix API** แยกใน Google Cloud Console — คนละตัวกับ `GOOGLE_CLIENT_ID` (OAuth)
