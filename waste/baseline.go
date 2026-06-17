package waste

import "math"

// BaselineMetrics scores the current vehicle assignment without re-planning. It
// keeps each unit on its present truck (current_vehicle_id) and only orders the
// stops, so the deadhead it reports is the cost of today's grouping. It is
// computed with the same midpoint + nearest-neighbour method as the solver
// metrics, which makes the before/after deadhead comparison apples-to-apples.
type BaselineMetrics struct {
	TruckGroups        int
	AssignedUnits      int
	InterStopDeadheadM float64
	ServiceM           float64
	MeanUtilisation    float64
	LoadGini           float64
}

// BaselineDay scores the current assignment for one weekday. shiftMinutes is the
// shift length used only for the utilisation figure; the deadhead and Gini do
// not depend on it.
func BaselineDay(units []RouteUnit, weekday, shiftMinutes int, cfg Config) BaselineMetrics {
	groups := groupByVehicle(UnitsForDay(units, weekday))
	var m BaselineMetrics
	var durations, utilisation []float64
	for _, group := range groups {
		m.TruckGroups++
		m.AssignedUnits += len(group)
		points, serviceM := groupPoints(group)
		deadheadM := nnDeadheadM(points)
		m.InterStopDeadheadM += deadheadM
		m.ServiceM += serviceM

		duration := float64(cfg.serviceMinutes(serviceM)) + deadheadM/cfg.driveMPerMin()
		durations = append(durations, duration)
		if shiftMinutes > 0 {
			utilisation = append(utilisation, duration/float64(shiftMinutes))
		}
	}
	m.MeanUtilisation = mean(utilisation)
	m.LoadGini = gini(durations)
	return m
}

func groupByVehicle(units []RouteUnit) map[string][]RouteUnit {
	out := make(map[string][]RouteUnit)
	for _, u := range units {
		out[u.CurrentVehicleID] = append(out[u.CurrentVehicleID], u)
	}
	return out
}

func groupPoints(group []RouteUnit) (points []LatLng, serviceM float64) {
	points = make([]LatLng, len(group))
	for i, u := range group {
		points[i] = u.Midpoint()
		serviceM += u.ServiceLenM
	}
	return points, serviceM
}

// nnDeadheadM sums the straight-line travel of a nearest-neighbour tour over the
// points, starting from the one closest to their centroid. This mirrors the
// solver's centroid depot while measuring only between-stop legs, so it matches
// Metrics.InterStopDeadheadM.
func nnDeadheadM(points []LatLng) float64 {
	if len(points) < 2 {
		return 0
	}
	visited := make([]bool, len(points))
	current := nearestToCentroid(points)
	visited[current] = true

	var total float64
	for step := 1; step < len(points); step++ {
		next, best := -1, math.MaxFloat64
		for j := range points {
			if visited[j] {
				continue
			}
			if d := haversineM(points[current], points[j]); d < best {
				best, next = d, j
			}
		}
		total += best
		visited[next] = true
		current = next
	}
	return total
}

func nearestToCentroid(points []LatLng) int {
	c := centroidOf(points)
	best, idx := math.MaxFloat64, 0
	for i, p := range points {
		if d := haversineM(c, p); d < best {
			best, idx = d, i
		}
	}
	return idx
}

func centroidOf(points []LatLng) LatLng {
	var lat, lng float64
	for _, p := range points {
		lat += p.Lat()
		lng += p.Lng()
	}
	n := float64(len(points))
	return LatLng{lat / n, lng / n}
}
