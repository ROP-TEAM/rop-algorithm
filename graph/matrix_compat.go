// matrix_compat.go re-exports model types for backward compatibility.
// Remove once all call sites import from model directly.
package graph

import "github.com/ROP-TEAM/rop-algorithm/model"

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
