// Package waste builds garbage-collection routing problems from cleaned route
// units and reuses the existing VRPTW solver to plan them.
//
// Phase 1 treats each collection route as an atomic unit (one route = one truck
// load) and places it at its midpoint, so the node-based solver can schedule it
// with real time windows and shift limits. Arc-level traversal is Phase 2.
package waste

import (
	"encoding/json"
	"fmt"
	"os"
	"slices"
)

// LatLng is a geographic point as [latitude, longitude], matching the cleaned
// JSON which stores coordinates as a two-element array.
type LatLng [2]float64

func (p LatLng) Lat() float64 { return p[0] }
func (p LatLng) Lng() float64 { return p[1] }

// TimeWindow is the collectable interval of a street in minutes from midnight.
// End may exceed 1440 when the window crosses midnight (e.g. 21:00-05:00).
type TimeWindow struct {
	StartMin       int  `json:"start_min"`
	EndMin         int  `json:"end_min"`
	CrossesMidnight bool `json:"crosses_midnight"`
}

// RouteUnit is one collection route as produced by build_route_units.py.
type RouteUnit struct {
	UnitID            string     `json:"unit_id"`
	Name              string     `json:"name"`
	CurrentVehicleID  string     `json:"current_vehicle_id"`
	OperationDayIndex []int      `json:"operation_day_indexes"`
	Frequency         string     `json:"frequency"`
	TimeWindow        TimeWindow `json:"time_window"`
	ServiceLenM       float64    `json:"service_len_m"`
	Start             LatLng     `json:"start"`
	End               LatLng     `json:"end"`
	LandUse           string     `json:"land_use"`
}

// Midpoint is where the unit is placed for the node-based solver.
func (u RouteUnit) Midpoint() LatLng {
	return LatLng{(u.Start.Lat() + u.End.Lat()) / 2, (u.Start.Lng() + u.End.Lng()) / 2}
}

// CollectedOn reports whether the unit must be serviced on the given weekday
// (0 = Sunday, matching the cleaning pipeline).
func (u RouteUnit) CollectedOn(weekday int) bool {
	return slices.Contains(u.OperationDayIndex, weekday)
}

// LoadUnits reads the route-unit JSON file produced by the cleaning pipeline.
func LoadUnits(path string) ([]RouteUnit, error) {
	raw, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("read route units %q: %w", path, err)
	}
	var units []RouteUnit
	if err := json.Unmarshal(raw, &units); err != nil {
		return nil, fmt.Errorf("decode route units %q: %w", path, err)
	}
	return units, nil
}
