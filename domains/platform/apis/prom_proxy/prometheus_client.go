package prom_proxy

import (
	"context"
	"time"

	"github.com/prometheus/client_golang/api"
	v1 "github.com/prometheus/client_golang/api/prometheus/v1"
	"github.com/prometheus/common/model"
)

type PrometheusClient struct {
	client v1.API
}

func NewPrometheusClient(baseURL string) (*PrometheusClient, error) {
	client, err := api.NewClient(api.Config{
		Address: baseURL,
	})
	if err != nil {
		return nil, err
	}

	return &PrometheusClient{
		client: v1.NewAPI(client),
	}, nil
}

func (c *PrometheusClient) Query(ctx context.Context, query string) (*QueryResponse, error) {
	result, warnings, err := c.client.Query(ctx, query, time.Now())
	if err != nil {
		return nil, err
	}

	// Log warnings if any (could be improved with proper logging)
	_ = warnings

	// Convert Prometheus result to our QueryResponse format
	return convertToQueryResponse(result), nil
}

func (c *PrometheusClient) QueryRange(ctx context.Context, query string, start, end time.Time, step string) (*QueryResponse, error) {
	stepDuration, err := model.ParseDuration(step)
	if err != nil {
		return nil, err
	}

	result, warnings, err := c.client.QueryRange(ctx, query, v1.Range{
		Start: start,
		End:   end,
		Step:  time.Duration(stepDuration),
	})
	if err != nil {
		return nil, err
	}

	// Log warnings if any (could be improved with proper logging)
	_ = warnings

	// Convert Prometheus result to our QueryResponse format
	return convertToQueryResponse(result), nil
}

// Convert Prometheus model.Value to our QueryResponse format
func convertToQueryResponse(value model.Value) *QueryResponse {
	response := &QueryResponse{
		Status: "success",
		Data: struct {
			ResultType string   `json:"resultType"`
			Result     []Result `json:"result"`
		}{
			Result: []Result{},
		},
	}

	switch v := value.(type) {
	case model.Vector:
		response.Data.ResultType = "vector"
		for _, sample := range v {
			result := Result{
				Metric: make(map[string]string),
				Value:  []interface{}{float64(sample.Timestamp.Unix()), sample.Value.String()},
			}
			for name, value := range sample.Metric {
				result.Metric[string(name)] = string(value)
			}
			response.Data.Result = append(response.Data.Result, result)
		}

	case model.Matrix:
		response.Data.ResultType = "matrix"
		for _, series := range v {
			result := Result{
				Metric: make(map[string]string),
				Values: [][]interface{}{},
			}
			for name, value := range series.Metric {
				result.Metric[string(name)] = string(value)
			}
			for _, pair := range series.Values {
				result.Values = append(result.Values, []interface{}{
					float64(pair.Timestamp.Unix()),
					pair.Value.String(),
				})
			}
			response.Data.Result = append(response.Data.Result, result)
		}

	case *model.Scalar:
		response.Data.ResultType = "scalar"
		result := Result{
			Value: []interface{}{float64(v.Timestamp.Unix()), v.Value.String()},
		}
		response.Data.Result = append(response.Data.Result, result)

	case *model.String:
		response.Data.ResultType = "string"
		result := Result{
			Value: []interface{}{float64(v.Timestamp.Unix()), string(v.Value)},
		}
		response.Data.Result = append(response.Data.Result, result)
	}

	return response
}

// Keep the existing Result and QueryResponse structs for compatibility
type QueryResponse struct {
	Status string `json:"status"`
	Data   struct {
		ResultType string   `json:"resultType"`
		Result     []Result `json:"result"`
	} `json:"data"`
}

type Result struct {
	Metric map[string]string `json:"metric"`
	Value  []interface{}     `json:"value,omitempty"`  // For instant queries
	Values [][]interface{}   `json:"values,omitempty"` // For range queries
}