package waste

import (
	"sort"

	"github.com/ROP-TEAM/rop-algorithm/model"
)

// Metrics summarises a plan against the levers the project cares about: how many
// truck-shifts it used, how far trucks drove without collecting (deadhead), how
// full each shift was, and how evenly work was spread.
type Metrics struct {
	TrucksUsed         int
	AssignedUnits      int
	UnassignedUnits    int
	DeadheadM          float64 // route distance incl. depot legs (solver objective)
	InterStopDeadheadM float64 // between stops only; comparable to the baseline
	ServiceM           float64
	MeanUtilisation    float64 // mean route duration / shift length
	LoadGini           float64 // inequality of route durations across trucks
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

// accumulator merges metrics across shift bands whose shift lengths differ, so
// utilisation is measured against each route's own shift rather than one global
// value.
type accumulator struct {
	m           Metrics
	durations   []float64
	utilisation []float64
}

func newAccumulator() *accumulator { return &accumulator{} }

func (a *accumulator) add(sol model.Solution, serviceByUnit map[string]float64, posByUnit map[string]LatLng, shiftMinutes int) {
	a.m.UnassignedUnits += len(sol.Unassigned)
	for _, route := range sol.Routes {
		if len(route.Stops) == 0 {
			continue
		}
		a.m.TrucksUsed++
		a.m.DeadheadM += route.TotalDistance
		a.m.InterStopDeadheadM += interStopDeadhead(route, posByUnit)
		a.durations = append(a.durations, float64(route.TotalDuration))
		if shiftMinutes > 0 {
			a.utilisation = append(a.utilisation, float64(route.TotalDuration)/float64(shiftMinutes))
		}
		for _, stop := range route.Stops {
			if length, ok := serviceByUnit[stop.NodeID]; ok {
				a.m.ServiceM += length
				a.m.AssignedUnits++
			}
		}
	}
}

// interStopDeadhead sums straight-line distance between consecutive stops only,
// excluding the depot legs the VRP solver adds at each route's ends. This is the
// figure comparable to the Python baseline's inter-route deadhead.
func interStopDeadhead(route model.Route, posByUnit map[string]LatLng) float64 {
	var total float64
	for i := 1; i < len(route.Stops); i++ {
		prev, ok1 := posByUnit[route.Stops[i-1].NodeID]
		curr, ok2 := posByUnit[route.Stops[i].NodeID]
		if ok1 && ok2 {
			total += haversineM(prev, curr)
		}
	}
	return total
}

func (a *accumulator) metrics() Metrics {
	a.m.MeanUtilisation = mean(a.utilisation)
	a.m.LoadGini = gini(a.durations)
	return a.m
}

func mean(values []float64) float64 {
	if len(values) == 0 {
		return 0
	}
	var sum float64
	for _, v := range values {
		sum += v
	}
	return sum / float64(len(values))
}

// ServiceByUnit indexes units by id for metric attribution.
func ServiceByUnit(units []RouteUnit) map[string]float64 {
	out := make(map[string]float64, len(units))
	for _, u := range units {
		out[u.UnitID] = u.ServiceLenM
	}
	return out
}
