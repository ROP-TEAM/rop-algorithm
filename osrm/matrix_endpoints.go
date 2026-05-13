package osrm

import (
	"context"
	"encoding/json"
	"fmt"

	"github.com/ROP-TEAM/rop-algorithm/model"
)

// Nearest snaps a single coordinate to the nearest road.
func (m *OSRMMatrix) Nearest(ctx context.Context, loc model.Location) (NearestPoint, error) {
	url := fmt.Sprintf("%s/nearest/v1/driving/%s?number=1", m.baseURL, formatCoordinate(loc))

	resp, err := m.executeHTTP(ctx, url)
	if err != nil {
		return NearestPoint{}, err
	}
	defer resp.Body.Close()

	body, err := readAndCheckHTTP(resp)
	if err != nil {
		return NearestPoint{}, err
	}

	var nearest OSRMNearestResponse
	if err := json.Unmarshal(body, &nearest); err != nil {
		return NearestPoint{}, fmt.Errorf("osrm nearest decode: %w", err)
	}

	if nearest.Code != "Ok" {
		return NearestPoint{}, fmt.Errorf("osrm nearest error: %s", nearest.Code)
	}
	if len(nearest.Waypoints) == 0 {
		return NearestPoint{}, fmt.Errorf("osrm nearest: no waypoints returned")
	}

	wp := nearest.Waypoints[0]
	return NearestPoint{
		Name:     wp.Name,
		Lat:      wp.Location[1],
		Lng:      wp.Location[0],
		Distance: wp.Distance,
		Hint:     wp.Hint,
	}, nil
}

// Trip solves a TSP for the given locations and returns the optimal visit order.
// The first location is used as the start (source=first, roundtrip=false).
func (m *OSRMMatrix) Trip(ctx context.Context, locs []model.Location) (TripResult, error) {
	if len(locs) < 2 {
		return TripResult{}, fmt.Errorf("osrm trip: need at least 2 locations")
	}

	url := fmt.Sprintf("%s/trip/v1/driving/%s?roundtrip=false&source=first", m.baseURL, formatCoordinates(locs))

	resp, err := m.executeHTTP(ctx, url)
	if err != nil {
		return TripResult{}, err
	}
	defer resp.Body.Close()

	body, err := readAndCheckHTTP(resp)
	if err != nil {
		return TripResult{}, err
	}

	var trip OSRMTripResponse
	if err := json.Unmarshal(body, &trip); err != nil {
		return TripResult{}, fmt.Errorf("osrm trip decode: %w", err)
	}

	if trip.Code != "Ok" {
		msg := trip.Message
		if msg == "" {
			msg = trip.Code
		}
		return TripResult{}, fmt.Errorf("osrm trip error: %s", msg)
	}

	waypoints := make([]TripWaypoint, len(trip.Waypoints))
	for i, w := range trip.Waypoints {
		waypoints[i] = TripWaypoint{
			Name:          w.Name,
			Lat:           w.Location[1],
			Lng:           w.Location[0],
			TripsIndex:    w.TripsIndex,
			WaypointIndex: w.WaypointIndex,
			Hint:          w.Hint,
		}
	}

	result := TripResult{Waypoints: waypoints}
	if len(trip.Trips) > 0 {
		result.Distance = trip.Trips[0].Distance
		result.Duration = trip.Trips[0].Duration
		result.Geometry = trip.Trips[0].Geometry
	}
	return result, nil
}

// Route gets the fastest route with full polyline geometry between the given waypoints.
func (m *OSRMMatrix) Route(ctx context.Context, locs []model.Location) (RouteResult, error) {
	if len(locs) < 2 {
		return RouteResult{}, fmt.Errorf("osrm route: need at least 2 locations")
	}

	url := fmt.Sprintf("%s/route/v1/driving/%s?geometries=polyline&overview=full", m.baseURL, formatCoordinates(locs))

	resp, err := m.executeHTTP(ctx, url)
	if err != nil {
		return RouteResult{}, err
	}
	defer resp.Body.Close()

	body, err := readAndCheckHTTP(resp)
	if err != nil {
		return RouteResult{}, err
	}

	var route OSRMRouteResponse
	if err := json.Unmarshal(body, &route); err != nil {
		return RouteResult{}, fmt.Errorf("osrm route decode: %w", err)
	}

	if route.Code != "Ok" {
		msg := route.Message
		if msg == "" {
			msg = route.Code
		}
		return RouteResult{}, fmt.Errorf("osrm route error: %s", msg)
	}

	waypoints := make([]RouteWaypoint, len(route.Waypoints))
	for i, w := range route.Waypoints {
		waypoints[i] = RouteWaypoint{
			Name: w.Name,
			Lat:  w.Location[1],
			Lng:  w.Location[0],
		}
	}

	routes := make([]RouteItem, len(route.Routes))
	for i, r := range route.Routes {
		routes[i] = RouteItem{
			Distance: r.Distance,
			Duration: r.Duration,
			Geometry: r.Geometry,
		}
	}

	return RouteResult{Waypoints: waypoints, Routes: routes}, nil
}

// BuildRectangularMatrix returns an m×n matrix from m sources to n destinations.
func (m *OSRMMatrix) BuildRectangularMatrix(ctx context.Context, sources, destinations []model.Location) ([][]int, [][]int, error) {
	s := len(sources)
	d := len(destinations)
	if s == 0 || d == 0 {
		return [][]int{}, [][]int{}, nil
	}

	allLocs := append([]model.Location{}, sources...)
	allLocs = append(allLocs, destinations...)

	url := fmt.Sprintf("%s/table/v1/driving/%s?annotations=duration,distance&sources=%s&destinations=%s",
		m.baseURL, formatCoordinates(allLocs), indexRange(0, s), indexRange(s, s+d))

	resp, err := m.executeHTTP(ctx, url)
	if err != nil {
		return nil, nil, err
	}
	defer resp.Body.Close()

	body, err := readAndCheckHTTP(resp)
	if err != nil {
		return nil, nil, err
	}

	return parseTableResponseWithShape(body, s, d)
}
