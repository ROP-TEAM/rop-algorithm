package waste

import (
	"context"
	"testing"

	"github.com/ROP-TEAM/rop-algorithm/model"
)

// fakeSolver assigns every node of a problem to one route, so the planner's
// aggregation and metrics can be tested without the real solver.
type fakeSolver struct{}

func (fakeSolver) Solve(_ context.Context, p model.Problem) (model.Solution, error) {
	stops := make([]model.RouteStop, len(p.Nodes))
	for i, n := range p.Nodes {
		stops[i] = model.RouteStop{NodeID: n.ID}
	}
	route := model.Route{VehicleID: "truck-000", Stops: stops, TotalDistance: 1000, TotalDuration: 240}
	return model.Solution{Routes: []model.Route{route}, Status: model.SolutionStatusOK}, nil
}

func planAllDays() []RouteUnit {
	units := gridUnits(12)
	for i := range units {
		units[i].OperationDayIndex = []int{3} // all due on weekday 3
		units[i].ServiceLenM = 600
	}
	return units
}

func TestPlanDayAssignsEveryDueUnit(t *testing.T) {
	planner := Planner{
		Solver:     fakeSolver{},
		Config:     DefaultConfig(),
		Fleet:      Fleet{Mode: MinimizeFleet, ShiftStart: 300, ShiftEnd: 780},
		MaxPerCell: 4,
	}

	plan, err := planner.PlanDay(context.Background(), planAllDays(), 3)
	if err != nil {
		t.Fatalf("PlanDay error: %v", err)
	}
	if plan.Cells < 3 {
		t.Errorf("expected >=3 cells for 12 units at max 4, got %d", plan.Cells)
	}
	if plan.Metrics.AssignedUnits != 12 {
		t.Errorf("assigned units = %d, want 12", plan.Metrics.AssignedUnits)
	}
	if plan.Metrics.UnassignedUnits != 0 {
		t.Errorf("unassigned = %d, want 0", plan.Metrics.UnassignedUnits)
	}
	if plan.Metrics.TrucksUsed != plan.Cells {
		t.Errorf("trucks used = %d, want one per cell (%d)", plan.Metrics.TrucksUsed, plan.Cells)
	}
}

func TestPlanDayEmptyWhenNothingDue(t *testing.T) {
	planner := Planner{Solver: fakeSolver{}, Config: DefaultConfig(), MaxPerCell: 4}
	plan, err := planner.PlanDay(context.Background(), planAllDays(), 0)
	if err != nil {
		t.Fatalf("PlanDay error: %v", err)
	}
	if plan.Metrics.TrucksUsed != 0 || plan.Cells != 0 {
		t.Errorf("expected empty plan for idle weekday, got %+v", plan)
	}
}
