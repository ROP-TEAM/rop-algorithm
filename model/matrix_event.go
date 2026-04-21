package model

// MatrixEvent describes an observable event emitted by the matrix executor/cache flow.
type MatrixEvent struct {
	Name              string      `json:"name"`
	Policy            CachePolicy `json:"policy"`
	Reason            string      `json:"reason,omitempty"`
	CacheKey          string      `json:"cache_key,omitempty"`
	ChunkOrigins      int         `json:"chunk_origins,omitempty"`
	ChunkDestinations int         `json:"chunk_destinations,omitempty"`
	Error             string      `json:"error,omitempty"`
}
