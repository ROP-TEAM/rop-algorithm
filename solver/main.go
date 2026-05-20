package solver

import (
	"context"
	"errors"

	"github.com/ROP-TEAM/rop-algorithm/model"
)

// Solver runs the routing optimization algorithm.
type Solver interface {
	Solve(ctx context.Context, p model.Problem) (model.Solution, error)
}

// StubSolver returns all nodes as unassigned — use until a real solver is wired.
type StubSolver struct{}

func NewStub() Solver { return &StubSolver{} }

func (s *StubSolver) Solve(_ context.Context, _ model.Problem) (model.Solution, error) {
	return model.Solution{}, errors.New("solver not available: SOLVER_BINARY_PATH is not set")
}
