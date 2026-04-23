package solver

import (
	"context"

	"github.com/ROP-TEAM/rop-algorithm/model"
)

// Solver runs the routing optimization algorithm.
type Solver interface {
	Solve(ctx context.Context, p model.Problem) (model.Solution, error)
}

// StubSolver returns all nodes as unassigned — use until a real solver is wired.
type StubSolver struct{}

func NewStub() Solver { return &StubSolver{} }

func (s *StubSolver) Solve(_ context.Context, p model.Problem) (model.Solution, error) {
	ids := make([]string, len(p.Nodes))
	for i, n := range p.Nodes {
		ids[i] = n.ID
	}
	return model.Solution{
		Unassigned: ids,
		Status:     model.SolutionStatusInfeasible,
	}, nil
}
