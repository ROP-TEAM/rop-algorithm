package waste

import "testing"

func sampleUnits() []RouteUnit {
	return []RouteUnit{
		{
			UnitID:            "R_1",
			OperationDayIndex: []int{1, 4},
			TimeWindow:        TimeWindow{StartMin: 300, EndMin: 780},
			ServiceLenM:       1000, // at 5 km/h -> 12 min
			Start:             LatLng{13.72, 100.50},
			End:               LatLng{13.74, 100.52},
		},
		{
			UnitID:            "R_2",
			OperationDayIndex: []int{2},
			TimeWindow:        TimeWindow{StartMin: 1260, EndMin: 1740},
			ServiceLenM:       500,
			Start:             LatLng{13.70, 100.48},
			End:               LatLng{13.70, 100.48},
		},
	}
}

func TestBuildProblemShapesNodesAndMatrix(t *testing.T) {
	units := sampleUnits()
	fleet := Fleet{Mode: FullFleet, Count: 2, ShiftStart: 300, ShiftEnd: 780, Depot: LatLng{13.71, 100.49}}

	problem, err := BuildProblem(units, fleet, DefaultConfig())
	if err != nil {
		t.Fatalf("BuildProblem returned error: %v", err)
	}

	if got := len(problem.Nodes); got != 2 {
		t.Errorf("node count = %d, want 2", got)
	}
	if got := len(problem.Vehicles); got != 2 {
		t.Errorf("vehicle count = %d, want 2", got)
	}
	wantDim := len(units) + 1
	if got := len(problem.Distances); got != wantDim {
		t.Fatalf("distance matrix dim = %d, want %d", got, wantDim)
	}
	if got := len(problem.Distances[0]); got != wantDim {
		t.Errorf("distance row width = %d, want %d", got, wantDim)
	}
}

func TestServiceTimeConvertsLengthAtCollectionSpeed(t *testing.T) {
	problem, err := BuildProblem(sampleUnits(), Fleet{Mode: FullFleet, Count: 1}, DefaultConfig())
	if err != nil {
		t.Fatalf("BuildProblem returned error: %v", err)
	}
	// 1000 m at 5 km/h (83.3 m/min) = 12 min.
	if got := problem.Nodes[0].ServiceTime; got != 12 {
		t.Errorf("service time = %d, want 12", got)
	}
	if problem.Nodes[0].TWEnd != 780 {
		t.Errorf("time window end = %d, want 780", problem.Nodes[0].TWEnd)
	}
}

func TestMinimizeFleetAddsFixedCost(t *testing.T) {
	vehicles, err := Fleet{Mode: MinimizeFleet, Count: 3}.vehicles()
	if err != nil {
		t.Fatalf("vehicles() error: %v", err)
	}
	for _, v := range vehicles {
		if v.FixedCost <= 0 {
			t.Errorf("MinimizeFleet vehicle %s has no fixed cost", v.ID)
		}
	}
}

func TestCollectedOnMatchesOperationDays(t *testing.T) {
	u := sampleUnits()[0]
	if !u.CollectedOn(4) {
		t.Error("expected unit collected on weekday 4")
	}
	if u.CollectedOn(0) {
		t.Error("unit should not be collected on weekday 0")
	}
}
