package test

import (
	"testing"
	"time"

	gmap "github.com/ROP-TEAM/rop-algorithm/gmap"
)

func TestResolveCachePolicy(t *testing.T) {
	tests := []struct {
		name string
		req  gmap.DistanceMatrixRequest
		want gmap.CachePolicy
	}{
		{
			name: "static request",
			req: gmap.DistanceMatrixRequest{
				Origins:      []string{"o1"},
				Destinations: []string{"d1"},
			},
			want: gmap.CachePolicyStatic,
		},
		{
			name: "departure time makes traffic policy",
			req: gmap.DistanceMatrixRequest{
				Origins:       []string{"o1"},
				Destinations:  []string{"d1"},
				DepartureTime: 1713574800,
			},
			want: gmap.CachePolicyTraffic,
		},
		{
			name: "departure time now makes traffic policy",
			req: gmap.DistanceMatrixRequest{
				Origins:          []string{"o1"},
				Destinations:     []string{"d1"},
				DepartureTimeNow: true,
			},
			want: gmap.CachePolicyTraffic,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := gmap.ResolveCachePolicy(tt.req); got != tt.want {
				t.Fatalf("expected %s, got %s", tt.want, got)
			}
		})
	}
}

func TestBuildMatrixCacheKeyStaticIgnoresAvoidOrder(t *testing.T) {
	cfg := gmap.DefaultMatrixCacheConfig()
	now := time.Date(2026, 4, 20, 8, 0, 0, 0, time.UTC)

	reqA := gmap.DistanceMatrixRequest{
		Origins:      []string{"o1"},
		Destinations: []string{"d1"},
		Avoid:        []string{"tolls", "ferries"},
	}
	reqB := gmap.DistanceMatrixRequest{
		Origins:      []string{"o1"},
		Destinations: []string{"d1"},
		Avoid:        []string{"ferries", "tolls"},
	}

	keyA, err := gmap.BuildMatrixCacheKey(reqA, now, cfg)
	if err != nil {
		t.Fatalf("BuildMatrixCacheKey(reqA): %v", err)
	}
	keyB, err := gmap.BuildMatrixCacheKey(reqB, now, cfg)
	if err != nil {
		t.Fatalf("BuildMatrixCacheKey(reqB): %v", err)
	}

	if keyA.Key != keyB.Key {
		t.Fatalf("expected identical static keys, got %q vs %q", keyA.Key, keyB.Key)
	}
}

func TestBuildMatrixCacheKeyTrafficIncludesDateAndSlot(t *testing.T) {
	cfg := gmap.DefaultMatrixCacheConfig()
	now := time.Date(2026, 4, 20, 7, 30, 0, 0, time.UTC)

	key, err := gmap.BuildMatrixCacheKey(gmap.DistanceMatrixRequest{
		Origins:          []string{"o1"},
		Destinations:     []string{"d1"},
		DepartureTimeNow: true,
		TrafficModel:     "best_guess",
	}, now, cfg)
	if err != nil {
		t.Fatalf("BuildMatrixCacheKey: %v", err)
	}

	if key.Policy != gmap.CachePolicyTraffic {
		t.Fatalf("expected traffic policy, got %s", key.Policy)
	}
	if key.Date != "2026-04-20" {
		t.Fatalf("expected date 2026-04-20, got %s", key.Date)
	}
	if key.Slot != "morning" {
		t.Fatalf("expected slot morning, got %s", key.Slot)
	}
}

func TestMatrixCacheTTLByMode(t *testing.T) {
	cfg := gmap.DefaultMatrixCacheConfig()
	now := time.Date(2026, 4, 20, 12, 0, 0, 0, time.UTC)

	if got := gmap.MatrixCacheTTL(gmap.DistanceMatrixRequest{
		Origins:      []string{"o1"},
		Destinations: []string{"d1"},
		Mode:         "walking",
	}, now, cfg); got != 7*24*time.Hour {
		t.Fatalf("expected walking ttl 7d, got %s", got)
	}

	if got := gmap.MatrixCacheTTL(gmap.DistanceMatrixRequest{
		Origins:      []string{"o1"},
		Destinations: []string{"d1"},
		Mode:         "transit",
	}, now, cfg); got != 6*time.Hour {
		t.Fatalf("expected transit ttl 6h, got %s", got)
	}
}

func TestMatrixCacheTTLTrafficUsesSlot(t *testing.T) {
	cfg := gmap.DefaultMatrixCacheConfig()
	now := time.Date(2026, 4, 20, 17, 0, 0, 0, time.UTC)

	got := gmap.MatrixCacheTTL(gmap.DistanceMatrixRequest{
		Origins:          []string{"o1"},
		Destinations:     []string{"d1"},
		DepartureTimeNow: true,
	}, now, cfg)
	if got != time.Hour {
		t.Fatalf("expected evening ttl 1h, got %s", got)
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
		got := gmap.MatrixTrafficSlot(time.Date(2026, 4, 20, tc.hour, 0, 0, 0, time.UTC))
		if got != tc.want {
			t.Fatalf("hour %d: expected %s, got %s", tc.hour, tc.want, got)
		}
	}
}

func TestDevMatrixCacheConfigUsesMonthTTL(t *testing.T) {
	cfg := gmap.DevMatrixCacheConfig()
	monthTTL := 30 * 24 * time.Hour

	if !cfg.Enabled {
		t.Fatal("expected dev cache config to enable cache")
	}
	if cfg.StaticTTL != monthTTL || cfg.WalkingTTL != monthTTL || cfg.BicyclingTTL != monthTTL || cfg.TransitTTL != monthTTL {
		t.Fatalf("expected all static ttls to be %s, got %#v", monthTTL, cfg)
	}

	for _, slot := range []string{"morning", "midday", "evening", "night"} {
		if got := cfg.TrafficTTLs[slot]; got != monthTTL {
			t.Fatalf("expected traffic ttl for %s to be %s, got %s", slot, monthTTL, got)
		}
	}
}
