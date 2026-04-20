package graph

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"sort"
	"strings"
	"sync"
	"time"
)

const (
	defaultStaticCacheNamespace  = "matrix:static:v1"
	defaultTrafficCacheNamespace = "matrix:traffic:v1"
)

type CachePolicy string

const (
	CachePolicyStatic  CachePolicy = "static"
	CachePolicyTraffic CachePolicy = "traffic"
)

// MatrixCache is the storage contract for caching resolved matrix results.
type MatrixCache interface {
	Get(ctx context.Context, key string) (*DistanceMatrixResult, bool, error)
	Set(ctx context.Context, key string, value *DistanceMatrixResult, ttl time.Duration) error
}

type memoryMatrixCacheEntry struct {
	value     *DistanceMatrixResult
	expiresAt time.Time
}

// MemoryMatrixCache is a simple in-memory cache implementation intended for single-process use.
type MemoryMatrixCache struct {
	mu      sync.RWMutex
	entries map[string]memoryMatrixCacheEntry
	now     func() time.Time
}

// MatrixCacheConfig controls cache behavior for both current static mode and future traffic mode.
type MatrixCacheConfig struct {
	Enabled        bool
	TrafficEnabled bool

	StaticNamespace  string
	TrafficNamespace string

	StaticTTL    time.Duration
	WalkingTTL   time.Duration
	BicyclingTTL time.Duration
	TransitTTL   time.Duration

	TrafficTTLs map[string]time.Duration
}

// MatrixCacheKeyParts captures the human-meaningful pieces used to build a cache key.
type MatrixCacheKeyParts struct {
	Policy    CachePolicy `json:"policy"`
	Namespace string      `json:"namespace"`
	Hash      string      `json:"hash"`
	Date      string      `json:"date,omitempty"`
	Slot      string      `json:"slot,omitempty"`
	Key       string      `json:"key"`
}

type normalizedMatrixCacheRequest struct {
	Origins                  []string `json:"origins"`
	Destinations             []string `json:"destinations"`
	Mode                     string   `json:"mode"`
	Units                    string   `json:"units"`
	Language                 string   `json:"language"`
	Region                   string   `json:"region"`
	Avoid                    []string `json:"avoid"`
	TransitMode              []string `json:"transit_mode"`
	TransitRoutingPreference string   `json:"transit_routing_preference"`
	TrafficModel             string   `json:"traffic_model,omitempty"`
	Date                     string   `json:"date,omitempty"`
	Slot                     string   `json:"slot,omitempty"`
}

func DefaultMatrixCacheConfig() MatrixCacheConfig {
	return MatrixCacheConfig{
		Enabled:          false,
		TrafficEnabled:   false,
		StaticNamespace:  defaultStaticCacheNamespace,
		TrafficNamespace: defaultTrafficCacheNamespace,
		StaticTTL:        24 * time.Hour,
		WalkingTTL:       7 * 24 * time.Hour,
		BicyclingTTL:     7 * 24 * time.Hour,
		TransitTTL:       6 * time.Hour,
		TrafficTTLs: map[string]time.Duration{
			"morning": 1 * time.Hour,
			"midday":  4 * time.Hour,
			"evening": 1 * time.Hour,
			"night":   8 * time.Hour,
		},
	}
}

// DevMatrixCacheConfig returns a cache configuration optimized for development
// environments where minimizing API calls is more important than freshness.
func DevMatrixCacheConfig() MatrixCacheConfig {
	cfg := DefaultMatrixCacheConfig()
	monthTTL := 30 * 24 * time.Hour

	cfg.Enabled = true
	cfg.StaticTTL = monthTTL
	cfg.WalkingTTL = monthTTL
	cfg.BicyclingTTL = monthTTL
	cfg.TransitTTL = monthTTL
	cfg.TrafficTTLs = map[string]time.Duration{
		"morning": monthTTL,
		"midday":  monthTTL,
		"evening": monthTTL,
		"night":   monthTTL,
	}

	return cfg
}

func NewMemoryMatrixCache() *MemoryMatrixCache {
	return &MemoryMatrixCache{
		entries: make(map[string]memoryMatrixCacheEntry),
		now:     time.Now,
	}
}

func ResolveCachePolicy(req DistanceMatrixRequest) CachePolicy {
	if req.DepartureTime != 0 || req.DepartureTimeNow || req.TrafficModel != "" {
		return CachePolicyTraffic
	}

	return CachePolicyStatic
}

func BuildMatrixCacheKey(req DistanceMatrixRequest, now time.Time, cfg MatrixCacheConfig) (MatrixCacheKeyParts, error) {
	if err := validateDistanceMatrixRequest(req); err != nil {
		return MatrixCacheKeyParts{}, err
	}

	policy := ResolveCachePolicy(req)
	switch policy {
	case CachePolicyStatic:
		normalized := normalizeMatrixCacheRequest(req, "", "")
		hash, err := hashNormalizedMatrixCacheRequest(normalized)
		if err != nil {
			return MatrixCacheKeyParts{}, err
		}

		namespace := cfg.StaticNamespace
		if namespace == "" {
			namespace = defaultStaticCacheNamespace
		}

		return MatrixCacheKeyParts{
			Policy:    policy,
			Namespace: namespace,
			Hash:      hash,
			Key:       namespace + ":" + hash,
		}, nil
	case CachePolicyTraffic:
		when := resolveTrafficReferenceTime(req, now)
		date := when.Format("2006-01-02")
		slot := MatrixTrafficSlot(when)
		normalized := normalizeMatrixCacheRequest(req, date, slot)
		hash, err := hashNormalizedMatrixCacheRequest(normalized)
		if err != nil {
			return MatrixCacheKeyParts{}, err
		}

		namespace := cfg.TrafficNamespace
		if namespace == "" {
			namespace = defaultTrafficCacheNamespace
		}

		return MatrixCacheKeyParts{
			Policy:    policy,
			Namespace: namespace,
			Hash:      hash,
			Date:      date,
			Slot:      slot,
			Key:       namespace + ":" + date + ":" + slot + ":" + hash,
		}, nil
	default:
		return MatrixCacheKeyParts{}, fmt.Errorf("unsupported cache policy: %s", policy)
	}
}

func MatrixCacheTTL(req DistanceMatrixRequest, now time.Time, cfg MatrixCacheConfig) time.Duration {
	policy := ResolveCachePolicy(req)

	if policy == CachePolicyTraffic {
		slot := MatrixTrafficSlot(resolveTrafficReferenceTime(req, now))
		if ttl, ok := cfg.TrafficTTLs[slot]; ok && ttl > 0 {
			return ttl
		}
		return 0
	}

	switch normalizedMode(req.Mode) {
	case "walking":
		return cfg.WalkingTTL
	case "bicycling":
		return cfg.BicyclingTTL
	case "transit":
		return cfg.TransitTTL
	default:
		return cfg.StaticTTL
	}
}

func MatrixTrafficSlot(t time.Time) string {
	hour := t.Hour()
	switch {
	case hour >= 6 && hour < 9:
		return "morning"
	case hour >= 9 && hour < 16:
		return "midday"
	case hour >= 16 && hour < 20:
		return "evening"
	default:
		return "night"
	}
}

func (c *MemoryMatrixCache) Get(ctx context.Context, key string) (*DistanceMatrixResult, bool, error) {
	_ = ctx

	nowFn := c.now
	if nowFn == nil {
		nowFn = time.Now
	}

	c.mu.RLock()
	entry, ok := c.entries[key]
	c.mu.RUnlock()
	if !ok {
		return nil, false, nil
	}

	if !entry.expiresAt.IsZero() && !entry.expiresAt.After(nowFn()) {
		c.mu.Lock()
		delete(c.entries, key)
		c.mu.Unlock()
		return nil, false, nil
	}

	return cloneDistanceMatrixResult(entry.value), true, nil
}

func (c *MemoryMatrixCache) Set(ctx context.Context, key string, value *DistanceMatrixResult, ttl time.Duration) error {
	_ = ctx

	nowFn := c.now
	if nowFn == nil {
		nowFn = time.Now
	}

	entry := memoryMatrixCacheEntry{
		value: cloneDistanceMatrixResult(value),
	}
	if ttl > 0 {
		entry.expiresAt = nowFn().Add(ttl)
	}

	c.mu.Lock()
	c.entries[key] = entry
	c.mu.Unlock()
	return nil
}

func normalizeMatrixCacheRequest(req DistanceMatrixRequest, date, slot string) normalizedMatrixCacheRequest {
	avoid := append([]string(nil), req.Avoid...)
	sort.Strings(avoid)

	transitMode := append([]string(nil), req.TransitMode...)
	sort.Strings(transitMode)

	return normalizedMatrixCacheRequest{
		Origins:                  append([]string(nil), req.Origins...),
		Destinations:             append([]string(nil), req.Destinations...),
		Mode:                     normalizedMode(req.Mode),
		Units:                    normalizedUnits(req.Units),
		Language:                 strings.TrimSpace(req.Language),
		Region:                   strings.TrimSpace(req.Region),
		Avoid:                    avoid,
		TransitMode:              transitMode,
		TransitRoutingPreference: strings.TrimSpace(req.TransitRoutingPreference),
		TrafficModel:             strings.TrimSpace(req.TrafficModel),
		Date:                     date,
		Slot:                     slot,
	}
}

func hashNormalizedMatrixCacheRequest(req normalizedMatrixCacheRequest) (string, error) {
	payload, err := json.Marshal(req)
	if err != nil {
		return "", fmt.Errorf("marshal normalized matrix cache request: %w", err)
	}

	sum := sha256.Sum256(payload)
	return hex.EncodeToString(sum[:]), nil
}

func resolveTrafficReferenceTime(req DistanceMatrixRequest, now time.Time) time.Time {
	if req.DepartureTime != 0 {
		return time.Unix(req.DepartureTime, 0).In(now.Location())
	}
	if req.ArrivalTime != 0 {
		return time.Unix(req.ArrivalTime, 0).In(now.Location())
	}
	return now
}

func normalizedMode(mode string) string {
	mode = strings.TrimSpace(mode)
	if mode == "" {
		return "driving"
	}
	return mode
}

func normalizedUnits(units string) string {
	units = strings.TrimSpace(units)
	if units == "" {
		return "metric"
	}
	return units
}

func cloneDistanceMatrixResult(src *DistanceMatrixResult) *DistanceMatrixResult {
	if src == nil {
		return nil
	}

	dst := &DistanceMatrixResult{
		Request:   cloneDistanceMatrixRequest(src.Request),
		Response:  cloneDistanceMatrixResponse(src.Response),
		Durations: cloneIntMatrix(src.Durations),
		Distances: cloneIntMatrix(src.Distances),
	}
	return dst
}

func cloneDistanceMatrixRequest(src DistanceMatrixRequest) DistanceMatrixRequest {
	return DistanceMatrixRequest{
		Origins:                  append([]string(nil), src.Origins...),
		Destinations:             append([]string(nil), src.Destinations...),
		Mode:                     src.Mode,
		Units:                    src.Units,
		Language:                 src.Language,
		Region:                   src.Region,
		Avoid:                    append([]string(nil), src.Avoid...),
		DepartureTime:            src.DepartureTime,
		DepartureTimeNow:         src.DepartureTimeNow,
		TrafficModel:             src.TrafficModel,
		ArrivalTime:              src.ArrivalTime,
		TransitMode:              append([]string(nil), src.TransitMode...),
		TransitRoutingPreference: src.TransitRoutingPreference,
	}
}

func cloneDistanceMatrixResponse(src DistanceMatrixResponse) DistanceMatrixResponse {
	dst := DistanceMatrixResponse{
		Status:               src.Status,
		ErrorMessage:         src.ErrorMessage,
		OriginAddresses:      append([]string(nil), src.OriginAddresses...),
		DestinationAddresses: append([]string(nil), src.DestinationAddresses...),
		Rows:                 make([]DistanceMatrixRow, len(src.Rows)),
	}

	for i, row := range src.Rows {
		dst.Rows[i] = DistanceMatrixRow{
			Elements: make([]DistanceMatrixElement, len(row.Elements)),
		}
		for j, el := range row.Elements {
			dst.Rows[i].Elements[j] = DistanceMatrixElement{
				Status:            el.Status,
				Distance:          cloneValueText(el.Distance),
				Duration:          cloneValueText(el.Duration),
				DurationInTraffic: cloneValueText(el.DurationInTraffic),
				Fare:              cloneTransitFare(el.Fare),
			}
		}
	}

	return dst
}

func cloneIntMatrix(src [][]int) [][]int {
	if src == nil {
		return nil
	}

	dst := make([][]int, len(src))
	for i := range src {
		dst[i] = append([]int(nil), src[i]...)
	}
	return dst
}

func cloneValueText(src *ValueText) *ValueText {
	if src == nil {
		return nil
	}

	dst := *src
	return &dst
}

func cloneTransitFare(src *TransitFare) *TransitFare {
	if src == nil {
		return nil
	}

	dst := *src
	return &dst
}
