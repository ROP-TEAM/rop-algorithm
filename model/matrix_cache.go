package model

import "time"

type CachePolicy string

const (
	CachePolicyStatic  CachePolicy = "static"
	CachePolicyTraffic CachePolicy = "traffic"
)

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

