package gmap

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"

	"github.com/ROP-TEAM/rop-algorithm/model"
)

func (g *GoogleMapsMatrix) doDistanceMatrixRequest(ctx context.Context, req model.DistanceMatrixRequest) (*model.DistanceMatrixResponse, error) {
	policy := ResolveCachePolicy(req)
	nO, nD := len(req.Origins), len(req.Destinations)

	g.emitEvent(ctx, model.MatrixEvent{Name: "api_request", Policy: policy, ChunkOrigins: nO, ChunkDestinations: nD})

	httpResp, err := g.executeHTTPRequest(ctx, req)
	if err != nil {
		g.emitEvent(ctx, model.MatrixEvent{Name: "api_request_error", Policy: policy, Error: err.Error(), ChunkOrigins: nO, ChunkDestinations: nD})
		return nil, err
	}
	defer httpResp.Body.Close()

	if httpResp.StatusCode != http.StatusOK {
		g.emitEvent(ctx, model.MatrixEvent{Name: "api_response_error", Policy: policy, Error: httpResp.Status, ChunkOrigins: nO, ChunkDestinations: nD})
		return nil, fmt.Errorf("distance matrix API unexpected HTTP status: %s", httpResp.Status)
	}

	apiResp, eventName, err := decodeAndValidateAPIResponse(httpResp.Body, len(req.Origins))
	if err != nil {
		g.emitEvent(ctx, model.MatrixEvent{Name: eventName, Policy: policy, Error: err.Error(), ChunkOrigins: nO, ChunkDestinations: nD})
		return nil, err
	}
	return apiResp, nil
}

func (g *GoogleMapsMatrix) executeHTTPRequest(ctx context.Context, req model.DistanceMatrixRequest) (*http.Response, error) {
	client := g.httpClient
	if client == nil {
		client = http.DefaultClient
	}
	httpReq, err := http.NewRequestWithContext(ctx, http.MethodGet, g.buildDistanceMatrixURL(req), nil)
	if err != nil {
		return nil, err
	}
	resp, err := client.Do(httpReq)
	if err != nil {
		return nil, fmt.Errorf("distance matrix API: %w", err)
	}
	return resp, nil
}

func decodeAndValidateAPIResponse(body io.Reader, expectedRows int) (*model.DistanceMatrixResponse, string, error) {
	var resp model.DistanceMatrixResponse
	if err := json.NewDecoder(body).Decode(&resp); err != nil {
		return nil, "api_decode_error", fmt.Errorf("distance matrix API decode: %w", err)
	}
	if resp.Status != "OK" {
		if resp.ErrorMessage != "" {
			return nil, "api_status_error", fmt.Errorf("distance matrix API status: %s (%s)", resp.Status, resp.ErrorMessage)
		}
		return nil, "api_status_error", fmt.Errorf("distance matrix API status: %s", resp.Status)
	}
	if len(resp.Rows) != expectedRows {
		return nil, "api_shape_error", fmt.Errorf("distance matrix API returned %d rows, expected %d", len(resp.Rows), expectedRows)
	}
	return &resp, "", nil
}
