package waste

import (
	"fmt"

	"github.com/ROP-TEAM/rop-algorithm/model"
)

// BuildProblem turns a set of route units and a fleet into a solver Problem.
// Each unit becomes a delivery node at its midpoint, with service time derived
// from its collected length and the time window carried straight through. The
// depot is the fleet depot; capacity is disabled in Phase 1 because one route is
// assumed to be one truck load.
func BuildProblem(units []RouteUnit, fleet Fleet, cfg Config) (model.Problem, error) {
	if len(units) == 0 {
		return model.Problem{}, fmt.Errorf("no route units to plan")
	}
	vehicles, err := fleet.vehicles()
	if err != nil {
		return model.Problem{}, fmt.Errorf("build fleet: %w", err)
	}

	nodes := make([]model.Node, len(units))
	points := make([]LatLng, len(units))
	for i, u := range units {
		mid := u.Midpoint()
		points[i] = mid
		nodes[i] = model.Node{
			ID:          u.UnitID,
			Lat:         mid.Lat(),
			Lng:         mid.Lng(),
			ServiceTime: cfg.serviceMinutes(u.ServiceLenM),
			TWStart:     u.TimeWindow.StartMin,
			TWEnd:       u.TimeWindow.EndMin,
			Type:        model.NodeTypeDelivery,
			MustServe:   true,
		}
	}

	dist, dur := buildMatrices(fleet.Depot, points, cfg)
	return model.Problem{
		Depot:             depotNode(fleet.Depot),
		Nodes:             nodes,
		Vehicles:          vehicles,
		Distances:         dist,
		Durations:         dur,
		TimeLimitMS:       cfg.TimeLimitMS,
		EnableALNS:        true,
		DisableCapacity:   fleet.Capacity == 0,
		DisableTimeWindow: false,
	}, nil
}

func depotNode(p LatLng) model.Node {
	return model.Node{ID: "depot", Lat: p.Lat(), Lng: p.Lng(), Type: model.NodeTypeDepot}
}
