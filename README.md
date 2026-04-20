# rop-algorithm

Pure Go module สำหรับ Vehicle Routing Problem (VRP) optimization

---

## โครงสร้าง Module

```
rop-algorithm/
├── gmap/                       — package gmap: Google Maps data layer (nodes + edges)
│   ├── matrix_service.go       — GoogleMapsMatrix, ExecuteMatrix, BuildMatrix, orchestration
│   ├── matrix_request.go       — query building, validation, location normalization
│   ├── matrix_http.go          — HTTP execution, response decode, API checks
│   ├── matrix_cache.go         — cache policy, key, TTL logic
│   ├── matrix_cache_memory.go  — MatrixCache interface, MemoryMatrixCache
│   ├── matrix_observability.go — MatrixEventHook, MatrixMetricsCollector, MatrixMetrics
│   ├── matrix_compat.go        — type aliases re-exporting จาก model/
│   └── readme.md
├── graph/                      — package graph: graph algorithms (pathFinder, Dijkstra, A* ฯลฯ)
│   └── pathFinder.go
├── model/                      — data shapes only (ไม่มี behavior)
│   ├── matrix.go               — Location, MatrixOptions, DistanceMatrix{Request,Result,Response,…}
│   ├── matrix_cache.go         — CachePolicy, MatrixCacheConfig, MatrixCacheKeyParts
│   └── matrix_event.go         — MatrixEvent
├── core/                       — constraint, priority, time window
├── solver/                     — algorithm orchestration
└── test/
    ├── matrix_test.go          — integration + HTTP + query-building tests
    └── matrix_cache_test.go    — cache key/TTL/policy unit tests
```

Layer rule: **gmap** = Google Maps data · **graph** = graph algorithms · **model** = data shapes · **test** = verification

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

→ [gmap/readme.md](gmap/readme.md)

---
