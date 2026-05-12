package osrm

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/ROP-TEAM/rop-algorithm/model"
)

func TestBuildMatrix(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/table/v1/driving/100.501800,13.756300;100.534600,13.746900" {
			t.Errorf("unexpected path: %s", r.URL.Path)
		}
		if r.URL.Query().Get("annotations") != "duration,distance" {
			t.Errorf("missing annotations param")
		}

		resp := OSRMTableResponse{
			Code: "Ok",
			Durations: [][]float64{
				{0, 600.0},
				{600.0, 0},
			},
			Distances: [][]float64{
				{0, 5000.0},
				{5000.0, 0},
			},
		}
		json.NewEncoder(w).Encode(resp)
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

	durations, distances, err := m.BuildMatrix(context.Background(), locs, model.MatrixOptions{})
	if err != nil {
		t.Fatalf("BuildMatrix: %v", err)
	}

	if len(durations) != 2 || len(durations[0]) != 2 {
		t.Fatalf("expected 2x2 durations, got %dx%d", len(durations), len(durations[0]))
	}
	if len(distances) != 2 || len(distances[0]) != 2 {
		t.Fatalf("expected 2x2 distances, got %dx%d", len(distances), len(distances[0]))
	}

	if durations[0][0] != 0 || durations[0][1] != 10 || durations[1][0] != 10 || durations[1][1] != 0 {
		t.Errorf("durations (expected minutes): got %v", durations)
	}
	if distances[0][0] != 0 || distances[0][1] != 5000 || distances[1][0] != 5000 || distances[1][1] != 0 {
		t.Errorf("distances (expected meters): got %v", distances)
	}
}

func TestBuildMatrixHTTPError(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusInternalServerError)
	}))
	defer server.Close()

	m, err := NewOSRMMatrix(server.URL)
	if err != nil {
		t.Fatalf("NewOSRMMatrix: %v", err)
	}

	locs := []model.Location{
		model.NewLatLngLocation(13.75, 100.50),
		model.NewLatLngLocation(13.74, 100.53),
	}

	_, _, err = m.BuildMatrix(context.Background(), locs, model.MatrixOptions{})
	if err == nil {
		t.Fatal("expected error for HTTP 500")
	}
}

func TestBuildMatrixOSRMError(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		json.NewEncoder(w).Encode(OSRMTableResponse{
			Code:    "NoTable",
			Message: "no route found",
		})
	}))
	defer server.Close()

	m, err := NewOSRMMatrix(server.URL)
	if err != nil {
		t.Fatalf("NewOSRMMatrix: %v", err)
	}

	locs := []model.Location{
		model.NewLatLngLocation(13.75, 100.50),
		model.NewLatLngLocation(13.74, 100.53),
	}

	_, _, err = m.BuildMatrix(context.Background(), locs, model.MatrixOptions{})
	if err == nil {
		t.Fatal("expected error for OSRM NoTable")
	}
}

func TestBuildMatrixEmptyLocs(t *testing.T) {
	m, err := NewOSRMMatrix("http://localhost:5000")
	if err != nil {
		t.Fatalf("NewOSRMMatrix: %v", err)
	}

	durations, distances, err := m.BuildMatrix(context.Background(), []model.Location{}, model.MatrixOptions{})
	if err != nil {
		t.Fatalf("BuildMatrix: %v", err)
	}
	if len(durations) != 0 || len(distances) != 0 {
		t.Errorf("expected empty matrices, got %v %v", durations, distances)
	}
}

func TestBuildMatrixRowCountMismatch(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		json.NewEncoder(w).Encode(OSRMTableResponse{
			Code:      "Ok",
			Durations: [][]float64{{0}},
			Distances: [][]float64{{0}},
		})
	}))
	defer server.Close()

	m, err := NewOSRMMatrix(server.URL)
	if err != nil {
		t.Fatalf("NewOSRMMatrix: %v", err)
	}

	locs := []model.Location{
		model.NewLatLngLocation(13.75, 100.50),
		model.NewLatLngLocation(13.74, 100.53),
	}

	_, _, err = m.BuildMatrix(context.Background(), locs, model.MatrixOptions{})
	if err == nil {
		t.Fatal("expected error for row count mismatch")
	}
}
