package test

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

	graph "github.com/ROP-TEAM/rop-algorithm/google_map_API"
)

func TestGoogleMapsMatrix(t *testing.T) {
	apiKey := os.Getenv("GOOGLE_MAPS_API_KEY")
	if apiKey == "" {
		t.Skip("GOOGLE_MAPS_API_KEY not set")
	}

	m, err := graph.NewGoogleMapsMatrix(apiKey)
	if err != nil {
		t.Fatalf("NewGoogleMapsMatrix: %v", err)
	}

	req := graph.DistanceMatrixRequest{
		Origins:      []string{"13.756300,100.501800", "13.746900,100.534600", "13.730800,100.541800"},
		Destinations: []string{"13.756300,100.501800", "13.746900,100.534600", "13.730800,100.541800"},
	}

	result, err := m.ExecuteMatrix(context.Background(), req)
	if err != nil {
		t.Fatalf("ExecuteMatrix: %v", err)
	}

	pretty, err := json.MarshalIndent(result.Response, "", "  ")
	if err != nil {
		t.Fatalf("MarshalIndent: %v", err)
	}
	t.Logf("Full Distance Matrix response:\n%s", pretty)
}

func TestGoogleMapsMatrixBatch(t *testing.T) {
	apiKey := os.Getenv("GOOGLE_MAPS_API_KEY")
	if apiKey == "" {
		t.Skip("GOOGLE_MAPS_API_KEY not set")
	}

	m, err := graph.NewGoogleMapsMatrix(apiKey)
	if err != nil {
		t.Fatalf("NewGoogleMapsMatrix: %v", err)
	}

	locs := make([]graph.Location, 30)
	for i := range locs {
		locs[i] = graph.Location{
			Lat: 13.70 + float64(i)*0.005,
			Lng: 100.50 + float64(i)*0.003,
		}
	}

	durations, distances, err := m.BuildMatrix(context.Background(), locs, graph.MatrixOptions{})
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
	req := graph.DistanceMatrixRequest{
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

	query := graph.BuildDistanceMatrixQuery(req, "secret")

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
	query := graph.BuildDistanceMatrixQuery(graph.DistanceMatrixRequest{
		Origins:          []string{"13.1,100.1"},
		Destinations:     []string{"13.2,100.2"},
		DepartureTimeNow: true,
	}, "secret")

	assertQueryValue(t, query, "departure_time", "now")
}

func TestValidateDistanceMatrixRequestRejectsConflictingTimes(t *testing.T) {
	err := graph.ValidateDistanceMatrixRequest(graph.DistanceMatrixRequest{
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
		resp := graph.DistanceMatrixResponse{
			Status:               "OK",
			OriginAddresses:      append([]string(nil), origins...),
			DestinationAddresses: append([]string(nil), destinations...),
			Rows:                 make([]graph.DistanceMatrixRow, len(origins)),
		}

		for i := range origins {
			resp.Rows[i] = graph.DistanceMatrixRow{Elements: make([]graph.DistanceMatrixElement, len(destinations))}
			for j := range destinations {
				resp.Rows[i].Elements[j] = graph.DistanceMatrixElement{
					Status: "OK",
					Distance: &graph.ValueText{
						Value: (i+1)*1000 + (j + 1),
						Text:  fmt.Sprintf("%d m", (i+1)*1000+(j+1)),
					},
					Duration: &graph.ValueText{
						Value: (i+1)*600 + (j * 60),
						Text:  fmt.Sprintf("%d mins", (i+1)*10+j),
					},
				}
			}
		}

		_ = json.NewEncoder(w).Encode(resp)
	}))
	defer server.Close()

	m, err := graph.NewGoogleMapsMatrix("test-key", graph.WithBaseURL(server.URL), graph.WithClock(func() time.Time {
		return time.Date(2026, 4, 20, 9, 0, 0, 0, time.UTC)
	}))
	if err != nil {
		t.Fatalf("NewGoogleMapsMatrix: %v", err)
	}

	req := graph.DistanceMatrixRequest{
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
	if result.Durations[10][2] != 12 || result.Distances[10][2] != 1003 {
		t.Fatalf("unexpected merged result: durations=%v distances=%v", result.Durations[10][2], result.Distances[10][2])
	}
}

func TestExecuteMatrixUsesDurationInTrafficWhenPresent(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_ = json.NewEncoder(w).Encode(graph.DistanceMatrixResponse{
			Status:               "OK",
			OriginAddresses:      []string{"o1"},
			DestinationAddresses: []string{"d1"},
			Rows: []graph.DistanceMatrixRow{{
				Elements: []graph.DistanceMatrixElement{{
					Status:            "OK",
					Distance:          &graph.ValueText{Value: 1500, Text: "1.5 km"},
					Duration:          &graph.ValueText{Value: 600, Text: "10 mins"},
					DurationInTraffic: &graph.ValueText{Value: 900, Text: "15 mins"},
				}},
			}},
		})
	}))
	defer server.Close()

	m, _ := graph.NewGoogleMapsMatrix("test-key", graph.WithBaseURL(server.URL))
	result, err := m.ExecuteMatrix(context.Background(), graph.DistanceMatrixRequest{
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
		_ = json.NewEncoder(w).Encode(graph.DistanceMatrixResponse{
			Status:       "REQUEST_DENIED",
			ErrorMessage: "bad key",
		})
	}))
	defer server.Close()

	m, _ := graph.NewGoogleMapsMatrix("test-key", graph.WithBaseURL(server.URL))
	_, err := m.ExecuteMatrix(context.Background(), graph.DistanceMatrixRequest{
		Origins:      []string{"o1"},
		Destinations: []string{"d1"},
	})
	if err == nil || !strings.Contains(err.Error(), "bad key") {
		t.Fatalf("expected top-level api error, got %v", err)
	}
}

func TestExecuteMatrixSupportsRegionAndLanguage(t *testing.T) {
	var gotQuery url.Values
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		gotQuery = r.URL.Query()
		_ = json.NewEncoder(w).Encode(graph.DistanceMatrixResponse{
			Status:               "OK",
			OriginAddresses:      []string{"Bangkok"},
			DestinationAddresses: []string{"Asok"},
			Rows: []graph.DistanceMatrixRow{{Elements: []graph.DistanceMatrixElement{{
				Status:   "OK",
				Distance: &graph.ValueText{Value: 1000, Text: "1 km"},
				Duration: &graph.ValueText{Value: 300, Text: "5 mins"},
			}}}},
		})
	}))
	defer server.Close()

	m, _ := graph.NewGoogleMapsMatrix("test-key", graph.WithBaseURL(server.URL))
	_, err := m.ExecuteMatrix(context.Background(), graph.DistanceMatrixRequest{
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

func TestExecuteMatrixUsesStaticCache(t *testing.T) {
	var calls int
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		calls++
		_ = json.NewEncoder(w).Encode(graph.DistanceMatrixResponse{
			Status:               "OK",
			OriginAddresses:      []string{"o1"},
			DestinationAddresses: []string{"d1"},
			Rows: []graph.DistanceMatrixRow{{Elements: []graph.DistanceMatrixElement{{
				Status:   "OK",
				Distance: &graph.ValueText{Value: 1200, Text: "1.2 km"},
				Duration: &graph.ValueText{Value: 600, Text: "10 mins"},
			}}}},
		})
	}))
	defer server.Close()

	m, _ := graph.NewGoogleMapsMatrix("test-key", graph.WithBaseURL(server.URL), graph.WithClock(func() time.Time {
		return time.Date(2026, 4, 20, 9, 0, 0, 0, time.UTC)
	}))
	cfg := graph.DefaultMatrixCacheConfig()
	cfg.Enabled = true
	m.EnableInMemoryCache(cfg)

	req := graph.DistanceMatrixRequest{Origins: []string{"o1"}, Destinations: []string{"d1"}}
	first, err := m.ExecuteMatrix(context.Background(), req)
	if err != nil {
		t.Fatalf("first ExecuteMatrix: %v", err)
	}
	second, err := m.ExecuteMatrix(context.Background(), req)
	if err != nil {
		t.Fatalf("second ExecuteMatrix: %v", err)
	}

	if calls != 1 || first.Durations[0][0] != second.Durations[0][0] {
		t.Fatalf("expected cached result with one upstream call, calls=%d", calls)
	}
}

func TestExecuteMatrixEmitsCacheEventsAndMetrics(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_ = json.NewEncoder(w).Encode(graph.DistanceMatrixResponse{
			Status:               "OK",
			OriginAddresses:      []string{"o1"},
			DestinationAddresses: []string{"d1"},
			Rows: []graph.DistanceMatrixRow{{Elements: []graph.DistanceMatrixElement{{
				Status:   "OK",
				Distance: &graph.ValueText{Value: 1200, Text: "1.2 km"},
				Duration: &graph.ValueText{Value: 600, Text: "10 mins"},
			}}}},
		})
	}))
	defer server.Close()

	m, _ := graph.NewGoogleMapsMatrix("test-key", graph.WithBaseURL(server.URL), graph.WithClock(func() time.Time {
		return time.Date(2026, 4, 20, 9, 0, 0, 0, time.UTC)
	}))
	cfg := graph.DefaultMatrixCacheConfig()
	cfg.Enabled = true
	m.EnableInMemoryCache(cfg)

	var events []graph.MatrixEvent
	m.SetEventHook(func(ctx context.Context, event graph.MatrixEvent) {
		events = append(events, event)
	})
	metrics := graph.NewMatrixMetrics()
	m.SetMetricsCollector(metrics)

	req := graph.DistanceMatrixRequest{Origins: []string{"o1"}, Destinations: []string{"d1"}}
	_, _ = m.ExecuteMatrix(context.Background(), req)
	_, _ = m.ExecuteMatrix(context.Background(), req)

	if !containsEvent(events, "cache_miss") || !containsEvent(events, "cache_store") || !containsEvent(events, "cache_hit") {
		t.Fatalf("unexpected events: %#v", events)
	}
	snapshot := metrics.Snapshot()
	if snapshot["cache_miss|policy=static"] != 1 || snapshot["cache_hit|policy=static"] != 1 {
		t.Fatalf("unexpected snapshot: %#v", snapshot)
	}
}

func makeLocations(prefix string, n int) []string {
	out := make([]string, n)
	for i := range out {
		out[i] = fmt.Sprintf("%s%d", prefix, i)
	}
	return out
}

func containsEvent(events []graph.MatrixEvent, name string) bool {
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
