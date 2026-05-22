package grpcsolver

import (
	"context"
	"fmt"

	"google.golang.org/grpc"

	"github.com/ROP-TEAM/rop-algorithm/model"
	"github.com/ROP-TEAM/rop-algorithm/solver"
	pb "github.com/ROP-TEAM/rop-algorithm/solver/grpc/pb"
)

// GRPCSolver calls the C++ solver subprocess over gRPC.
type GRPCSolver struct {
	client pb.SolverServiceClient
}

// New wraps a gRPC connection as a solver.Solver.
func New(conn *grpc.ClientConn) solver.Solver {
	return &GRPCSolver{client: pb.NewSolverServiceClient(conn)}
}

// Solve invokes the C++ solver. Problem.SpeedWeight is stored in model.Problem and wired to the
// proto field speed_weight (field 8) — activate by regenerating solver/grpc/pb via:
//
//	go generate ./solver/proto/
//
// and adding SpeedWeight to problemToProto in convert.go.
func (s *GRPCSolver) Solve(ctx context.Context, p model.Problem) (model.Solution, error) {
	resp, err := s.client.Solve(ctx, problemToProto(p))
	if err != nil {
		return model.Solution{}, fmt.Errorf("grpc solver: %w", err)
	}
	return responseToSolution(resp), nil
}
