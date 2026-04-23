package model

// Problem is the complete input to a solver.
type Problem struct {
	Depot     Node
	Nodes     []Node
	Vehicles  []Vehicle
	Durations [][]float64 // minutes, Durations[i][j] = travel time i→j
	Distances [][]float64 // meters,  Distances[i][j] = distance i→j
}
