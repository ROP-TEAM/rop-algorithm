package waste

import (
	"testing"

	"github.com/ROP-TEAM/rop-algorithm/model"
)

func TestComputeMetricsSeparatesDeadheadFromService(t *testing.T) {
	sol := model.Solution{
		Routes: []model.Route{
			{Stops: []model.RouteStop{{NodeID: "R_1"}, {NodeID: "R_2"}}, TotalDistance: 2000, TotalDuration: 240},
			{Stops: []model.RouteStop{{NodeID: "R_3"}}, TotalDistance: 1000, TotalDuration: 120},
			{Stops: nil, TotalDistance: 999}, // empty route must not count
		},
		Unassigned: []string{"R_4"},
	}
	service := map[string]float64{"R_1": 100, "R_2": 200, "R_3": 300}

	m := ComputeMetrics(sol, service, 480)

	if m.TrucksUsed != 2 {
		t.Errorf("trucks used = %d, want 2 (empty route excluded)", m.TrucksUsed)
	}
	if m.AssignedUnits != 3 {
		t.Errorf("assigned = %d, want 3", m.AssignedUnits)
	}
	if m.UnassignedUnits != 1 {
		t.Errorf("unassigned = %d, want 1", m.UnassignedUnits)
	}
	if m.DeadheadM != 3000 {
		t.Errorf("deadhead = %.0f, want 3000", m.DeadheadM)
	}
	if m.ServiceM != 600 {
		t.Errorf("service = %.0f, want 600", m.ServiceM)
	}
	// durations 240 and 120, mean 180 / 480 = 0.375
	if m.MeanUtilisation < 0.37 || m.MeanUtilisation > 0.38 {
		t.Errorf("utilisation = %.3f, want ~0.375", m.MeanUtilisation)
	}
}

func TestGiniZeroWhenEqual(t *testing.T) {
	if g := gini([]float64{5, 5, 5}); g > 1e-9 {
		t.Errorf("gini of equal loads = %f, want 0", g)
	}
}
