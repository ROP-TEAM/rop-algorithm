package grpcsolver

import (
	"github.com/ROP-TEAM/rop-algorithm/model"
	pb "github.com/ROP-TEAM/rop-algorithm/solver/grpc/pb"
)

func problemToProto(p model.Problem) *pb.SolveRequest {
	nodes := make([]*pb.Node, len(p.Nodes))
	for i, n := range p.Nodes {
		nodes[i] = nodeToProto(n)
	}
	vehicles := make([]*pb.Vehicle, len(p.Vehicles))
	for i, v := range p.Vehicles {
		vehicles[i] = vehicleToProto(v)
	}
	return &pb.SolveRequest{
		Depot:             nodeToProto(p.Depot),
		Nodes:             nodes,
		Vehicles:          vehicles,
		Durations:         flattenMatrix(p.Durations),
		Distances:         flattenMatrix(p.Distances),
		MatrixSize:        int32(len(p.Nodes) + 1),
		TimeLimitMs:       int32(p.TimeLimitMS),
		EnableAlns:        p.EnableALNS,
		EnableMultiTrip:   p.EnableMultiTrip,
		ReloadMin:         int32(p.ReloadMin),
		Seed:              p.Seed,
		MultiStartCount:   int32(p.MultiStartCount),
		DisableCapacity:   p.DisableCapacity,
		DisableTimeWindow: p.DisableTimeWindow,
		WeightDistance:    p.WeightDistance,
		WeightCost:        p.WeightCost,
	}
}

func nodeToProto(n model.Node) *pb.Node {
	return &pb.Node{
		Id:             n.ID,
		Lat:            n.Lat,
		Lng:            n.Lng,
		Demand:         int32(n.Demand),
		ServiceTime:    int32(n.ServiceTime),
		TwStart:        int32(n.TWStart),
		TwEnd:          int32(n.TWEnd),
		Tags:           n.Tags,
		Type:           string(n.Type),
		PairId:         n.PairID,
		Priority:       n.Priority.Rank(),
		DeadlineMin:    int32(n.DeadlineMin),
		LinehaulDemand: int32(deriveLinehaul(n)),
		BackhaulDemand: int32(deriveBackhaul(n)),
		MustServe:      n.MustServe,
	}
}

func vehicleToProto(v model.Vehicle) *pb.Vehicle {
	return &pb.Vehicle{
		Id:          v.ID,
		Capacity:    int32(v.Capacity),
		ShiftStart:  int32(v.ShiftStart),
		ShiftEnd:    int32(v.ShiftEnd),
		BreakStart:  int32(v.BreakStart),
		BreakEnd:    int32(v.BreakEnd),
		MaxTasks:    int32(v.MaxTasks),
		MaxDistance: v.MaxDistance,
		Tags:        v.Tags,
		FixedCost:   v.FixedCost,
		CostPerKm:   v.CostPerKM,
		Type:        v.Type,
	}
}

func responseToSolution(r *pb.SolveResponse) model.Solution {
	routes := make([]model.Route, len(r.Routes))
	for i, rt := range r.Routes {
		routes[i] = protoToRoute(rt)
	}
	dropReasons := make([]model.DropReason, len(r.DropReasons))
	for i, dr := range r.DropReasons {
		dropReasons[i] = model.DropReason{
			NodeID: dr.NodeId,
			Code:   dr.Code,
			Detail: dr.Detail,
		}
	}
	return model.Solution{
		Routes:      routes,
		Unassigned:  r.Unassigned,
		Objective:   r.Objective,
		Status:      model.SolutionStatus(r.Status),
		DropReasons: dropReasons,
	}
}

func protoToRoute(r *pb.Route) model.Route {
	stops := make([]model.RouteStop, len(r.Stops))
	for i, s := range r.Stops {
		stops[i] = model.RouteStop{
			NodeID:     s.NodeId,
			ArrivalMin: int(s.ArrivalMin),
			DepartMin:  int(s.DepartMin),
		}
	}
	var tripSizes []int
	if len(r.TripSizes) > 0 {
		tripSizes = make([]int, len(r.TripSizes))
		for i, sz := range r.TripSizes {
			tripSizes[i] = int(sz)
		}
	}
	return model.Route{
		VehicleID:     r.VehicleId,
		Stops:         stops,
		TotalDistance: r.TotalDistance,
		TotalDuration: int(r.TotalDuration),
		TotalCost:     r.TotalCost,
		TripSizes:     tripSizes,
	}
}

func flattenMatrix(m [][]float64) []float64 {
	if len(m) == 0 {
		return nil
	}
	flat := make([]float64, 0, len(m)*len(m[0]))
	for _, row := range m {
		flat = append(flat, row...)
	}
	return flat
}

func deriveLinehaul(n model.Node) int {
	if n.Linehaul != 0 || n.Backhaul != 0 {
		return n.Linehaul
	}
	if n.Type == model.NodeTypePickup {
		return 0
	}
	return n.Demand
}

func deriveBackhaul(n model.Node) int {
	if n.Linehaul != 0 || n.Backhaul != 0 {
		return n.Backhaul
	}
	if n.Type == model.NodeTypePickup {
		return n.Demand
	}
	return 0
}
