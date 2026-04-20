package graph

import (
	"testing"
	"time"
)

func TestResolveCachePolicy(t *testing.T) {
	tests := []struct {
		name string
		req  DistanceMatrixRequest
		want CachePolicy
	}{
		{
			name: "static request",
			req: DistanceMatrixRequest{
				Origins:      []string{"o1"},
				Destinations: []string{"d1"},
			},
			want: CachePolicyStatic,
		},
		{
			name: "departure time makes traffic policy",
			req: DistanceMatrixRequest{
				Origins:       []string{"o1"},
				Destinations:  []string{"d1"},
				DepartureTime: 1713574800,
			},
			want: CachePolicyTraffic,
		},
		{
			name: "departure time now makes traffic policy",
			req: DistanceMatrixRequest{
				Origins:          []string{"o1"},
				Destinations:     []string{"d1"},
				DepartureTimeNow: true,
			},
			want: CachePolicyTraffic,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := ResolveCachePolicy(tt.req); got != tt.want {
				t.Fatalf("expected %s, got %s", tt.want, got)
			}
		})
	}
}

func TestBuildMatrixCacheKeyStaticIgnoresAvoidOrder(t *testing.T) {
	cfg := DefaultMatrixCacheConfig()
	now := time.Date(2026, 4, 20, 8, 0, 0, 0, time.UTC)

	reqA := DistanceMatrixRequest{
		Origins:      []string{"o1"},
		Destinations: []string{"d1"},
		Avoid:        []string{"tolls", "ferries"},
	}
	reqB := DistanceMatrixRequest{
		Origins:      []string{"o1"},
		Destinations: []string{"d1"},
		Avoid:        []string{"ferries", "tolls"},
	}

	keyA, err := BuildMatrixCacheKey(reqA, now, cfg)
	if err != nil {
		t.Fatalf("BuildMatrixCacheKey(reqA): %v", err)
	}
	keyB, err := BuildMatrixCacheKey(reqB, now, cfg)
	if err != nil {
		t.Fatalf("BuildMatrixCacheKey(reqB): %v", err)
	}

	if keyA.Key != keyB.Key {
		t.Fatalf("expected identical static keys, got %q vs %q", keyA.Key, keyB.Key)
	}
}

func TestBuildMatrixCacheKeyTrafficIncludesDateAndSlot(t *testing.T) {
	cfg := DefaultMatrixCacheConfig()
	now := time.Date(2026, 4, 20, 7, 30, 0, 0, time.UTC)

	key, err := BuildMatrixCacheKey(DistanceMatrixRequest{
		Origins:          []string{"o1"},
		Destinations:     []string{"d1"},
		DepartureTimeNow: true,
		TrafficModel:     "best_guess",
	}, now, cfg)
	if err != nil {
		t.Fatalf("BuildMatrixCacheKey: %v", err)
	}

	if key.Policy != CachePolicyTraffic {
		t.Fatalf("expected traffic policy, got %s", key.Policy)
	}
	if key.Date != "2026-04-20" {
		t.Fatalf("expected date 2026-04-20, got %s", key.Date)
	}
	if key.Slot != "morning" {
		t.Fatalf("expected slot morning, got %s", key.Slot)
	}
	if key.Namespace != defaultTrafficCacheNamespace {
		t.Fatalf("expected traffic namespace %q, got %q", defaultTrafficCacheNamespace, key.Namespace)
	}
}

func TestMatrixCacheTTLByMode(t *testing.T) {
	cfg := DefaultMatrixCacheConfig()
	now := time.Date(2026, 4, 20, 12, 0, 0, 0, time.UTC)

	if got := MatrixCacheTTL(DistanceMatrixRequest{
		Origins:      []string{"o1"},
		Destinations: []string{"d1"},
		Mode:         "walking",
	}, now, cfg); got != 7*24*time.Hour {
		t.Fatalf("expected walking ttl 7d, got %s", got)
	}

	if got := MatrixCacheTTL(DistanceMatrixRequest{
		Origins:      []string{"o1"},
		Destinations: []string{"d1"},
		Mode:         "transit",
	}, now, cfg); got != 6*time.Hour {
		t.Fatalf("expected transit ttl 6h, got %s", got)
	}
}

func TestMatrixCacheTTLTrafficUsesSlot(tt *testing.T) {
	cfg := DefaultMatrixCacheConfig()
	now := time.Date(2026, 4, 20, 17, 0, 0, 0, time.UTC)

	got := MatrixCacheTTL(DistanceMatrixRequest{
		Origins:          []string{"o1"},
		Destinations:     []string{"d1"},
		DepartureTimeNow: true,
	}, now, cfg)
	if got != time.Hour {
		tt.Fatalf("expected evening ttl 1h, got %s", got)
	}
}

func TestMatrixTrafficSlot(t *testing.T) {
	cases := []struct {
		hour int
		want string
	}{
		{7, "morning"},
		{10, "midday"},
		{18, "evening"},
		{2, "night"},
	}

	for _, tc := range cases {
		got := MatrixTrafficSlot(time.Date(2026, 4, 20, tc.hour, 0, 0, 0, time.UTC))
		if got != tc.want {
			t.Fatalf("hour %d: expected %s, got %s", tc.hour, tc.want, got)
		}
	}
}

func TestDevMatrixCacheConfigUsesMonthTTL(t *testing.T) {
	cfg := DevMatrixCacheConfig()
	monthTTL := 30 * 24 * time.Hour

	if !cfg.Enabled {
		t.Fatal("expected dev cache config to enable cache")
	}
	if cfg.StaticTTL != monthTTL {
		t.Fatalf("expected static ttl %s, got %s", monthTTL, cfg.StaticTTL)
	}
	if cfg.WalkingTTL != monthTTL {
		t.Fatalf("expected walking ttl %s, got %s", monthTTL, cfg.WalkingTTL)
	}
	if cfg.BicyclingTTL != monthTTL {
		t.Fatalf("expected bicycling ttl %s, got %s", monthTTL, cfg.BicyclingTTL)
	}
	if cfg.TransitTTL != monthTTL {
		t.Fatalf("expected transit ttl %s, got %s", monthTTL, cfg.TransitTTL)
	}

	for _, slot := range []string{"morning", "midday", "evening", "night"} {
		if got := cfg.TrafficTTLs[slot]; got != monthTTL {
			t.Fatalf("expected traffic ttl for %s to be %s, got %s", slot, monthTTL, got)
		}
	}
}
