# osrm — Open Source Routing Machine

`package osrm` — data layer สำหรับดึง travel time และ distance จาก OSRM server ผ่าน HTTP API

implement `gmap.DistanceMatrix` interface — ใช้แทน Google Maps ได้ทันทีด้วย config switch

## Prerequisites

1. OSRM server รันอยู่ (Docker หรือ binary)
2. ใส่ค่าใน `rop-backend/.env`:
   ```
   DISTANCE_MATRIX_PROVIDER=osrm
   OSRM_BASE_URL=http://localhost:5000
   ```

## Quick Start

```go
import "github.com/ROP-TEAM/rop-algorithm/osrm"

m, err := osrm.NewOSRMMatrix("http://localhost:5000")
if err != nil {
    return err
}

locs := []model.Location{
    model.NewLatLngLocation(13.756300, 100.501800),
    model.NewLatLngLocation(13.746900, 100.534600),
}

durations, distances, err := m.BuildMatrix(ctx, locs, model.MatrixOptions{})
// durations[i][j] = เวลาเดินทาง i→j (นาที)
// distances[i][j] = ระยะทาง i→j (เมตร)
```

---

## Key Differences vs Google Maps

| เรื่อง | gmap (Google) | osrm |
|---|---|---|
| Coord format | `lat,lng` | `lng,lat` |
| Duration unit | seconds → `/60` → int min | seconds → `/60` → int min |
| API key | ต้องใช้ | ไม่ต้องใช้ |
| Traffic | รองรับ (`departure_time`, `traffic_model`) | ไม่รองรับ — `MatrixOptions` ถูกละเลย |
| Chunking | auto-chunk 10×10 (limit 100 elements) | ไม่จำเป็น — OSRM รับใหญ่พอ |
| Cache | in-memory cache, Redis support | ยังไม่มี |
| Observability | event hook + metrics | ยังไม่มี |

---

## API Methods

### `BuildMatrix` — n×n matrix

```go
func (m *OSRMMatrix) BuildMatrix(ctx context.Context, locs []model.Location, opts model.MatrixOptions) ([][]int, [][]int, error)
```

เรียก `GET /table/v1/driving/{lng,lat;...}?annotations=duration,distance`

- `durations` — int นาที (OSRM คืนวินาที → หาร 60)
- `distances` — int เมตร
- `opts` ถูกละเลย (OSRM ไม่มี traffic model)

```go
locs := []model.Location{
    model.NewLatLngLocation(13.756300, 100.501800),
    model.NewLatLngLocation(13.746900, 100.534600),
    model.NewLatLngLocation(13.730800, 100.541800),
}
durations, distances, err := m.BuildMatrix(ctx, locs, model.MatrixOptions{})
// durations → [][]int ขนาด 3×3
// distances → [][]int ขนาด 3×3
```

### `BuildRectangularMatrix` — sources × destinations

```go
func (m *OSRMMatrix) BuildRectangularMatrix(ctx context.Context, sources, destinations []model.Location) ([][]int, [][]int, error)
```

ใช้ `sources` และ `destinations` params — เหมาะกับ depot→orders (1×N แทน N×N)

```go
depot := []model.Location{model.NewLatLngLocation(13.756300, 100.501800)}
orders := []model.Location{
    model.NewLatLngLocation(13.746900, 100.534600),
    model.NewLatLngLocation(13.730800, 100.541800),
}
durations, distances, err := m.BuildRectangularMatrix(ctx, depot, orders)
// durations → [][]int ขนาด 1×2
// durations[0][0] = depot→order1, durations[0][1] = depot→order2
```

### `Nearest` — snap to road

```go
func (m *OSRMMatrix) Nearest(ctx context.Context, loc model.Location) (NearestPoint, error)
```

เรียก `GET /nearest/v1/driving/{lng,lat}?number=1`

ใช้ validate ว่า location อยู่บนถนนก่อน optimization

```go
result, err := m.Nearest(ctx, model.NewLatLngLocation(13.756300, 100.501800))
// result.Name     = "Main Street"
// result.Distance = 5.2    ← ห่างจากถนน (เมตร)
// result.Lat, result.Lng   ← พิกัดที่ snap ไป
```

### `Route` — full route geometry

```go
func (m *OSRMMatrix) Route(ctx context.Context, locs []model.Location) (RouteResult, error)
```

เรียก `GET /route/v1/driving/{coords}?geometries=polyline&overview=full`

ใช้ดึง polyline จริงระหว่าง stops — สำหรับแสดงบนแผนที่ frontend

```go
locs := []model.Location{
    model.NewLatLngLocation(13.756300, 100.501800),
    model.NewLatLngLocation(13.746900, 100.534600),
}
result, err := m.Route(ctx, locs)
// result.Routes[0].Distance = 5200.5        (เมตร)
// result.Routes[0].Duration = 600.0         (วินาที)
// result.Routes[0].Geometry = "polyline..." (encoded polyline)
// result.Waypoints → snapped waypoint list
```

### `Trip` — TSP solver

```go
func (m *OSRMMatrix) Trip(ctx context.Context, locs []model.Location) (TripResult, error)
```

เรียก `GET /trip/v1/driving/{coords}?roundtrip=false&source=first`

OSRM แก้ TSP ด้วย farthest-insertion heuristic — ใช้เป็น baseline benchmark เทียบกับ ALNS ได้

```go
locs := []model.Location{
    model.NewLatLngLocation(13.756300, 100.501800), // จุดเริ่มต้น (source=first)
    model.NewLatLngLocation(13.746900, 100.534600),
    model.NewLatLngLocation(13.730800, 100.541800),
}
result, err := m.Trip(ctx, locs)
// result.Waypoints[i].WaypointIndex  → ลำดับการเยี่ยมชม
// result.Distance = 8500.5           (เมตร)
// result.Duration = 1200.0           (วินาที)
// result.Geometry = "polyline..."    (encoded polyline)
```

---

## Error Handling

OSRM response code `!= "Ok"` → คืน error ทันที:

```
Ok          — ปกติ
NoTable     — ไม่มีเส้นทางระหว่างสองจุด
NoRoute     — /route ไม่พบเส้นทาง
NoTrips     — /trip ไม่สามารถสร้าง trip ได้
InvalidValue — พารามิเตอร์ไม่ถูกต้อง
```

HTTP status != 200 → error

---

## Wire กับ rop-backend

`matrix_factory.go` เลือก implementation ตาม env var:

```go
// internal/services/matrix_factory.go
func NewDistanceMatrix(cfg *config.Config) (gmap.DistanceMatrix, error) {
    switch cfg.DISTANCE_MATRIX_PROVIDER {
    case "osrm":
        return osrm.NewOSRMMatrix(cfg.OSRM_BASE_URL)
    case "google", "":
        return gmap.NewGoogleMapsMatrix(cfg.GOOGLE_MAPS_API_KEY)
    default:
        return nil, fmt.Errorf("unknown DISTANCE_MATRIX_PROVIDER: %s", cfg.DISTANCE_MATRIX_PROVIDER)
    }
}
```

```go
// main.go
matrix, err := services.NewDistanceMatrix(cfg)
if err != nil {
    log.Fatal(err)
}

planningService := services.NewPlanningService(db, matrix)
```

เปลี่ยน provider โดยแก้ `.env` อย่างเดียว:

```env
# ใช้ Google Maps (default)
DISTANCE_MATRIX_PROVIDER=google
GOOGLE_MAPS_API_KEY=AIza...

# ใช้ OSRM
DISTANCE_MATRIX_PROVIDER=osrm
OSRM_BASE_URL=http://localhost:5000
```

---

## OSRM Coordinate Format

OSRM ใช้ `lng,lat` (กลับข้างกับ Google Maps `lat,lng`):

```
Google: 13.756300,100.501800    (lat,lng)
OSRM:   100.501800,13.756300    (lng,lat)
```

package จัดการกลับข้างให้อัตโนมัติ — รับ `model.Location` ปกติ ไม่ต้องคิดเอง

---

## ไฟล์ในแพ็กเกจ

| ไฟล์ | หน้าที่ |
|---|---|
| `matrix_service.go` | `OSRMMatrix` struct, constructor, `BuildMatrix` (implement `gmap.DistanceMatrix`) |
| `matrix_endpoints.go` | `Nearest`, `Trip`, `Route`, `BuildRectangularMatrix` |
| `matrix_http.go` | `executeHTTP`, `formatCoordinate`, `formatCoordinates`, `parseTableResponse` |
| `matrix_response.go` | OSRM response types + public result types |
| `matrix_service_test.go` | 5 tests: BuildMatrix happy path, HTTP error, OSRM error, empty locs, row mismatch |
| `matrix_endpoints_test.go` | 4 tests: Nearest, Trip, Route, BuildRectangularMatrix |
