package graph

import (
	"context"
	"fmt"
	"strconv"

	googlemaps "googlemaps.github.io/maps"
)

const chunkSize = 25

// Location is a geographic coordinate used as input to BuildMatrix.
type Location struct {
	Lat float64
	Lng float64
}

// DistanceMatrix is the contract the algorithm uses to get travel data.
type DistanceMatrix interface {
	// BuildMatrix returns n×n matrices of durations (minutes) and distances (meters).
	BuildMatrix(ctx context.Context, locs []Location) (durations [][]int, distances [][]int, err error)
}

// GoogleMapsMatrix implements DistanceMatrix via the Distance Matrix API.
type GoogleMapsMatrix struct {
	client *googlemaps.Client
}

func NewGoogleMapsMatrix(apiKey string) (*GoogleMapsMatrix, error) {
	c, err := googlemaps.NewClient(googlemaps.WithAPIKey(apiKey))
	if err != nil {
		return nil, err
	}
	return &GoogleMapsMatrix{client: c}, nil
}

// BuildMatrix fetches travel durations and distances between every pair of locations.
// Automatically batches requests when len(locs) > 25.
func (g *GoogleMapsMatrix) BuildMatrix(ctx context.Context, locs []Location) ([][]int, [][]int, error) {
	n := len(locs)
	durations := make([][]int, n)
	distances := make([][]int, n)
	for i := range durations {
		durations[i] = make([]int, n)
		distances[i] = make([]int, n)
	}

	latLngs := make([]string, n)
	for i, l := range locs {
		latLngs[i] = fmt.Sprintf("%s,%s",
			strconv.FormatFloat(l.Lat, 'f', 6, 64),
			strconv.FormatFloat(l.Lng, 'f', 6, 64),
		)
	}

	for oStart := 0; oStart < n; oStart += chunkSize {
		oEnd := clamp(oStart+chunkSize, n)

		for dStart := 0; dStart < n; dStart += chunkSize {
			dEnd := clamp(dStart+chunkSize, n)

			req := &googlemaps.DistanceMatrixRequest{
				Origins:      latLngs[oStart:oEnd],
				Destinations: latLngs[dStart:dEnd],
				Mode:         googlemaps.TravelModeDriving,
				Units:        googlemaps.UnitsMetric,
			}

			resp, err := g.client.DistanceMatrix(ctx, req)
			if err != nil {
				return nil, nil, fmt.Errorf("distance matrix API: %w", err)
			}
			// library returns error on non-OK status via StatusError()

			for ri, row := range resp.Rows {
				for ci, el := range row.Elements {
					if el.Status != "OK" {
						return nil, nil, fmt.Errorf(
							"element [%d][%d] status: %s", oStart+ri, dStart+ci, el.Status,
						)
					}
					durations[oStart+ri][dStart+ci] = int(el.Duration.Minutes())
					distances[oStart+ri][dStart+ci] = el.Distance.Meters
				}
			}
		}
	}

	return durations, distances, nil
}

func clamp(v, max int) int {
	if v < max {
		return v
	}
	return max
}
