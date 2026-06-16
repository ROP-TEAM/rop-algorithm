package waste

import (
	"context"
	"fmt"

	"github.com/ROP-TEAM/rop-algorithm/model"
	"github.com/ROP-TEAM/rop-algorithm/solver"
)

// Planner plans one weekday of collection by decomposing it into matrix-sized
// cells, solving each with the shared VRPTW solver, and merging the results.
type Planner struct {
	Solver     solver.Solver
	Config     Config
	Fleet      Fleet // template: Mode, shift, capacity. Depot and Count are set per cell.
	MaxPerCell int   // cap on units per subproblem so the distance matrix stays small
}

// DayPlan is the merged outcome for one weekday.
type DayPlan struct {
	Weekday int
	Cells   int
	Metrics Metrics
}

// PlanDay filters the units due on weekday, splits them into cells, solves each,
// and reports the merged metrics.
func (p Planner) PlanDay(ctx context.Context, units []RouteUnit, weekday int) (DayPlan, error) {
	due := UnitsForDay(units, weekday)
	if len(due) == 0 {
		return DayPlan{Weekday: weekday}, nil
	}
	cells := DecomposeGrid(due, p.MaxPerCell)

	merged := model.Solution{Status: model.SolutionStatusOK}
	for i, cell := range cells {
		sol, err := p.solveCell(ctx, cell)
		if err != nil {
			return DayPlan{}, fmt.Errorf("solve cell %d/%d (weekday %d): %w", i+1, len(cells), weekday, err)
		}
		merged.Routes = append(merged.Routes, sol.Routes...)
		merged.Unassigned = append(merged.Unassigned, sol.Unassigned...)
	}

	shiftMinutes := p.Fleet.ShiftEnd - p.Fleet.ShiftStart
	return DayPlan{
		Weekday: weekday,
		Cells:   len(cells),
		Metrics: ComputeMetrics(merged, ServiceByUnit(due), shiftMinutes),
	}, nil
}

func (p Planner) solveCell(ctx context.Context, cell []RouteUnit) (model.Solution, error) {
	fleet := p.Fleet
	fleet.Depot = centroid(cell)
	fleet.Count = cellTruckCount(p.Fleet, len(cell))

	problem, err := BuildProblem(cell, fleet, p.Config)
	if err != nil {
		return model.Solution{}, fmt.Errorf("build problem: %w", err)
	}
	return p.Solver.Solve(ctx, problem)
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
