package graph

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"time"
)

const (
	defaultDistanceMatrixURL = "https://maps.googleapis.com/maps/api/distancematrix/json"
	defaultChunkSize         = 10
)

// Location is a convenience type used by the compatibility BuildMatrix wrapper.
type Location struct {
	Lat float64
	Lng float64

	// Raw overrides Lat/Lng formatting and is sent as-is to the API.
	// Supports address, place_id:..., plus code, enc:...:, side_of_road:..., heading=X:...
	Raw string
}

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

func NewGoogleMapsMatrix(apiKey string) (*GoogleMapsMatrix, error) {
	if strings.TrimSpace(apiKey) == "" {
		return nil, fmt.Errorf("google maps api key is required")
	}

	return &GoogleMapsMatrix{
		apiKey:      apiKey,
		httpClient:  http.DefaultClient,
		baseURL:     defaultDistanceMatrixURL,
		cacheConfig: DefaultMatrixCacheConfig(),
		now:         time.Now,
	}, nil
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

func (g *GoogleMapsMatrix) doDistanceMatrixRequest(ctx context.Context, req DistanceMatrixRequest) (*DistanceMatrixResponse, error) {
	client := g.httpClient
	if client == nil {
		client = http.DefaultClient
	}

	g.emitEvent(ctx, MatrixEvent{
		Name:              "api_request",
		Policy:            ResolveCachePolicy(req),
		ChunkOrigins:      len(req.Origins),
		ChunkDestinations: len(req.Destinations),
	})

	httpReq, err := http.NewRequestWithContext(ctx, http.MethodGet, g.buildDistanceMatrixURL(req), nil)
	if err != nil {
		g.emitEvent(ctx, MatrixEvent{
			Name:         "api_request_error",
			Policy:       ResolveCachePolicy(req),
			Error:        err.Error(),
			ChunkOrigins: len(req.Origins),
			ChunkDestinations: len(req.Destinations),
		})
		return nil, err
	}

	resp, err := client.Do(httpReq)
	if err != nil {
		g.emitEvent(ctx, MatrixEvent{
			Name:              "api_request_error",
			Policy:            ResolveCachePolicy(req),
			Error:             err.Error(),
			ChunkOrigins:      len(req.Origins),
			ChunkDestinations: len(req.Destinations),
		})
		return nil, fmt.Errorf("distance matrix API: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		g.emitEvent(ctx, MatrixEvent{
			Name:              "api_response_error",
			Policy:            ResolveCachePolicy(req),
			Error:             resp.Status,
			ChunkOrigins:      len(req.Origins),
			ChunkDestinations: len(req.Destinations),
		})
		return nil, fmt.Errorf("distance matrix API unexpected HTTP status: %s", resp.Status)
	}

	var matrixResp DistanceMatrixResponse
	if err := json.NewDecoder(resp.Body).Decode(&matrixResp); err != nil {
		g.emitEvent(ctx, MatrixEvent{
			Name:              "api_decode_error",
			Policy:            ResolveCachePolicy(req),
			Error:             err.Error(),
			ChunkOrigins:      len(req.Origins),
			ChunkDestinations: len(req.Destinations),
		})
		return nil, fmt.Errorf("distance matrix API decode: %w", err)
	}

	if matrixResp.Status != "OK" {
		errMsg := matrixResp.Status
		if matrixResp.ErrorMessage != "" {
			errMsg += ": " + matrixResp.ErrorMessage
		}
		g.emitEvent(ctx, MatrixEvent{
			Name:              "api_status_error",
			Policy:            ResolveCachePolicy(req),
			Error:             errMsg,
			ChunkOrigins:      len(req.Origins),
			ChunkDestinations: len(req.Destinations),
		})
		if matrixResp.ErrorMessage != "" {
			return nil, fmt.Errorf("distance matrix API status: %s (%s)", matrixResp.Status, matrixResp.ErrorMessage)
		}
		return nil, fmt.Errorf("distance matrix API status: %s", matrixResp.Status)
	}

	if len(matrixResp.Rows) != len(req.Origins) {
		g.emitEvent(ctx, MatrixEvent{
			Name:              "api_shape_error",
			Policy:            ResolveCachePolicy(req),
			Error:             "row_count_mismatch",
			ChunkOrigins:      len(req.Origins),
			ChunkDestinations: len(req.Destinations),
		})
		return nil, fmt.Errorf("distance matrix API returned %d rows, expected %d", len(matrixResp.Rows), len(req.Origins))
	}

	return &matrixResp, nil
}

func (g *GoogleMapsMatrix) buildDistanceMatrixURL(req DistanceMatrixRequest) string {
	baseURL := g.baseURL
	if baseURL == "" {
		baseURL = defaultDistanceMatrixURL
	}

	return baseURL + "?" + buildDistanceMatrixQuery(req, g.apiKey).Encode()
}

func buildDistanceMatrixQuery(req DistanceMatrixRequest, apiKey string) url.Values {
	query := url.Values{}
	query.Set("origins", strings.Join(req.Origins, "|"))
	query.Set("destinations", strings.Join(req.Destinations, "|"))
	query.Set("key", apiKey)

	mode := req.Mode
	if mode == "" {
		mode = "driving"
	}
	query.Set("mode", mode)

	units := req.Units
	if units == "" {
		units = "metric"
	}
	query.Set("units", units)

	if req.Language != "" {
		query.Set("language", req.Language)
	}
	if req.Region != "" {
		query.Set("region", req.Region)
	}
	if len(req.Avoid) > 0 {
		query.Set("avoid", strings.Join(req.Avoid, "|"))
	}
	if req.DepartureTimeNow {
		query.Set("departure_time", "now")
	} else if req.DepartureTime != 0 {
		query.Set("departure_time", strconv.FormatInt(req.DepartureTime, 10))
	}
	if req.ArrivalTime != 0 {
		query.Set("arrival_time", strconv.FormatInt(req.ArrivalTime, 10))
	}
	if req.TrafficModel != "" {
		query.Set("traffic_model", req.TrafficModel)
	}
	if len(req.TransitMode) > 0 {
		query.Set("transit_mode", strings.Join(req.TransitMode, "|"))
	}
	if req.TransitRoutingPreference != "" {
		query.Set("transit_routing_preference", req.TransitRoutingPreference)
	}

	return query
}

func buildDistanceMatrixRequest(locs []Location, opts MatrixOptions) DistanceMatrixRequest {
	points := make([]string, len(locs))
	for i, loc := range locs {
		points[i] = loc.requestValue()
	}

	req := DistanceMatrixRequest{
		Origins:                  points,
		Destinations:             points,
		Mode:                     "driving",
		Units:                    "metric",
		Language:                 opts.Language,
		Region:                   opts.Region,
		Avoid:                    append([]string(nil), opts.Avoid...),
		DepartureTime:            opts.DepartureTime,
		DepartureTimeNow:         opts.DepartureTimeNow,
		ArrivalTime:              opts.ArrivalTime,
		TrafficModel:             opts.TrafficModel,
		TransitMode:              append([]string(nil), opts.TransitMode...),
		TransitRoutingPreference: opts.TransitRoutingPreference,
	}
	if opts.Mode != "" {
		req.Mode = opts.Mode
	}
	if opts.Units != "" {
		req.Units = opts.Units
	}

	return req
}

func validateDistanceMatrixRequest(req DistanceMatrixRequest) error {
	if len(req.Origins) == 0 {
		return fmt.Errorf("origins are required")
	}
	if len(req.Destinations) == 0 {
		return fmt.Errorf("destinations are required")
	}
	if req.DepartureTime != 0 && req.DepartureTimeNow {
		return fmt.Errorf("departure_time and departure_time=now cannot be used together")
	}
	if (req.DepartureTime != 0 || req.DepartureTimeNow) && req.ArrivalTime != 0 {
		return fmt.Errorf("departure_time and arrival_time cannot be used together")
	}

	return nil
}

func chunkSizeForRequest(req DistanceMatrixRequest) int {
	if req.DepartureTime != 0 || req.DepartureTimeNow {
		return defaultChunkSize
	}

	return defaultChunkSize
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

func (l Location) requestValue() string {
	if strings.TrimSpace(l.Raw) != "" {
		return l.Raw
	}

	return fmt.Sprintf("%s,%s",
		strconv.FormatFloat(l.Lat, 'f', 6, 64),
		strconv.FormatFloat(l.Lng, 'f', 6, 64),
	)
}

func clamp(v, max int) int {
	if v < max {
		return v
	}
	return max
}
