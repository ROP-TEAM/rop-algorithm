package model

// SolutionStatus reports the outcome of a solver run.
type SolutionStatus string

const (
	SolutionStatusOK         SolutionStatus = "OK"
	SolutionStatusInfeasible SolutionStatus = "INFEASIBLE"
	SolutionStatusTimeout    SolutionStatus = "TIMEOUT"
)

// RouteStop is one visited node within a Route.
type RouteStop struct {
	NodeID     string
	ArrivalMin int // minutes from midnight
	DepartMin  int // minutes from midnight
}

// Route is the assigned sequence of stops for one vehicle.
type Route struct {
	VehicleID     string
	Stops         []RouteStop
	TotalDistance float64 // meters
	TotalDuration int     // minutes
	TotalCost     float64
}

// DropReason explains why a node was left out of a plan.
type DropReason struct {
	NodeID string
	Code   string
	Detail string
}

// Solution is the output from a solver run.
type Solution struct {
	Routes      []Route
	Unassigned  []string
	Objective   float64
	Status      SolutionStatus
	DropReasons []DropReason
}
