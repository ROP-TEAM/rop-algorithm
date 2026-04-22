package gmap

import (
	"context"
	"fmt"
	"net/http"
	"time"
)

const (
	defaultDistanceMatrixURL = "https://maps.googleapis.com/maps/api/distancematrix/json"
	defaultChunkSize         = 10
)

// DistanceMatrix is the contract the algorithm uses to get travel data.
type DistanceMatrix interface {
	// BuildMatrix returns n×n matrices of durations (minutes) and distances (meters).
	// Pass MatrixOptions{} for defaults (driving, no traffic).
	BuildMatrix(ctx context.Context, locs []Location, opts MatrixOptions) (durations [][]int, distances [][]int, err error)
}

// GoogleMapsMatrix implements DistanceMatrix via the Distance Matrix API.
type GoogleMapsMatrix struct {
	apiKey      string
	httpClient  *http.Client
	baseURL     string
	cache       MatrixCache
	cacheConfig MatrixCacheConfig
	now         func() time.Time
	eventHook   MatrixEventHook
	metrics     MatrixMetricsCollector
}

type GoogleMapsMatrixOption func(*GoogleMapsMatrix)

func (g *GoogleMapsMatrix) SetCache(cache MatrixCache, cfg MatrixCacheConfig) {
	g.cache = cache
	g.cacheConfig = cfg
}

func (g *GoogleMapsMatrix) EnableInMemoryCache(cfg MatrixCacheConfig) {
	g.cache = NewMemoryMatrixCache()
	g.cacheConfig = cfg
}

func (g *GoogleMapsMatrix) SetEventHook(hook MatrixEventHook)               { g.eventHook = hook }
func (g *GoogleMapsMatrix) SetMetricsCollector(c MatrixMetricsCollector)    { g.metrics = c }

func (g *GoogleMapsMatrix) ExecuteMatrix(ctx context.Context, req DistanceMatrixRequest) (*DistanceMatrixResult, error) {
	if err := validateDistanceMatrixRequest(req); err != nil {
		return nil, err
	}
	if cached, ok, err := g.getCachedMatrix(ctx, req); err != nil {
		return nil, err
	} else if ok {
		return cached, nil
	}
	result := allocateResult(req)
	for oStart := 0; oStart < len(req.Origins); oStart += defaultChunkSize {
		oEnd := clamp(oStart+defaultChunkSize, len(req.Origins))
		for dStart := 0; dStart < len(req.Destinations); dStart += defaultChunkSize {
			dEnd := clamp(dStart+defaultChunkSize, len(req.Destinations))
			chunk := buildChunk(req, oStart, oEnd, dStart, dEnd)
			resp, err := g.doDistanceMatrixRequest(ctx, chunk.req)
			if err != nil {
				return nil, err
			}
			if err := mergeChunkIntoResult(result, chunk, *resp); err != nil {
				return nil, err
			}
		}
	}
	if err := g.setCachedMatrix(ctx, result); err != nil {
		return nil, err
	}
	return result, nil
}

// BuildMatrix is a convenience wrapper around ExecuteMatrix for square n×n matrices.
func (g *GoogleMapsMatrix) BuildMatrix(ctx context.Context, locs []Location, opts MatrixOptions) ([][]int, [][]int, error) {
	req := buildDistanceMatrixRequest(locs, opts)
	result, err := g.ExecuteMatrix(ctx, req)
	if err != nil {
		return nil, nil, err
	}
	return result.Durations, result.Distances, nil
}

func (g *GoogleMapsMatrix) getCachedMatrix(ctx context.Context, req DistanceMatrixRequest) (*DistanceMatrixResult, bool, error) {
	policy := ResolveCachePolicy(req)
	key, ok, err := g.resolveCacheKey(req)
	if err != nil {
		g.emitEvent(ctx, MatrixEvent{Name: "cache_lookup_error", Policy: policy, Error: err.Error()})
		return nil, false, err
	}
	if !ok {
		reason := "cache_disabled"
		if g.cacheConfig.Enabled && policy == CachePolicyTraffic {
			reason = "traffic_cache_disabled"
		}
		g.emitEvent(ctx, MatrixEvent{Name: "cache_bypass", Policy: policy, Reason: reason})
		return nil, false, nil
	}
	value, hit, err := g.cache.Get(ctx, key)
	if err != nil {
		g.emitEvent(ctx, MatrixEvent{Name: "cache_lookup_error", Policy: policy, CacheKey: key, Error: err.Error()})
		return nil, false, err
	}
	if hit {
		g.emitEvent(ctx, MatrixEvent{Name: "cache_hit", Policy: policy, CacheKey: key})
		return value, true, nil
	}
	g.emitEvent(ctx, MatrixEvent{Name: "cache_miss", Policy: policy, CacheKey: key})
	return nil, false, nil
}

func (g *GoogleMapsMatrix) setCachedMatrix(ctx context.Context, result *DistanceMatrixResult) error {
	if result == nil {
		return nil
	}
	policy := ResolveCachePolicy(result.Request)
	key, ok, err := g.resolveCacheKey(result.Request)
	if err != nil {
		g.emitEvent(ctx, MatrixEvent{Name: "cache_store_error", Policy: policy, Error: err.Error()})
		return err
	}
	if !ok {
		return nil
	}
	nowFn := g.now
	if nowFn == nil {
		nowFn = time.Now
	}
	ttl := MatrixCacheTTL(result.Request, nowFn(), g.cacheConfig)
	if ttl <= 0 {
		g.emitEvent(ctx, MatrixEvent{Name: "cache_bypass", Policy: policy, CacheKey: key, Reason: "ttl_disabled"})
		return nil
	}
	if err := g.cache.Set(ctx, key, result, ttl); err != nil {
		g.emitEvent(ctx, MatrixEvent{Name: "cache_store_error", Policy: policy, CacheKey: key, Error: err.Error()})
		return err
	}
	g.emitEvent(ctx, MatrixEvent{Name: "cache_store", Policy: policy, CacheKey: key, Reason: ttl.String()})
	return nil
}

func (g *GoogleMapsMatrix) resolveCacheKey(req DistanceMatrixRequest) (string, bool, error) {
	if !g.cacheConfig.Enabled || g.cache == nil {
		return "", false, nil
	}
	if ResolveCachePolicy(req) == CachePolicyTraffic && !g.cacheConfig.TrafficEnabled {
		return "", false, nil
	}
	nowFn := g.now
	if nowFn == nil {
		nowFn = time.Now
	}
	keyParts, err := BuildMatrixCacheKey(req, nowFn(), g.cacheConfig)
	if err != nil {
		return "", false, err
	}
	return keyParts.Key, true, nil
}

func (g *GoogleMapsMatrix) emitEvent(ctx context.Context, event MatrixEvent) {
	if g.eventHook != nil {
		g.eventHook(ctx, event)
	}
	if g.metrics != nil {
		g.metrics.RecordMatrixEvent(event)
	}
}

func elementDurationMinutes(el DistanceMatrixElement) (int, error) {
	if el.DurationInTraffic != nil {
		return el.DurationInTraffic.Value / 60, nil
	}
	if el.Duration != nil {
		return el.Duration.Value / 60, nil
	}
	return 0, fmt.Errorf("element status OK but duration fields are absent")
}

func makeMatrix(rows, cols int) [][]int {
	out := make([][]int, rows)
	for i := range out {
		out[i] = make([]int, cols)
	}
	return out
}

func clamp(v, max int) int {
	if v < max {
		return v
	}
	return max
}
