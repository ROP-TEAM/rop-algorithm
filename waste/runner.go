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
	Solver      solver.Solver
	Config      Config
	Fleet       Fleet // template: Mode, capacity. Shift, Depot and Count are set per band/cell.
	MaxPerCell  int
	MaxShiftMin int // cap on a merged shift band; 0 = DefaultMaxShiftMin
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
	bands := MergeWindows(due, p.MaxShiftMin)
	cellCount := 0
	for _, band := range bands {
		shift := band.Shift.EndMin - band.Shift.StartMin
		for _, cell := range DecomposeGrid(band.Units, p.MaxPerCell) {
			cellCount++
			sol, err := p.solveCell(ctx, cell, band.Shift)
			if err != nil {
				return DayPlan{}, fmt.Errorf("weekday %d band %d-%d: %w", weekday, band.Shift.StartMin, band.Shift.EndMin, err)
			}
			acc.add(sol, ServiceByUnit(cell), positionByUnit(cell), shift)
		}
	}
	return DayPlan{Weekday: weekday, Cells: cellCount, Bands: len(bands), Metrics: acc.metrics()}, nil
}

func (p Planner) solveCell(ctx context.Context, cell []RouteUnit, shift TimeWindow) (model.Solution, error) {
	fleet := p.Fleet
	fleet.ShiftStart = shift.StartMin
	fleet.ShiftEnd = shift.EndMin
	fleet.Depot = centroid(cell)
	fleet.Count = cellTruckCount(p.Fleet, len(cell))

	problem, err := BuildProblem(cell, fleet, p.Config)
	if err != nil {
		return model.Solution{}, fmt.Errorf("build problem: %w", err)
	}
	return p.Solver.Solve(ctx, problem)
}

// positionByUnit indexes unit midpoints by id so deadhead can be measured
// between consecutive stops, excluding the artificial per-cell depot legs.
func positionByUnit(units []RouteUnit) map[string]LatLng {
	out := make(map[string]LatLng, len(units))
	for _, u := range units {
		out[u.UnitID] = u.Midpoint()
	}
	return out
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
