package graph

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"net/url"
	"os"
	"strings"
	"testing"
	"time"
)

// TestGoogleMapsMatrix calls the real Distance Matrix API through the compatibility wrapper.
// Run with: go test ./graph/... -v -run TestGoogleMapsMatrix
func TestGoogleMapsMatrix(t *testing.T) {
	apiKey := os.Getenv("GOOGLE_MAPS_API_KEY")
	if apiKey == "" {
		t.Skip("GOOGLE_MAPS_API_KEY not set")
	}

	m, err := NewGoogleMapsMatrix(apiKey)
	if err != nil {
		t.Fatalf("NewGoogleMapsMatrix: %v", err)
	}

	locs := []Location{
		{Lat: 13.7563, Lng: 100.5018},
		{Lat: 13.7469, Lng: 100.5346},
		{Lat: 13.7308, Lng: 100.5418},
	}

	req := buildDistanceMatrixRequest(locs, MatrixOptions{})
	result, err := m.ExecuteMatrix(context.Background(), req)
	if err != nil {
		t.Fatalf("ExecuteMatrix: %v", err)
	}

	pretty, err := json.MarshalIndent(result.Response, "", "  ")
	if err != nil {
		t.Fatalf("MarshalIndent: %v", err)
	}
	t.Logf("Full Distance Matrix response:\n%s", pretty)

	for i, row := range result.Response.Rows {
		for j, el := range row.Elements {
			t.Logf(
				"raw [%d→%d] status=%s distance=%s duration=%s duration_in_traffic=%s fare=%s",
				i,
				j,
				el.Status,
				formatValueText(el.Distance),
				formatValueText(el.Duration),
				formatValueText(el.DurationInTraffic),
				formatTransitFare(el.Fare),
			)
		}
	}

	durations, distances, err := m.BuildMatrix(context.Background(), locs, MatrixOptions{})
	if err != nil {
		t.Fatalf("BuildMatrix: %v", err)
	}

	n := len(locs)
	t.Logf("Matrix %d×%d", n, n)
	for i := range locs {
		for j := range locs {
			t.Logf("  [%d→%d] duration=%d min  distance=%d m", i, j, durations[i][j], distances[i][j])
		}
	}

	for i := range locs {
		if durations[i][i] != 0 || distances[i][i] != 0 {
			t.Errorf("diagonal [%d][%d] should be 0", i, i)
		}
	}
}

// TestGoogleMapsMatrixBatch tests automatic batching when locations > 25.
func TestGoogleMapsMatrixBatch(t *testing.T) {
	apiKey := os.Getenv("GOOGLE_MAPS_API_KEY")
	if apiKey == "" {
		t.Skip("GOOGLE_MAPS_API_KEY not set")
	}

	m, err := NewGoogleMapsMatrix(apiKey)
	if err != nil {
		t.Fatalf("NewGoogleMapsMatrix: %v", err)
	}

	locs := make([]Location, 30)
	for i := range locs {
		locs[i] = Location{
			Lat: 13.70 + float64(i)*0.005,
			Lng: 100.50 + float64(i)*0.003,
		}
	}

	durations, distances, err := m.BuildMatrix(context.Background(), locs, MatrixOptions{})
	if err != nil {
		t.Fatalf("BuildMatrix: %v", err)
	}

	if len(durations) != 30 || len(durations[0]) != 30 {
		t.Errorf("expected 30×30 matrix, got %d×%d", len(durations), len(durations[0]))
	}
	if len(distances) != 30 || len(distances[0]) != 30 {
		t.Errorf("expected 30×30 matrix, got %d×%d", len(distances), len(distances[0]))
	}
}

func TestBuildDistanceMatrixQueryIncludesOptions(t *testing.T) {
	req := DistanceMatrixRequest{
		Origins:                  []string{"place_id:origin"},
		Destinations:             []string{"heading=90:13.756300,100.501800"},
		Mode:                     "transit",
		Units:                    "imperial",
		Language:                 "th",
		Region:                   "th",
		Avoid:                    []string{"tolls", "ferries"},
		DepartureTime:            1713574800,
		TrafficModel:             "best_guess",
		TransitMode:              []string{"train", "subway"},
		TransitRoutingPreference: "less_walking",
	}

	query := buildDistanceMatrixQuery(req, "secret")

	assertQueryValue(t, query, "origins", "place_id:origin")
	assertQueryValue(t, query, "destinations", "heading=90:13.756300,100.501800")
	assertQueryValue(t, query, "mode", "transit")
	assertQueryValue(t, query, "units", "imperial")
	assertQueryValue(t, query, "language", "th")
	assertQueryValue(t, query, "region", "th")
	assertQueryValue(t, query, "avoid", "tolls|ferries")
	assertQueryValue(t, query, "departure_time", "1713574800")
	assertQueryValue(t, query, "traffic_model", "best_guess")
	assertQueryValue(t, query, "transit_mode", "train|subway")
	assertQueryValue(t, query, "transit_routing_preference", "less_walking")
	assertQueryValue(t, query, "key", "secret")
}

func TestBuildDistanceMatrixQueryDepartureTimeNow(t *testing.T) {
	query := buildDistanceMatrixQuery(DistanceMatrixRequest{
		Origins:          []string{"13.1,100.1"},
		Destinations:     []string{"13.2,100.2"},
		DepartureTimeNow: true,
	}, "secret")

	assertQueryValue(t, query, "departure_time", "now")
}

func TestValidateDistanceMatrixRequestRejectsConflictingTimes(t *testing.T) {
	err := validateDistanceMatrixRequest(DistanceMatrixRequest{
		Origins:       []string{"a"},
		Destinations:  []string{"b"},
		DepartureTime: 1,
		ArrivalTime:   2,
	})
	if err == nil {
		t.Fatal("expected validation error")
	}
}

func TestExecuteMatrixRectangularBatching(t *testing.T) {
	var calls []url.Values
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		calls = append(calls, r.URL.Query())

		origins := strings.Split(r.URL.Query().Get("origins"), "|")
		destinations := strings.Split(r.URL.Query().Get("destinations"), "|")
		resp := DistanceMatrixResponse{
			Status:               "OK",
			OriginAddresses:      append([]string(nil), origins...),
			DestinationAddresses: append([]string(nil), destinations...),
			Rows:                 make([]DistanceMatrixRow, len(origins)),
		}

		for i := range origins {
			resp.Rows[i] = DistanceMatrixRow{Elements: make([]DistanceMatrixElement, len(destinations))}
			for j := range destinations {
				resp.Rows[i].Elements[j] = DistanceMatrixElement{
					Status: "OK",
					Distance: &ValueText{
						Value: (i + 1) * 1000 + (j + 1),
						Text:  fmt.Sprintf("%d m", (i+1)*1000+(j+1)),
					},
					Duration: &ValueText{
						Value: (i + 1) * 600 + (j * 60),
						Text:  fmt.Sprintf("%d mins", (i+1)*10+j),
					},
				}
			}
		}

		_ = json.NewEncoder(w).Encode(resp)
	}))
	defer server.Close()

	m := newTestMatrix(server.URL)
	req := DistanceMatrixRequest{
		Origins:      makeLocations("o", 11),
		Destinations: makeLocations("d", 3),
	}

	result, err := m.ExecuteMatrix(context.Background(), req)
	if err != nil {
		t.Fatalf("ExecuteMatrix: %v", err)
	}

	if len(calls) != 2 {
		t.Fatalf("expected 2 batched requests, got %d", len(calls))
	}
	if got := len(result.Durations); got != 11 {
		t.Fatalf("expected 11 origin rows, got %d", got)
	}
	if got := len(result.Durations[0]); got != 3 {
		t.Fatalf("expected 3 destinations, got %d", got)
	}
	if result.Durations[10][2] != 12 {
		t.Fatalf("expected merged duration 12, got %d", result.Durations[10][2])
	}
	if result.Distances[10][2] != 1003 {
		t.Fatalf("expected merged distance 1003, got %d", result.Distances[10][2])
	}
}

func TestExecuteMatrixUsesDurationInTrafficWhenPresent(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		resp := DistanceMatrixResponse{
			Status:               "OK",
			OriginAddresses:      []string{"o1"},
			DestinationAddresses: []string{"d1"},
			Rows: []DistanceMatrixRow{
				{
					Elements: []DistanceMatrixElement{
						{
							Status: "OK",
							Distance: &ValueText{
								Value: 1500,
								Text:  "1.5 km",
							},
							Duration: &ValueText{
								Value: 600,
								Text:  "10 mins",
							},
							DurationInTraffic: &ValueText{
								Value: 900,
								Text:  "15 mins",
							},
						},
					},
				},
			},
		}
		_ = json.NewEncoder(w).Encode(resp)
	}))
	defer server.Close()

	m := newTestMatrix(server.URL)
	result, err := m.ExecuteMatrix(context.Background(), DistanceMatrixRequest{
		Origins:          []string{"o1"},
		Destinations:     []string{"d1"},
		DepartureTimeNow: true,
	})
	if err != nil {
		t.Fatalf("ExecuteMatrix: %v", err)
	}

	if result.Durations[0][0] != 15 {
		t.Fatalf("expected duration_in_traffic to win, got %d", result.Durations[0][0])
	}
}

func TestExecuteMatrixReturnsTopLevelAPIError(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_ = json.NewEncoder(w).Encode(DistanceMatrixResponse{
			Status:       "REQUEST_DENIED",
			ErrorMessage: "bad key",
		})
	}))
	defer server.Close()

	m := newTestMatrix(server.URL)
	_, err := m.ExecuteMatrix(context.Background(), DistanceMatrixRequest{
		Origins:      []string{"o1"},
		Destinations: []string{"d1"},
	})
	if err == nil || !strings.Contains(err.Error(), "bad key") {
		t.Fatalf("expected top-level api error, got %v", err)
	}
}

func TestExecuteMatrixReturnsElementStatusError(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_ = json.NewEncoder(w).Encode(DistanceMatrixResponse{
			Status:               "OK",
			OriginAddresses:      []string{"o1"},
			DestinationAddresses: []string{"d1"},
			Rows: []DistanceMatrixRow{
				{
					Elements: []DistanceMatrixElement{
						{Status: "ZERO_RESULTS"},
					},
				},
			},
		})
	}))
	defer server.Close()

	m := newTestMatrix(server.URL)
	_, err := m.ExecuteMatrix(context.Background(), DistanceMatrixRequest{
		Origins:      []string{"o1"},
		Destinations: []string{"d1"},
	})
	if err == nil || !strings.Contains(err.Error(), "ZERO_RESULTS") {
		t.Fatalf("expected element status error, got %v", err)
	}
}

func TestExecuteMatrixSupportsRegionAndLanguage(t *testing.T) {
	var gotQuery url.Values
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		gotQuery = r.URL.Query()
		_ = json.NewEncoder(w).Encode(DistanceMatrixResponse{
			Status:               "OK",
			OriginAddresses:      []string{"Bangkok"},
			DestinationAddresses: []string{"Asok"},
			Rows: []DistanceMatrixRow{
				{
					Elements: []DistanceMatrixElement{
						{
							Status: "OK",
							Distance: &ValueText{
								Value: 1000,
								Text:  "1 km",
							},
							Duration: &ValueText{
								Value: 300,
								Text:  "5 mins",
							},
						},
					},
				},
			},
		})
	}))
	defer server.Close()

	m := newTestMatrix(server.URL)
	_, err := m.ExecuteMatrix(context.Background(), DistanceMatrixRequest{
		Origins:      []string{"Bangkok"},
		Destinations: []string{"Asok"},
		Language:     "th",
		Region:       "th",
	})
	if err != nil {
		t.Fatalf("ExecuteMatrix: %v", err)
	}

	assertQueryValue(t, gotQuery, "language", "th")
	assertQueryValue(t, gotQuery, "region", "th")
}

func TestExecuteMatrixSupportsRawLocationStrings(t *testing.T) {
	var gotQuery url.Values
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		gotQuery = r.URL.Query()
		_ = json.NewEncoder(w).Encode(DistanceMatrixResponse{
			Status:               "OK",
			OriginAddresses:      []string{"origin"},
			DestinationAddresses: []string{"destination"},
			Rows: []DistanceMatrixRow{
				{
					Elements: []DistanceMatrixElement{
						{
							Status: "OK",
							Distance: &ValueText{Value: 100, Text: "100 m"},
							Duration: &ValueText{Value: 60, Text: "1 min"},
						},
					},
				},
			},
		})
	}))
	defer server.Close()

	m := newTestMatrix(server.URL)
	_, err := m.ExecuteMatrix(context.Background(), DistanceMatrixRequest{
		Origins:      []string{"side_of_road:13.756300,100.501800"},
		Destinations: []string{"heading=90:13.746900,100.534600"},
	})
	if err != nil {
		t.Fatalf("ExecuteMatrix: %v", err)
	}

	assertQueryValue(t, gotQuery, "origins", "side_of_road:13.756300,100.501800")
	assertQueryValue(t, gotQuery, "destinations", "heading=90:13.746900,100.534600")
}

func TestExecuteMatrixUsesStaticCache(t *testing.T) {
	var calls int
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		calls++
		_ = json.NewEncoder(w).Encode(DistanceMatrixResponse{
			Status:               "OK",
			OriginAddresses:      []string{"o1"},
			DestinationAddresses: []string{"d1"},
			Rows: []DistanceMatrixRow{
				{
					Elements: []DistanceMatrixElement{
						{
							Status:   "OK",
							Distance: &ValueText{Value: 1200, Text: "1.2 km"},
							Duration: &ValueText{Value: 600, Text: "10 mins"},
						},
					},
				},
			},
		})
	}))
	defer server.Close()

	m := newTestMatrix(server.URL)
	cfg := DefaultMatrixCacheConfig()
	cfg.Enabled = true
	m.EnableInMemoryCache(cfg)
	m.baseURL = server.URL
	m.now = func() time.Time {
		return time.Date(2026, 4, 20, 9, 0, 0, 0, time.UTC)
	}

	req := DistanceMatrixRequest{
		Origins:      []string{"o1"},
		Destinations: []string{"d1"},
	}

	first, err := m.ExecuteMatrix(context.Background(), req)
	if err != nil {
		t.Fatalf("first ExecuteMatrix: %v", err)
	}
	second, err := m.ExecuteMatrix(context.Background(), req)
	if err != nil {
		t.Fatalf("second ExecuteMatrix: %v", err)
	}

	if calls != 1 {
		t.Fatalf("expected 1 upstream call with cache hit, got %d", calls)
	}
	if first.Durations[0][0] != second.Durations[0][0] {
		t.Fatalf("expected cached result to match first call")
	}
}

func TestExecuteMatrixSkipsTrafficCacheWhenDisabled(t *testing.T) {
	var calls int
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		calls++
		_ = json.NewEncoder(w).Encode(DistanceMatrixResponse{
			Status:               "OK",
			OriginAddresses:      []string{"o1"},
			DestinationAddresses: []string{"d1"},
			Rows: []DistanceMatrixRow{
				{
					Elements: []DistanceMatrixElement{
						{
							Status:            "OK",
							Distance:          &ValueText{Value: 1200, Text: "1.2 km"},
							Duration:          &ValueText{Value: 600, Text: "10 mins"},
							DurationInTraffic: &ValueText{Value: 900, Text: "15 mins"},
						},
					},
				},
			},
		})
	}))
	defer server.Close()

	m := newTestMatrix(server.URL)
	cfg := DefaultMatrixCacheConfig()
	cfg.Enabled = true
	cfg.TrafficEnabled = false
	m.EnableInMemoryCache(cfg)
	m.baseURL = server.URL
	m.now = func() time.Time {
		return time.Date(2026, 4, 20, 17, 0, 0, 0, time.UTC)
	}

	req := DistanceMatrixRequest{
		Origins:          []string{"o1"},
		Destinations:     []string{"d1"},
		DepartureTimeNow: true,
	}

	if _, err := m.ExecuteMatrix(context.Background(), req); err != nil {
		t.Fatalf("first ExecuteMatrix: %v", err)
	}
	if _, err := m.ExecuteMatrix(context.Background(), req); err != nil {
		t.Fatalf("second ExecuteMatrix: %v", err)
	}

	if calls != 2 {
		t.Fatalf("expected 2 upstream calls when traffic cache is disabled, got %d", calls)
	}
}

func TestExecuteMatrixEmitsCacheEventsAndMetrics(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_ = json.NewEncoder(w).Encode(DistanceMatrixResponse{
			Status:               "OK",
			OriginAddresses:      []string{"o1"},
			DestinationAddresses: []string{"d1"},
			Rows: []DistanceMatrixRow{
				{
					Elements: []DistanceMatrixElement{
						{
							Status:   "OK",
							Distance: &ValueText{Value: 1200, Text: "1.2 km"},
							Duration: &ValueText{Value: 600, Text: "10 mins"},
						},
					},
				},
			},
		})
	}))
	defer server.Close()

	m := newTestMatrix(server.URL)
	cfg := DefaultMatrixCacheConfig()
	cfg.Enabled = true
	m.EnableInMemoryCache(cfg)
	m.baseURL = server.URL
	m.now = func() time.Time {
		return time.Date(2026, 4, 20, 9, 0, 0, 0, time.UTC)
	}

	var events []MatrixEvent
	m.SetEventHook(func(ctx context.Context, event MatrixEvent) {
		events = append(events, event)
	})
	metrics := NewMatrixMetrics()
	m.SetMetricsCollector(metrics)

	req := DistanceMatrixRequest{
		Origins:      []string{"o1"},
		Destinations: []string{"d1"},
	}

	if _, err := m.ExecuteMatrix(context.Background(), req); err != nil {
		t.Fatalf("first ExecuteMatrix: %v", err)
	}
	if _, err := m.ExecuteMatrix(context.Background(), req); err != nil {
		t.Fatalf("second ExecuteMatrix: %v", err)
	}

	if !containsEvent(events, "cache_miss") {
		t.Fatalf("expected cache_miss event, got %#v", events)
	}
	if !containsEvent(events, "cache_store") {
		t.Fatalf("expected cache_store event, got %#v", events)
	}
	if !containsEvent(events, "cache_hit") {
		t.Fatalf("expected cache_hit event, got %#v", events)
	}

	snapshot := metrics.Snapshot()
	if snapshot["cache_miss|policy=static"] != 1 {
		t.Fatalf("expected 1 cache_miss metric, got %#v", snapshot)
	}
	if snapshot["cache_store|policy=static|reason=24h0m0s"] != 1 {
		t.Fatalf("expected 1 cache_store metric, got %#v", snapshot)
	}
	if snapshot["cache_hit|policy=static"] != 1 {
		t.Fatalf("expected 1 cache_hit metric, got %#v", snapshot)
	}
}

func newTestMatrix(baseURL string) *GoogleMapsMatrix {
	return &GoogleMapsMatrix{
		apiKey:      "test-key",
		httpClient:  http.DefaultClient,
		baseURL:     baseURL,
		cacheConfig: DefaultMatrixCacheConfig(),
		now:         time.Now,
	}
}

func makeLocations(prefix string, n int) []string {
	out := make([]string, n)
	for i := range out {
		out[i] = fmt.Sprintf("%s%d", prefix, i)
	}
	return out
}

func containsEvent(events []MatrixEvent, name string) bool {
	for _, event := range events {
		if event.Name == name {
			return true
		}
	}
	return false
}

func assertQueryValue(t *testing.T, values url.Values, key, want string) {
	t.Helper()
	if got := values.Get(key); got != want {
		t.Fatalf("expected query %s=%q, got %q", key, want, got)
	}
}

func formatValueText(v *ValueText) string {
	if v == nil {
		return "-"
	}
	return fmt.Sprintf("%s (%d)", v.Text, v.Value)
}

func formatTransitFare(f *TransitFare) string {
	if f == nil {
		return "-"
	}
	if f.Text != "" {
		return fmt.Sprintf("%s [%s %.2f]", f.Text, f.Currency, f.Value)
	}
	return fmt.Sprintf("%s %.2f", f.Currency, f.Value)
}
