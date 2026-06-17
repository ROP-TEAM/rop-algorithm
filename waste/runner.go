package waste

import (
	"context"
	"fmt"

	"github.com/ROP-TEAM/rop-algorithm/model"
	"github.com/ROP-TEAM/rop-algorithm/solver"
)

// Planner plans one weekday of collection. It groups the units due that day into
// shift bands by time window — so each truck works one realistic shift — then
// splits each band geographically into matrix-sized cells and solves each cell
// with the shared VRPTW solver. The solver schedules stops sequentially inside
// the shift, which is what the greedy prototype could not do.
type Planner struct {
	Solver     solver.Solver
	Config     Config
	Fleet      Fleet // template: Mode, capacity. Shift, Depot and Count are set per band/cell.
	MaxPerCell int
}

// DayPlan is the merged outcome for one weekday.
type DayPlan struct {
	Weekday int
	Cells   int
	Bands   int
	Metrics Metrics
}

// PlanDay plans every unit due on weekday and returns the merged metrics.
func (p Planner) PlanDay(ctx context.Context, units []RouteUnit, weekday int) (DayPlan, error) {
	due := UnitsForDay(units, weekday)
	if len(due) == 0 {
		return DayPlan{Weekday: weekday}, nil
	}

	acc := newAccumulator()
	bands := groupByWindow(due)
	cellCount := 0
	for window, bandUnits := range bands {
		shift := window.EndMin - window.StartMin
		for _, cell := range DecomposeGrid(bandUnits, p.MaxPerCell) {
			cellCount++
			sol, err := p.solveCell(ctx, cell, window)
			if err != nil {
				return DayPlan{}, fmt.Errorf("weekday %d window %d-%d: %w", weekday, window.StartMin, window.EndMin, err)
			}
			acc.add(sol, ServiceByUnit(cell), shift)
		}
	}
	return DayPlan{Weekday: weekday, Cells: cellCount, Bands: len(bands), Metrics: acc.metrics()}, nil
}

func (p Planner) solveCell(ctx context.Context, cell []RouteUnit, window TimeWindow) (model.Solution, error) {
	fleet := p.Fleet
	fleet.ShiftStart = window.StartMin
	fleet.ShiftEnd = window.EndMin
	fleet.Depot = centroid(cell)
	fleet.Count = cellTruckCount(p.Fleet, len(cell))

	problem, err := BuildProblem(cell, fleet, p.Config)
	if err != nil {
		return model.Solution{}, fmt.Errorf("build problem: %w", err)
	}
	return p.Solver.Solve(ctx, problem)
}

func groupByWindow(units []RouteUnit) map[TimeWindow][]RouteUnit {
	bands := make(map[TimeWindow][]RouteUnit)
	for _, u := range units {
		bands[u.TimeWindow] = append(bands[u.TimeWindow], u)
	}
	return bands
}

// cellTruckCount caps the fleet per cell: an upper bound of one truck per unit
// when minimising, or the template count when the fleet is fixed.
func cellTruckCount(template Fleet, cellSize int) int {
	if template.Mode == FullFleet && template.Count > 0 {
		return template.Count
	}
	return cellSize
}

func centroid(units []RouteUnit) LatLng {
	var lat, lng float64
	for _, u := range units {
		mid := u.Midpoint()
		lat += mid.Lat()
		lng += mid.Lng()
	}
	n := float64(len(units))
	return LatLng{lat / n, lng / n}
}
