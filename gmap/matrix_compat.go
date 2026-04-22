// matrix_compat.go re-exports model types and constructors for backward compatibility.
// Remove once all call sites import from model directly.
package gmap

import "github.com/ROP-TEAM/rop-algorithm/model"

func NewLatLngLocation(lat, lng float64) Location { return model.NewLatLngLocation(lat, lng) }
func NewRawLocation(raw string) Location          { return model.NewRawLocation(raw) }

type Location = model.Location
type MatrixOptions = model.MatrixOptions
type CachePolicy = model.CachePolicy
type MatrixCacheConfig = model.MatrixCacheConfig
type MatrixCacheKeyParts = model.MatrixCacheKeyParts
type MatrixEvent = model.MatrixEvent
type DistanceMatrixRequest = model.DistanceMatrixRequest
type DistanceMatrixResult = model.DistanceMatrixResult
type DistanceMatrixResponse = model.DistanceMatrixResponse
type DistanceMatrixRow = model.DistanceMatrixRow
type DistanceMatrixElement = model.DistanceMatrixElement
type ValueText = model.ValueText
type TransitFare = model.TransitFare

const (
	CachePolicyStatic  = model.CachePolicyStatic
	CachePolicyTraffic = model.CachePolicyTraffic
)

const (
	ModeDriving   = model.ModeDriving
	ModeWalking   = model.ModeWalking
	ModeBicycling = model.ModeBicycling
	ModeTransit   = model.ModeTransit

	TrafficModelBestGuess   = model.TrafficModelBestGuess
	TrafficModelPessimistic = model.TrafficModelPessimistic
	TrafficModelOptimistic  = model.TrafficModelOptimistic

	AvoidTolls    = model.AvoidTolls
	AvoidHighways = model.AvoidHighways
	AvoidFerries  = model.AvoidFerries
	AvoidIndoor   = model.AvoidIndoor
)
