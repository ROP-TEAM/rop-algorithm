package model

// Vehicle is a routing agent.
type Vehicle struct {
	ID          string
	Capacity    int
	ShiftStart  int     // minutes from midnight
	ShiftEnd    int     // minutes from midnight
	BreakStart  int     // minutes from midnight; 0 = no break
	BreakEnd    int     // minutes from midnight
	MaxTasks    int     // 0 = unlimited
	MaxDistance float64 // meters; 0 = unlimited
	Tags        []string
}
