package waste

// Config holds the tunable assumptions that convert distance to time. The true
// speeds are unknown, so callers sweep them; see clean_trash/REPORT.md for the
// sensitivity of the results to these values.
type Config struct {
	CollectKmh  float64 // collection speed (slow, frequent stops)
	DriveKmh    float64 // driving speed between routes (deadhead)
	TimeLimitMS int     // solver budget per subproblem; 0 = solver default
	NoDepot     bool    // make the depot a zero-distance virtual node (Phase 1: no real depot)
}

// DefaultConfig is the mid point of the speed sweep used in the Python proofs,
// with a virtual depot so deadhead reflects only travel between collected streets.
func DefaultConfig() Config {
	return Config{CollectKmh: 5.0, DriveKmh: 20.0, NoDepot: true}
}

func (c Config) collectMPerMin() float64 { return c.CollectKmh * 1000 / 60 }
func (c Config) driveMPerMin() float64   { return c.DriveKmh * 1000 / 60 }

// serviceMinutes converts a route's collected length into service time.
func (c Config) serviceMinutes(lengthM float64) int {
	return int(lengthM/c.collectMPerMin() + 0.5)
}
