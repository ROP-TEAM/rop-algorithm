package waste

import "math"

const earthRadiusM = 6_371_000

// haversineM returns the great-circle distance between two points in metres.
func haversineM(a, b LatLng) float64 {
	lat1, lat2 := rad(a.Lat()), rad(b.Lat())
	dLat := rad(b.Lat() - a.Lat())
	dLng := rad(b.Lng() - a.Lng())
	h := math.Sin(dLat/2)*math.Sin(dLat/2) +
		math.Cos(lat1)*math.Cos(lat2)*math.Sin(dLng/2)*math.Sin(dLng/2)
	return 2 * earthRadiusM * math.Asin(math.Sqrt(h))
}

func rad(deg float64) float64 { return deg * math.Pi / 180 }

// buildMatrices returns the (1+N)×(1+N) distance and duration matrices for a
// depot followed by the given points. Index 0 is the depot. Distances are
// straight-line metres; durations convert them at the driving speed. A real
// deployment swaps these for OSRM matrices — the solver only sees the arrays.
//
// With cfg.NoDepot the depot's row and column are left at zero, making it a
// virtual node: the solver pays nothing to enter or leave it, so route distance
// reflects only travel between collected streets, not legs to an invented depot.
func buildMatrices(depot LatLng, points []LatLng, cfg Config) (dist, dur [][]float64) {
	all := append([]LatLng{depot}, points...)
	n := len(all)
	dist = make([][]float64, n)
	dur = make([][]float64, n)
	for i := range all {
		dist[i] = make([]float64, n)
		dur[i] = make([]float64, n)
		for j := range all {
			if i == j || (cfg.NoDepot && (i == 0 || j == 0)) {
				continue
			}
			d := haversineM(all[i], all[j])
			dist[i][j] = d
			dur[i][j] = d / cfg.driveMPerMin()
		}
	}
	return dist, dur
}
