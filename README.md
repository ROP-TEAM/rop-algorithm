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
│   └── car.go              — placeholder
├── graph/
│   ├── matrix_service.go   — GoogleMapsMatrix struct + options, ExecuteMatrix, BuildMatrix, cache/emit orchestration
│   ├── matrix_request.go   — BuildDistanceMatrixQuery, ValidateDistanceMatrixRequest, buildDistanceMatrixRequest, locationRequestValue
│   ├── matrix_http.go      — doDistanceMatrixRequest, HTTP execution, response decode, API status/shape checks
│   ├── matrix_cache.go     — ResolveCachePolicy, BuildMatrixCacheKey, MatrixCacheTTL, MatrixTrafficSlot, key hashing
│   ├── matrix_cache_memory.go — MatrixCache interface, MemoryMatrixCache, clone helpers
│   ├── matrix_observability.go — MatrixEventHook, MatrixMetricsCollector, MatrixMetrics
│   ├── matrix_compat.go    — type aliases re-exporting model types (backward compat)
│   └── pathFinder.go       — placeholder
├── test/
│   ├── matrix_test.go      — integration + HTTP + query-building tests
│   └── matrix_cache_test.go — cache key/TTL/policy unit tests
├── core/
│   ├── constraint/         — feasibility checker
│   ├── priority/           — node sorting
│   └── timeWindow/         — time window validation
└── solver/                 — ALNS main loop
```

Layer rule: **model** = data shapes only · **graph** = logic/infrastructure · **test** = verification

---

## Commands

```bash
cd rop-algorithm
go build ./...
go test ./...
go vet ./...
```

---

## Distance Matrix

ดูรายละเอียดการใช้งาน Google Maps Distance Matrix API, caching, และการ wire กับ backend ได้ที่

→ [google_maps_api](google_map_API\README.md)

---
