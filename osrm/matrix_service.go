package osrm

import (
	"context"
	"fmt"
	"net/http"
	"strings"

	"github.com/ROP-TEAM/rop-algorithm/model"
)

// OSRMMatrix implements gmap.DistanceMatrix via the OSRM /table API.
type OSRMMatrix struct {
	baseURL    string
	httpClient *http.Client
}

// OSRMMatrixOption configures an OSRMMatrix.
type OSRMMatrixOption func(*OSRMMatrix)

// WithHTTPClient sets the HTTP client used to call OSRM.
func WithHTTPClient(client *http.Client) OSRMMatrixOption {
	return func(m *OSRMMatrix) {
		m.httpClient = client
	}
}

// NewOSRMMatrix creates an OSRMMatrix. baseURL is the OSRM server root, e.g. "http://localhost:5000".
func NewOSRMMatrix(baseURL string, opts ...OSRMMatrixOption) (*OSRMMatrix, error) {
	if strings.TrimSpace(baseURL) == "" {
		return nil, fmt.Errorf("osrm base URL is required")
	}
	m := &OSRMMatrix{
		baseURL:    strings.TrimRight(baseURL, "/"),
		httpClient: http.DefaultClient,
	}
	for _, opt := range opts {
		if opt != nil {
			opt(m)
		}
	}
	return m, nil
}

// BuildMatrix returns n×n matrices of durations (minutes) and distances (meters).
func (m *OSRMMatrix) BuildMatrix(ctx context.Context, locs []model.Location, opts model.MatrixOptions) ([][]int, [][]int, error) {
	n := len(locs)
	if n == 0 {
		return [][]int{}, [][]int{}, nil
	}

	url := fmt.Sprintf("%s/table/v1/driving/%s?annotations=duration,distance", m.baseURL, formatCoordinates(locs))

	resp, err := m.executeHTTP(ctx, url)
	if err != nil {
		return nil, nil, err
	}
	defer resp.Body.Close()

	body, err := readAndCheckHTTP(resp)
	if err != nil {
		return nil, nil, err
	}

	return parseTableResponse(body, n)
}
