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

## Google Maps API — พารามิเตอร์ทั้งหมด

### รูปแบบ Location

Origins และ Destinations รับค่าได้หลายรูปแบบในสตริงเดียวกัน:

| รูปแบบ | ตัวอย่าง |
|---|---|
| พิกัด lat,lng | `"13.756300,100.501800"` |
| place_id | `"place_id:ChIJTydCFXdnHTERB3oVT1UZDRI"` |
| ที่อยู่ (geocode ที่ฝั่ง API) | `"Central World, Bangkok"` |
| side_of_road | `"side_of_road:13.756300,100.501800"` |
| heading | `"heading=90:13.756300,100.501800"` (0–360°) |
| plus code | `"7P3Q+QJ Bangkok"` |
| encoded polyline | `"enc:polyline_value:"` |

เมื่อใช้ `Location` struct และ `BuildMatrix` — ถ้าตั้ง `Raw` จะส่งค่า Raw ตรงๆ ข้าม lat/lng จะถูก format เป็น `"lat,lng"` ให้อัตโนมัติ

### Mode (วิธีเดินทาง)

| ค่า | ใช้กับ |
|---|---|
| `"driving"` | รถยนต์ (default) |
| `"walking"` | เดินเท้า |
| `"bicycling"` | จักรยาน |
| `"transit"` | ขนส่งสาธารณะ (ต้องตั้ง `TransitMode` หรือ `DepartureTime`) |

### Avoid (หลีกเลี่ยงเส้นทาง)

ใช้ร่วมกัน mode `driving` และ `bicycling`; transit ไม่รองรับ:

| ค่า | คำอธิบาย |
|---|---|
| `"tolls"` | หลีกเลี่ยงทางด่วน |
| `"highways"` | หลีกเลี่ยงทางหลวง |
| `"ferries"` | หลีกเลี่ยงเรือข้ามฟาก |
| `"indoor"` | หลีกเลี่ยงเส้นทางในอาคาร (walking เท่านั้น) |

### Traffic (สภาพจราจร)

`DurationInTraffic` จะถูกส่งกลับมาเมื่อ **mode = driving** และ **ตั้ง departure_time** เท่านั้น:

| field | ประเภท | คำอธิบาย |
|---|---|---|
| `DepartureTime` | `int64` | Unix timestamp — ต้องเป็นเวลาปัจจุบันหรืออนาคตเท่านั้น |
| `DepartureTimeNow` | `bool` | ส่ง `departure_time=now` ใช้สภาพจราจรขณะนั้น |
| `TrafficModel` | `string` | ต้องใช้คู่กับ `DepartureTime` เสมอ |

ค่า `TrafficModel`:

| ค่า | พฤติกรรม |
|---|---|
| `"best_guess"` | ประมาณการจาก historical + realtime (default ถ้าไม่ระบุ) |
| `"pessimistic"` | เวลานานกว่าปกติ — เหมาะกับงาน time-critical |
| `"optimistic"` | เวลาน้อยกว่าปกติ — เหมาะกับ best-case scenario |

### Transit (ขนส่งสาธารณะ)

ใช้กับ mode `transit` เท่านั้น:

| field | ค่า |
|---|---|
| `TransitMode` | `"bus"`, `"subway"`, `"train"`, `"tram"`, `"rail"` |
| `TransitRoutingPreference` | `"less_walking"` หรือ `"fewer_transfers"` |
| `ArrivalTime` | Unix timestamp — ใช้แทน `DepartureTime` ได้ (ไม่สามารถใช้พร้อมกัน) |

### Validation Rules

```
departure_time  +  departure_time_now   → error (ใช้พร้อมกันไม่ได้)
departure_time  +  arrival_time         → error (ใช้พร้อมกันไม่ได้)
origins = []    หรือ  destinations = [] → error
```

---

## ข้อจำกัดของ Google Maps API

### Quota และ Element Limits

| ข้อจำกัด | ค่า |
|---|---|
| สูงสุดต่อ request | **100 elements** (origins × destinations) — Standard Plan |
| สูงสุด origins หรือ destinations ต่อ request | **25** แต่ละด้าน |
| **Effective limit** (package นี้ใช้) | **10 × 10 = 100 elements** ต่อ chunk |
| QPM (Queries Per Minute) | 100 QPS default (ปรับได้ใน Console) |
| OVER_QUERY_LIMIT | retry หลังจาก back-off |

package นี้ auto-chunk request ขนาดใหญ่เป็น block **10×10** แล้ว merge ผลลัพธ์กลับโดยอัตโนมัติ — ไม่ต้องจัดการ chunking เอง

### Response Status Codes

**API-level** (field `status` ใน top-level response):

| Status | ความหมาย |
|---|---|
| `OK` | สำเร็จ |
| `INVALID_REQUEST` | พารามิเตอร์ไม่ถูกต้อง |
| `MAX_ELEMENTS_EXCEEDED` | origins × destinations เกิน limit |
| `MAX_DIMENSIONS_EXCEEDED` | origins หรือ destinations เกิน 25 |
| `OVER_DAILY_LIMIT` | เกิน daily quota หรือ API key ปัญหา |
| `OVER_QUERY_LIMIT` | เกิน QPS limit — ต้อง retry |
| `REQUEST_DENIED` | API key ไม่มีสิทธิ์ หรือ Distance Matrix API ยังไม่เปิด |
| `UNKNOWN_ERROR` | server error ฝั่ง Google |

**Element-level** (field `status` ใน แต่ละ element):

| Status | ความหมาย |
|---|---|
| `OK` | คำนวณสำเร็จ — มี `distance` และ `duration` |
| `NOT_FOUND` | ไม่พบ origin หรือ destination |
| `ZERO_RESULTS` | ไม่มีเส้นทางระหว่างจุด |
| `MAX_ROUTE_LENGTH_EXCEEDED` | เส้นทางยาวเกิน limit (driving ~6500 km) |

package นี้ return `error` ทันทีเมื่อพบ element status ที่ไม่ใช่ `OK`

### ข้อจำกัดเฉพาะ Mode

| Mode | ข้อจำกัด |
|---|---|
| `driving` | รองรับ `avoid`, `DepartureTime`, `TrafficModel` |
| `walking` | ไม่รองรับ `avoid=tolls/highways`, ไม่มี traffic |
| `bicycling` | รองรับ `avoid=tolls/highways/ferries` เท่านั้น |
| `transit` | ไม่รองรับ `avoid` ทั้งหมด, ต้องการ `DepartureTime` หรือ `ArrivalTime` |
| `transit` | `DurationInTraffic` จะไม่ถูกส่งกลับ |

### ข้อควรระวัง

- **`DepartureTime` ต้องเป็นเวลาปัจจุบันหรืออนาคต** — เวลาในอดีตทำให้ API คืน `INVALID_REQUEST`
- **`place_id` geocoding** เกิดขึ้นที่ฝั่ง API — แต่ละ place_id ที่ unique ไม่นับเพิ่ม quota
- **`heading=X:`** ใช้กำหนดทิศทางการขับออกจากจุด — มีผลต่อ routing บนถนนทางเดียว
- **ที่อยู่ที่ geocode ไม่เจอ** จะคืน element status `NOT_FOUND` แทน API error
- **Transit ใน บางประเทศ** Google Maps อาจไม่มีข้อมูล — คืน `ZERO_RESULTS`

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

Cache policy แบ่งอัตโนมัติจาก request:

| Policy | เงื่อนไข | Key format |
|---|---|---|
| `static` | ไม่มี traffic fields | `namespace:SHA256(…)` |
| `traffic` | มี `DepartureTime` / `DepartureTimeNow` / `TrafficModel` | `namespace:YYYY-MM-DD:slot:SHA256(…)` |

**Default TTL** (`DefaultMatrixCacheConfig`):

| Mode / Slot | TTL |
|---|---|
| driving (static) | 24 ชั่วโมง |
| walking | 7 วัน |
| bicycling | 7 วัน |
| transit | 6 ชั่วโมง |
| traffic — morning (06:00–08:59) | 1 ชั่วโมง |
| traffic — midday (09:00–15:59) | 4 ชั่วโมง |
| traffic — evening (16:00–19:59) | 1 ชั่วโมง |
| traffic — night (20:00–05:59) | 8 ชั่วโมง |

Cache key ถูก normalize ก่อน hash (sort avoid/transit_mode, trim whitespace, fill defaults) — ทำให้ request ที่ logical เหมือนกันได้ cache เดียวกันเสมอ

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

Event names: `api_request`, `api_request_error`, `api_response_error`, `api_decode_error`, `api_status_error`, `api_shape_error`, `cache_hit`, `cache_miss`, `cache_bypass`, `cache_lookup_error`, `cache_store`, `cache_store_error`

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
