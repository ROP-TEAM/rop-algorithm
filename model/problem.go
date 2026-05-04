package model

// Problem is the complete input to a solver.
type Problem struct {
	Depot     Node
	Nodes     []Node      // delivery/pickup only (no depot)
	Vehicles  []Vehicle
	Durations [][]float64 // (1+N)×(1+N) travel-time matrix, minutes; row/col 0 = depot
	Distances [][]float64 // (1+N)×(1+N) distance matrix, meters
}

// TimeWindow is an HH:mm time interval.
type TimeWindow struct {
	Start string `json:"start"`
	End   string `json:"end"`
}

// InputVehicle is the external JSON representation of a vehicle.
type InputVehicle struct {
	ID            int         `json:"id"`
	Model         string      `json:"model"`
	Display       string      `json:"display"`
	Capacity      int         `json:"capacity"`
	Skills        []string    `json:"skills"`
	MaxTask       int         `json:"maxTask"`
	WorkTime      TimeWindow  `json:"workTime"`
	BreakTime     *TimeWindow `json:"breakTime"`
	StartLocation Location    `json:"startLocation"`
	EndLocation   Location    `json:"endLocation"`
}

// InputOrder is the external JSON representation of a pickup/delivery order.
type InputOrder struct {
	ID          int        `json:"id"`
	Name        string     `json:"name"`
	Capacity    int        `json:"capacity"`
	Skills      []string   `json:"skills"`
	Location    Location   `json:"location"`
	TimeWindow  TimeWindow `json:"timeWindow"`
	Type        NodeType   `json:"type"`
	Priority    Priority   `json:"priority"`
	ServiceTime int        `json:"serviceTime"`
}

// PlanningInput is the top-level JSON request body.
type PlanningInput struct {
	Vehicles []InputVehicle `json:"vehicle"`
	Orders   []InputOrder   `json:"orders"`
}
