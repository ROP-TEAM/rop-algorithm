package gmap

import (
	"context"
	"fmt"
	"net/http"
	"strings"
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

func WithBaseURL(baseURL string) GoogleMapsMatrixOption {
	return func(g *GoogleMapsMatrix) {
		g.baseURL = baseURL
	}
}

func WithHTTPClient(client *http.Client) GoogleMapsMatrixOption {
	return func(g *GoogleMapsMatrix) {
		g.httpClient = client
	}
}

func WithClock(now func() time.Time) GoogleMapsMatrixOption {
	return func(g *GoogleMapsMatrix) {
		g.now = now
	}
}

func NewGoogleMapsMatrix(apiKey string, opts ...GoogleMapsMatrixOption) (*GoogleMapsMatrix, error) {
	if strings.TrimSpace(apiKey) == "" {
		return nil, fmt.Errorf("google maps api key is required")
	}

	g := &GoogleMapsMatrix{
		apiKey:      apiKey,
		httpClient:  http.DefaultClient,
		baseURL:     defaultDistanceMatrixURL,
		cacheConfig: DefaultMatrixCacheConfig(),
		now:         time.Now,
	}
	for _, opt := range opts {
		if opt != nil {
			opt(g)
		}
	}

	return g, nil
}

func (g *GoogleMapsMatrix) SetCache(cache MatrixCache, cfg MatrixCacheConfig) {
	g.cache = cache
	g.cacheConfig = cfg
}

func (g *GoogleMapsMatrix) EnableInMemoryCache(cfg MatrixCacheConfig) {
	g.cache = NewMemoryMatrixCache()
	g.cacheConfig = cfg
}

func (g *GoogleMapsMatrix) SetEventHook(hook MatrixEventHook) {
	g.eventHook = hook
}

func (g *GoogleMapsMatrix) SetMetricsCollector(collector MatrixMetricsCollector) {
	g.metrics = collector
}

// ExecuteMatrix executes a Distance Matrix request and returns both raw response data
// and parsed duration/distance matrices.
func (g *GoogleMapsMatrix) ExecuteMatrix(ctx context.Context, req DistanceMatrixRequest) (*DistanceMatrixResult, error) {
	if err := validateDistanceMatrixRequest(req); err != nil {
		return nil, err
	}

	if cached, ok, err := g.getCachedMatrix(ctx, req); err != nil {
		return nil, err
	} else if ok {
		return cached, nil
	}

	result := &DistanceMatrixResult{
		Request:   req,
		Response:  DistanceMatrixResponse{Status: "OK"},
		Durations: makeMatrix(len(req.Origins), len(req.Destinations)),
		Distances: makeMatrix(len(req.Origins), len(req.Destinations)),
	}
	result.Response.OriginAddresses = make([]string, len(req.Origins))
	result.Response.DestinationAddresses = make([]string, len(req.Destinations))
	result.Response.Rows = make([]DistanceMatrixRow, len(req.Origins))
	for i := range result.Response.Rows {
		result.Response.Rows[i] = DistanceMatrixRow{
			Elements: make([]DistanceMatrixElement, len(req.Destinations)),
		}
	}

	chunkSize := chunkSizeForRequest(req)
	for oStart := 0; oStart < len(req.Origins); oStart += chunkSize {
		oEnd := clamp(oStart+chunkSize, len(req.Origins))

		for dStart := 0; dStart < len(req.Destinations); dStart += chunkSize {
			dEnd := clamp(dStart+chunkSize, len(req.Destinations))

			chunkReq := req
			chunkReq.Origins = append([]string(nil), req.Origins[oStart:oEnd]...)
			chunkReq.Destinations = append([]string(nil), req.Destinations[dStart:dEnd]...)

			chunkResp, err := g.doDistanceMatrixRequest(ctx, chunkReq)
			if err != nil {
				return nil, err
			}

			copy(result.Response.OriginAddresses[oStart:oEnd], chunkResp.OriginAddresses)
			copy(result.Response.DestinationAddresses[dStart:dEnd], chunkResp.DestinationAddresses)

			for ri, row := range chunkResp.Rows {
				if len(row.Elements) != len(chunkReq.Destinations) {
					return nil, fmt.Errorf("distance matrix API returned %d elements for row %d, expected %d", len(row.Elements), oStart+ri, len(chunkReq.Destinations))
				}

				for ci, el := range row.Elements {
					if el.Status != "OK" {
						return nil, fmt.Errorf("element [%d][%d] status: %s", oStart+ri, dStart+ci, el.Status)
					}

					result.Response.Rows[oStart+ri].Elements[dStart+ci] = el
					result.Durations[oStart+ri][dStart+ci] = elementDurationMinutes(el)
					if el.Distance != nil {
						result.Distances[oStart+ri][dStart+ci] = el.Distance.Value
					}
				}
			}
		}
	}

	if err := g.setCachedMatrix(ctx, result); err != nil {
		return nil, err
	}

	return result, nil
}

// BuildMatrix is a compatibility wrapper around ExecuteMatrix.
func (g *GoogleMapsMatrix) BuildMatrix(ctx context.Context, locs []Location, opts MatrixOptions) ([][]int, [][]int, error) {
	req := buildDistanceMatrixRequest(locs, opts)
	result, err := g.ExecuteMatrix(ctx, req)
	if err != nil {
		return nil, nil, err
	}

	return result.Durations, result.Distances, nil
}

func (g *GoogleMapsMatrix) getCachedMatrix(ctx context.Context, req DistanceMatrixRequest) (*DistanceMatrixResult, bool, error) {
	if !g.cacheConfig.Enabled || g.cache == nil {
		g.emitEvent(ctx, MatrixEvent{
			Name:   "cache_bypass",
			Policy: ResolveCachePolicy(req),
			Reason: "cache_disabled",
		})
		return nil, false, nil
	}

	if ResolveCachePolicy(req) == CachePolicyTraffic && !g.cacheConfig.TrafficEnabled {
		g.emitEvent(ctx, MatrixEvent{
			Name:   "cache_bypass",
			Policy: ResolveCachePolicy(req),
			Reason: "traffic_cache_disabled",
		})
		return nil, false, nil
	}

	nowFn := g.now
	if nowFn == nil {
		nowFn = time.Now
	}

	key, err := BuildMatrixCacheKey(req, nowFn(), g.cacheConfig)
	if err != nil {
		g.emitEvent(ctx, MatrixEvent{
			Name:   "cache_lookup_error",
			Policy: ResolveCachePolicy(req),
			Error:  err.Error(),
		})
		return nil, false, err
	}

	value, ok, err := g.cache.Get(ctx, key.Key)
	if err != nil {
		g.emitEvent(ctx, MatrixEvent{
			Name:     "cache_lookup_error",
			Policy:   ResolveCachePolicy(req),
			CacheKey: key.Key,
			Error:    err.Error(),
		})
		return nil, false, err
	}
	if ok {
		g.emitEvent(ctx, MatrixEvent{
			Name:     "cache_hit",
			Policy:   ResolveCachePolicy(req),
			CacheKey: key.Key,
		})
		return value, true, nil
	}

	g.emitEvent(ctx, MatrixEvent{
		Name:     "cache_miss",
		Policy:   ResolveCachePolicy(req),
		CacheKey: key.Key,
	})
	return nil, false, nil
}

func (g *GoogleMapsMatrix) setCachedMatrix(ctx context.Context, result *DistanceMatrixResult) error {
	if result == nil {
		return nil
	}

	if !g.cacheConfig.Enabled || g.cache == nil {
		return nil
	}

	if ResolveCachePolicy(result.Request) == CachePolicyTraffic && !g.cacheConfig.TrafficEnabled {
		return nil
	}

	nowFn := g.now
	if nowFn == nil {
		nowFn = time.Now
	}

	key, err := BuildMatrixCacheKey(result.Request, nowFn(), g.cacheConfig)
	if err != nil {
		g.emitEvent(ctx, MatrixEvent{
			Name:   "cache_store_error",
			Policy: ResolveCachePolicy(result.Request),
			Error:  err.Error(),
		})
		return err
	}

	ttl := MatrixCacheTTL(result.Request, nowFn(), g.cacheConfig)
	if ttl <= 0 {
		g.emitEvent(ctx, MatrixEvent{
			Name:     "cache_bypass",
			Policy:   ResolveCachePolicy(result.Request),
			CacheKey: key.Key,
			Reason:   "ttl_disabled",
		})
		return nil
	}

	if err := g.cache.Set(ctx, key.Key, result, ttl); err != nil {
		g.emitEvent(ctx, MatrixEvent{
			Name:     "cache_store_error",
			Policy:   ResolveCachePolicy(result.Request),
			CacheKey: key.Key,
			Error:    err.Error(),
		})
		return err
	}

	g.emitEvent(ctx, MatrixEvent{
		Name:     "cache_store",
		Policy:   ResolveCachePolicy(result.Request),
		CacheKey: key.Key,
		Reason:   ttl.String(),
	})
	return nil
}

func (g *GoogleMapsMatrix) cacheEnabledForRequest(req DistanceMatrixRequest) bool {
	if g.cache == nil || !g.cacheConfig.Enabled {
		return false
	}

	if ResolveCachePolicy(req) == CachePolicyTraffic && !g.cacheConfig.TrafficEnabled {
		return false
	}

	return true
}

func (g *GoogleMapsMatrix) emitEvent(ctx context.Context, event MatrixEvent) {
	if g.eventHook != nil {
		g.eventHook(ctx, event)
	}
	if g.metrics != nil {
		g.metrics.RecordMatrixEvent(event)
	}
}

func elementDurationMinutes(el DistanceMatrixElement) int {
	if el.DurationInTraffic != nil {
		return el.DurationInTraffic.Value / 60
	}
	if el.Duration != nil {
		return el.Duration.Value / 60
	}
	return 0
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
