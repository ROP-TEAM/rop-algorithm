package graph

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
)

func (g *GoogleMapsMatrix) doDistanceMatrixRequest(ctx context.Context, req DistanceMatrixRequest) (*DistanceMatrixResponse, error) {
	client := g.httpClient
	if client == nil {
		client = http.DefaultClient
	}

	g.emitEvent(ctx, MatrixEvent{
		Name:              "api_request",
		Policy:            ResolveCachePolicy(req),
		ChunkOrigins:      len(req.Origins),
		ChunkDestinations: len(req.Destinations),
	})

	httpReq, err := http.NewRequestWithContext(ctx, http.MethodGet, g.buildDistanceMatrixURL(req), nil)
	if err != nil {
		g.emitEvent(ctx, MatrixEvent{
			Name:              "api_request_error",
			Policy:            ResolveCachePolicy(req),
			Error:             err.Error(),
			ChunkOrigins:      len(req.Origins),
			ChunkDestinations: len(req.Destinations),
		})
		return nil, err
	}

	resp, err := client.Do(httpReq)
	if err != nil {
		g.emitEvent(ctx, MatrixEvent{
			Name:              "api_request_error",
			Policy:            ResolveCachePolicy(req),
			Error:             err.Error(),
			ChunkOrigins:      len(req.Origins),
			ChunkDestinations: len(req.Destinations),
		})
		return nil, fmt.Errorf("distance matrix API: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		g.emitEvent(ctx, MatrixEvent{
			Name:              "api_response_error",
			Policy:            ResolveCachePolicy(req),
			Error:             resp.Status,
			ChunkOrigins:      len(req.Origins),
			ChunkDestinations: len(req.Destinations),
		})
		return nil, fmt.Errorf("distance matrix API unexpected HTTP status: %s", resp.Status)
	}

	var matrixResp DistanceMatrixResponse
	if err := json.NewDecoder(resp.Body).Decode(&matrixResp); err != nil {
		g.emitEvent(ctx, MatrixEvent{
			Name:              "api_decode_error",
			Policy:            ResolveCachePolicy(req),
			Error:             err.Error(),
			ChunkOrigins:      len(req.Origins),
			ChunkDestinations: len(req.Destinations),
		})
		return nil, fmt.Errorf("distance matrix API decode: %w", err)
	}

	if matrixResp.Status != "OK" {
		errMsg := matrixResp.Status
		if matrixResp.ErrorMessage != "" {
			errMsg += ": " + matrixResp.ErrorMessage
		}
		g.emitEvent(ctx, MatrixEvent{
			Name:              "api_status_error",
			Policy:            ResolveCachePolicy(req),
			Error:             errMsg,
			ChunkOrigins:      len(req.Origins),
			ChunkDestinations: len(req.Destinations),
		})
		if matrixResp.ErrorMessage != "" {
			return nil, fmt.Errorf("distance matrix API status: %s (%s)", matrixResp.Status, matrixResp.ErrorMessage)
		}
		return nil, fmt.Errorf("distance matrix API status: %s", matrixResp.Status)
	}

	if len(matrixResp.Rows) != len(req.Origins) {
		g.emitEvent(ctx, MatrixEvent{
			Name:              "api_shape_error",
			Policy:            ResolveCachePolicy(req),
			Error:             "row_count_mismatch",
			ChunkOrigins:      len(req.Origins),
			ChunkDestinations: len(req.Destinations),
		})
		return nil, fmt.Errorf("distance matrix API returned %d rows, expected %d", len(matrixResp.Rows), len(req.Origins))
	}

	return &matrixResp, nil
}
