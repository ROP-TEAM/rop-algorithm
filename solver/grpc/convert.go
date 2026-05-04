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
		Depot:       nodeToProto(p.Depot),
		Nodes:       nodes,
		Vehicles:    vehicles,
		Durations:   flattenMatrix(p.Durations),
		Distances:   flattenMatrix(p.Distances),
		MatrixSize:  int32(len(p.Nodes) + 1),
	}
}

func nodeToProto(n model.Node) *pb.Node {
	return &pb.Node{
		Id:          n.ID,
		Lat:         n.Lat,
		Lng:         n.Lng,
		Demand:      int32(n.Demand),
		ServiceTime: int32(n.ServiceTime),
		TwStart:     int32(n.TWStart),
		TwEnd:       int32(n.TWEnd),
		Tags:        n.Tags,
		Type:        string(n.Type),
		PairId:      n.PairID,
		Priority:    n.Priority.Rank(),
		DeadlineMin: int32(n.DeadlineMin),
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
	}
}

func responseToSolution(r *pb.SolveResponse) model.Solution {
	routes := make([]model.Route, len(r.Routes))
	for i, rt := range r.Routes {
		routes[i] = protoToRoute(rt)
	}
	return model.Solution{
		Routes:     routes,
		Unassigned: r.Unassigned,
		Objective:  r.Objective,
		Status:     model.SolutionStatus(r.Status),
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
	return model.Route{
		VehicleID:     r.VehicleId,
		Stops:         stops,
		TotalDistance: r.TotalDistance,
		TotalDuration: int(r.TotalDuration),
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
