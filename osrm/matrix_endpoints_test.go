package osrm

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/ROP-TEAM/rop-algorithm/model"
)

func TestNearest(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/nearest/v1/driving/100.501800,13.756300" {
			t.Errorf("unexpected path: %s", r.URL.Path)
		}
		if r.URL.Query().Get("number") != "1" {
			t.Errorf("expected number=1")
		}
		json.NewEncoder(w).Encode(OSRMNearestResponse{
			Code: "Ok",
			Waypoints: []OSRMWaypoint{{
				Name:     "Main Street",
				Location: [2]float64{100.501800, 13.756300},
				Distance: 5.2,
				Hint:     "abc123",
			}},
		})
	}))
	defer server.Close()

	m, err := NewOSRMMatrix(server.URL)
	if err != nil {
		t.Fatalf("NewOSRMMatrix: %v", err)
	}

	result, err := m.Nearest(context.Background(), model.NewLatLngLocation(13.756300, 100.501800))
	if err != nil {
		t.Fatalf("Nearest: %v", err)
	}
	if result.Name != "Main Street" {
		t.Errorf("expected Main Street, got %s", result.Name)
	}
	if result.Distance != 5.2 {
		t.Errorf("expected distance 5.2, got %f", result.Distance)
	}
	if result.Lng != 100.501800 || result.Lat != 13.756300 {
		t.Errorf("expected (13.756300, 100.501800), got (%f, %f)", result.Lat, result.Lng)
	}
}

func TestTrip(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/trip/v1/driving/100.501800,13.756300;100.534600,13.746900;100.541800,13.730800" {
			t.Errorf("unexpected path: %s", r.URL.Path)
		}
		if r.URL.Query().Get("roundtrip") != "false" {
			t.Errorf("expected roundtrip=false")
		}
		if r.URL.Query().Get("source") != "first" {
			t.Errorf("expected source=first")
		}
		json.NewEncoder(w).Encode(OSRMTripResponse{
			Code: "Ok",
			Waypoints: []OSRMTripWaypoint{
				{Name: "A", Location: [2]float64{100.501800, 13.756300}, TripsIndex: 0, WaypointIndex: 0, Hint: "h1"},
				{Name: "B", Location: [2]float64{100.534600, 13.746900}, TripsIndex: 0, WaypointIndex: 1, Hint: "h2"},
				{Name: "C", Location: [2]float64{100.541800, 13.730800}, TripsIndex: 0, WaypointIndex: 2, Hint: "h3"},
			},
			Trips: []OSRMTripRoute{{
				Distance: 8500.5,
				Duration: 1200.0,
				Geometry: "polyline_encoded_string",
			}},
		})
	}))
	defer server.Close()

	m, err := NewOSRMMatrix(server.URL)
	if err != nil {
		t.Fatalf("NewOSRMMatrix: %v", err)
	}

	locs := []model.Location{
		model.NewLatLngLocation(13.756300, 100.501800),
		model.NewLatLngLocation(13.746900, 100.534600),
		model.NewLatLngLocation(13.730800, 100.541800),
	}

	result, err := m.Trip(context.Background(), locs)
	if err != nil {
		t.Fatalf("Trip: %v", err)
	}
	if result.Distance != 8500.5 {
		t.Errorf("expected distance 8500.5, got %f", result.Distance)
	}
	if result.Duration != 1200.0 {
		t.Errorf("expected duration 1200.0, got %f", result.Duration)
	}
	if result.Geometry != "polyline_encoded_string" {
		t.Errorf("expected geometry, got %s", result.Geometry)
	}
	if len(result.Waypoints) != 3 {
		t.Fatalf("expected 3 waypoints, got %d", len(result.Waypoints))
	}
	if result.Waypoints[2].WaypointIndex != 2 {
		t.Errorf("expected waypoint_index=2, got %d", result.Waypoints[2].WaypointIndex)
	}
}

func TestRoute(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/route/v1/driving/100.501800,13.756300;100.534600,13.746900" {
			t.Errorf("unexpected path: %s", r.URL.Path)
		}
		if r.URL.Query().Get("geometries") != "polyline" {
			t.Errorf("expected geometries=polyline")
		}
		if r.URL.Query().Get("overview") != "full" {
			t.Errorf("expected overview=full")
		}
		json.NewEncoder(w).Encode(OSRMRouteResponse{
			Code: "Ok",
			Waypoints: []OSRMWaypoint{
				{Name: "Start", Location: [2]float64{100.501800, 13.756300}, Distance: 0, Hint: "h1"},
				{Name: "End", Location: [2]float64{100.534600, 13.746900}, Distance: 0, Hint: "h2"},
			},
			Routes: []OSRMRoute{{
				Distance: 5200.0,
				Duration: 600.0,
				Geometry: "encoded_polyline_here",
			}},
		})
	}))
	defer server.Close()

	m, err := NewOSRMMatrix(server.URL)
	if err != nil {
		t.Fatalf("NewOSRMMatrix: %v", err)
	}

	locs := []model.Location{
		model.NewLatLngLocation(13.756300, 100.501800),
		model.NewLatLngLocation(13.746900, 100.534600),
	}

	result, err := m.Route(context.Background(), locs)
	if err != nil {
		t.Fatalf("Route: %v", err)
	}
	if len(result.Waypoints) != 2 {
		t.Fatalf("expected 2 waypoints, got %d", len(result.Waypoints))
	}
	if result.Waypoints[0].Name != "Start" {
		t.Errorf("expected Start, got %s", result.Waypoints[0].Name)
	}
	if len(result.Routes) != 1 {
		t.Fatalf("expected 1 route, got %d", len(result.Routes))
	}
	if result.Routes[0].Distance != 5200.0 {
		t.Errorf("expected distance 5200, got %f", result.Routes[0].Distance)
	}
	if result.Routes[0].Duration != 600.0 {
		t.Errorf("expected duration 600, got %f", result.Routes[0].Duration)
	}
	if result.Routes[0].Geometry != "encoded_polyline_here" {
		t.Errorf("expected geometry, got %s", result.Routes[0].Geometry)
	}
}

func TestBuildRectangularMatrix(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/table/v1/driving/100.501800,13.756300;100.534600,13.746900;100.541800,13.730800" {
			t.Errorf("unexpected path: %s", r.URL.Path)
		}
		if r.URL.Query().Get("sources") != "0" {
			t.Errorf("expected sources=0, got %s", r.URL.Query().Get("sources"))
		}
		if r.URL.Query().Get("destinations") != "1;2" {
			t.Errorf("expected destinations=1;2, got %s", r.URL.Query().Get("destinations"))
		}

		json.NewEncoder(w).Encode(OSRMTableResponse{
			Code: "Ok",
			Durations: [][]float64{
				{300.0, 900.0},
			},
			Distances: [][]float64{
				{2500.0, 8000.0},
			},
		})
	}))
	defer server.Close()

	m, err := NewOSRMMatrix(server.URL)
	if err != nil {
		t.Fatalf("NewOSRMMatrix: %v", err)
	}

	sources := []model.Location{
		model.NewLatLngLocation(13.756300, 100.501800),
	}
	destinations := []model.Location{
		model.NewLatLngLocation(13.746900, 100.534600),
		model.NewLatLngLocation(13.730800, 100.541800),
	}

	durations, distances, err := m.BuildRectangularMatrix(context.Background(), sources, destinations)
	if err != nil {
		t.Fatalf("BuildRectangularMatrix: %v", err)
	}

	if len(durations) != 1 || len(durations[0]) != 2 {
		t.Fatalf("expected 1x2 durations, got %dx%d", len(durations), len(durations[0]))
	}
	if len(distances) != 1 || len(distances[0]) != 2 {
		t.Fatalf("expected 1x2 distances, got %dx%d", len(distances), len(distances[0]))
	}

	if durations[0][0] != 5 || durations[0][1] != 15 {
		t.Errorf("durations (expected minutes): got %v", durations)
	}
	if distances[0][0] != 2500 || distances[0][1] != 8000 {
		t.Errorf("distances (expected meters): got %v", distances)
	}
}
