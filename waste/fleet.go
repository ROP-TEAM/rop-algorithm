package waste

import (
	"fmt"

	"github.com/ROP-TEAM/rop-algorithm/model"
)

// FleetMode selects how many trucks the plan may use.
type FleetMode int

const (
	// MinimizeFleet supplies a generous pool of trucks and lets the solver's
	// per-vehicle fixed cost drive the count down — the rental objective.
	MinimizeFleet FleetMode = iota
	// FullFleet fixes the truck count, spreading work to finish sooner.
	FullFleet
)

// Fleet describes the trucks available for one subproblem.
type Fleet struct {
	Mode       FleetMode
	Count      int     // FullFleet: exact count; MinimizeFleet: upper bound
	ShiftStart int     // minutes from midnight
	ShiftEnd   int     // minutes from midnight
	Capacity   int     // 0 = capacity disabled (Phase 1 default)
	Depot      LatLng
}

// vehicles materialises the fleet as solver vehicles anchored at the depot. A
// non-zero fixed cost under MinimizeFleet makes each unused truck free and each
// used truck a penalty, so the solver opens as few as possible.
func (f Fleet) vehicles() ([]model.Vehicle, error) {
	if f.Count <= 0 {
		return nil, fmt.Errorf("fleet count must be positive, got %d", f.Count)
	}
	fixedCost := 0.0
	if f.Mode == MinimizeFleet {
		fixedCost = 100_000
	}
	vehicles := make([]model.Vehicle, f.Count)
	for i := range vehicles {
		vehicles[i] = model.Vehicle{
			ID:         fmt.Sprintf("truck-%03d", i),
			Capacity:   f.Capacity,
			ShiftStart: f.ShiftStart,
			ShiftEnd:   f.ShiftEnd,
			FixedCost:  fixedCost,
			CostPerKM:  1,
			StartLat:   f.Depot.Lat(),
			StartLng:   f.Depot.Lng(),
			EndLat:     f.Depot.Lat(),
			EndLng:     f.Depot.Lng(),
		}
	}
	return vehicles, nil
}
