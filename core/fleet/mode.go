// Package fleet holds the fleet-sizing strategy shared across routing problems:
// whether a plan should use the fewest vehicles possible or a fixed full fleet.
// It is domain-agnostic — both the general VRP and the waste planner pick a mode
// and translate it into a per-vehicle fixed cost the solver minimises.
package fleet

// Mode selects how many vehicles a plan may open.
type Mode int

const (
	// Minimize supplies a generous pool of vehicles and a large per-vehicle fixed
	// cost, so opening a vehicle must pay for itself — the rental objective that
	// drives the vehicle count down.
	Minimize Mode = iota
	// Full keeps the vehicle count fixed with no fixed cost, spreading work to
	// finish sooner rather than with the fewest vehicles.
	Full
)

// FixedCostPenalty is charged per opened vehicle under Minimize. It must dominate
// any realistic total travel distance so the solver always prefers fewer
// vehicles over shorter routes; raise it above the largest plausible route
// distance if instances grow.
const FixedCostPenalty = 100_000

// FixedCost returns the per-vehicle fixed cost the solver should apply for the
// mode: a dominating penalty when minimising, zero when the fleet is fixed.
func FixedCost(m Mode) float64 {
	if m == Minimize {
		return FixedCostPenalty
	}
	return 0
}
