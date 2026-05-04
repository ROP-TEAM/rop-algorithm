package gmap

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"sort"
	"strings"
	"time"

	"github.com/ROP-TEAM/rop-algorithm/model"
)

const (
	defaultStaticCacheNamespace  = "matrix:static:v1"
	defaultTrafficCacheNamespace = "matrix:traffic:v1"
)

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

func DefaultMatrixCacheConfig() model.MatrixCacheConfig {
	return model.MatrixCacheConfig{
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
func DevMatrixCacheConfig() model.MatrixCacheConfig {
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

func ResolveCachePolicy(req model.DistanceMatrixRequest) model.CachePolicy {
	if req.DepartureTime != 0 || req.DepartureTimeNow || req.TrafficModel != "" {
		return model.CachePolicyTraffic
	}
	return model.CachePolicyStatic
}

func BuildMatrixCacheKey(req model.DistanceMatrixRequest, now time.Time, cfg model.MatrixCacheConfig) (model.MatrixCacheKeyParts, error) {
	if err := validateDistanceMatrixRequest(req); err != nil {
		return model.MatrixCacheKeyParts{}, err
	}
	policy := ResolveCachePolicy(req)
	switch policy {
	case model.CachePolicyStatic:
		ns := cfg.StaticNamespace
		if ns == "" {
			ns = defaultStaticCacheNamespace
		}
		return buildCacheKeyParts(model.MatrixCacheKeyParts{Policy: policy, Namespace: ns}, req)
	case model.CachePolicyTraffic:
		when := resolveTrafficReferenceTime(req, now)
		ns := cfg.TrafficNamespace
		if ns == "" {
			ns = defaultTrafficCacheNamespace
		}
		return buildCacheKeyParts(model.MatrixCacheKeyParts{
			Policy: policy, Namespace: ns,
			Date: when.Format("2006-01-02"), Slot: MatrixTrafficSlot(when),
		}, req)
	default:
		return model.MatrixCacheKeyParts{}, fmt.Errorf("unsupported cache policy: %s", policy)
	}
}

func buildCacheKeyParts(parts model.MatrixCacheKeyParts, req model.DistanceMatrixRequest) (model.MatrixCacheKeyParts, error) {
	normalized := normalizeMatrixCacheRequest(req, parts.Date, parts.Slot)
	hash, err := hashNormalizedMatrixCacheRequest(normalized)
	if err != nil {
		return model.MatrixCacheKeyParts{}, err
	}
	parts.Hash = hash
	if parts.Date != "" {
		parts.Key = parts.Namespace + ":" + parts.Date + ":" + parts.Slot + ":" + hash
	} else {
		parts.Key = parts.Namespace + ":" + hash
	}
	return parts, nil
}

func MatrixCacheTTL(req model.DistanceMatrixRequest, now time.Time, cfg model.MatrixCacheConfig) time.Duration {
	policy := ResolveCachePolicy(req)

	if policy == model.CachePolicyTraffic {
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

func normalizeMatrixCacheRequest(req model.DistanceMatrixRequest, date, slot string) normalizedMatrixCacheRequest {
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

func resolveTrafficReferenceTime(req model.DistanceMatrixRequest, now time.Time) time.Time {
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
