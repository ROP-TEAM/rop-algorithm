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

func (s *GRPCSolver) Solve(ctx context.Context, p model.Problem) (model.Solution, error) {
	resp, err := s.client.Solve(ctx, problemToProto(p))
	if err != nil {
		return model.Solution{}, fmt.Errorf("grpc solver: %w", err)
	}
	return responseToSolution(resp), nil
}
