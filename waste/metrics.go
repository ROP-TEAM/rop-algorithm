package waste

import (
	"sort"

	"github.com/ROP-TEAM/rop-algorithm/model"
)

// Metrics summarises a plan against the levers the project cares about: how many
// truck-shifts it used, how far trucks drove without collecting (deadhead), how
// full each shift was, and how evenly work was spread.
type Metrics struct {
	TrucksUsed      int
	AssignedUnits   int
	UnassignedUnits int
	DeadheadM       float64
	ServiceM        float64
	MeanUtilisation float64 // mean route duration / shift length
	LoadGini        float64 // inequality of route durations across trucks
}

// ComputeMetrics scores a solution. serviceByUnit maps a unit id to its
// collected length so service work can be separated from deadhead driving.
func ComputeMetrics(sol model.Solution, serviceByUnit map[string]float64, shiftMinutes int) Metrics {
	m := Metrics{UnassignedUnits: len(sol.Unassigned)}
	var durations []float64
	for _, route := range sol.Routes {
		if len(route.Stops) == 0 {
			continue
		}
		m.TrucksUsed++
		m.DeadheadM += route.TotalDistance
		durations = append(durations, float64(route.TotalDuration))
		for _, stop := range route.Stops {
			if length, ok := serviceByUnit[stop.NodeID]; ok {
				m.ServiceM += length
				m.AssignedUnits++
			}
		}
	}
	m.MeanUtilisation = meanUtilisation(durations, shiftMinutes)
	m.LoadGini = gini(durations)
	return m
}

func meanUtilisation(durations []float64, shiftMinutes int) float64 {
	if len(durations) == 0 || shiftMinutes <= 0 {
		return 0
	}
	var sum float64
	for _, d := range durations {
		sum += d
	}
	return sum / float64(len(durations)) / float64(shiftMinutes)
}

func gini(values []float64) float64 {
	if len(values) == 0 {
		return 0
	}
	sorted := make([]float64, len(values))
	copy(sorted, values)
	sort.Float64s(sorted)
	var cumulative, total float64
	for i, v := range sorted {
		cumulative += float64(i+1) * v
		total += v
	}
	if total == 0 {
		return 0
	}
	n := float64(len(sorted))
	return 2*cumulative/(n*total) - (n+1)/n
}

// ServiceByUnit indexes units by id for metric attribution.
func ServiceByUnit(units []RouteUnit) map[string]float64 {
	out := make(map[string]float64, len(units))
	for _, u := range units {
		out[u.UnitID] = u.ServiceLenM
	}
	return out
}
