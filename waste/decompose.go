package waste

import "sort"

// DecomposeGrid splits units into geographic cells each no larger than maxPerCell
// so every cell yields a solver problem small enough for a dense distance matrix.
// It recursively bisects the longest axis, which keeps cells compact without
// needing administrative boundaries. District-first splitting is a later refinement
// and requires a district field on the unit, which the Phase 1 input does not carry.
func DecomposeGrid(units []RouteUnit, maxPerCell int) [][]RouteUnit {
	if maxPerCell <= 0 || len(units) <= maxPerCell {
		return [][]RouteUnit{units}
	}
	left, right := bisectOnLongestAxis(units)
	// A degenerate split (all points coincident) cannot shrink further.
	if len(left) == 0 || len(right) == 0 {
		return [][]RouteUnit{units}
	}
	return append(DecomposeGrid(left, maxPerCell), DecomposeGrid(right, maxPerCell)...)
}

func bisectOnLongestAxis(units []RouteUnit) (left, right []RouteUnit) {
	minLat, maxLat, minLng, maxLng := bounds(units)
	sorted := make([]RouteUnit, len(units))
	copy(sorted, units)
	if maxLat-minLat >= maxLng-minLng {
		sort.Slice(sorted, func(i, j int) bool { return sorted[i].Midpoint().Lat() < sorted[j].Midpoint().Lat() })
	} else {
		sort.Slice(sorted, func(i, j int) bool { return sorted[i].Midpoint().Lng() < sorted[j].Midpoint().Lng() })
	}
	mid := len(sorted) / 2
	return sorted[:mid], sorted[mid:]
}

func bounds(units []RouteUnit) (minLat, maxLat, minLng, maxLng float64) {
	first := units[0].Midpoint()
	minLat, maxLat = first.Lat(), first.Lat()
	minLng, maxLng = first.Lng(), first.Lng()
	for _, u := range units {
		p := u.Midpoint()
		minLat = min(minLat, p.Lat())
		maxLat = max(maxLat, p.Lat())
		minLng = min(minLng, p.Lng())
		maxLng = max(maxLng, p.Lng())
	}
	return minLat, maxLat, minLng, maxLng
}

// UnitsForDay returns the units that must be collected on the given weekday.
func UnitsForDay(units []RouteUnit, weekday int) []RouteUnit {
	var out []RouteUnit
	for _, u := range units {
		if u.CollectedOn(weekday) {
			out = append(out, u)
		}
	}
	return out
}
