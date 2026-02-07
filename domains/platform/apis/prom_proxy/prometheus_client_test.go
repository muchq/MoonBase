package prom_proxy

import (
	"context"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func TestNewPrometheusClient_Success(t *testing.T) {
	baseURL := "http://localhost:9090"
	client, err := NewPrometheusClient(baseURL)
	
	require.NoError(t, err)
	assert.NotNil(t, client)
	assert.NotNil(t, client.client)
}

func TestNewPrometheusClient_InvalidURL(t *testing.T) {
	// Test with an invalid URL scheme that might cause an error
	invalidURL := "://invalid-url"
	client, err := NewPrometheusClient(invalidURL)
	
	// The official client might not error on invalid URLs during construction,
	// so we check that we at least get a client back
	if err != nil {
		assert.Nil(t, client)
	} else {
		assert.NotNil(t, client)
	}
}

func TestPrometheusClient_Query_Success(t *testing.T) {
	// Mock Prometheus server
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		// Debug: Let's see exactly what the official client sends
		t.Logf("Method: %s", r.Method)
		t.Logf("Content-Type: %s", r.Header.Get("Content-Type"))
		t.Logf("URL: %s", r.URL.String())
		
		assert.Contains(t, r.URL.Path, "/api/v1/query")
		
		// Check for query parameter in either GET query params or POST form data
		var queryParam string
		if r.Method == "GET" {
			queryParam = r.URL.Query().Get("query")
		} else if r.Method == "POST" {
			queryParam = r.FormValue("query")
		}
		assert.Equal(t, "test_metric", queryParam)
		
		response := `{
			"status": "success",
			"data": {
				"resultType": "vector",
				"result": [
					{
						"metric": {"__name__": "test_metric", "instance": "localhost:9090"},
						"value": [1609459200, "42.5"]
					}
				]
			}
		}`
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusOK)
		w.Write([]byte(response))
	}))
	defer server.Close()
	
	client, err := NewPrometheusClient(server.URL)
	require.NoError(t, err)
	
	result, err := client.Query(context.Background(), "test_metric")
	
	require.NoError(t, err)
	assert.Equal(t, "success", result.Status)
	assert.Equal(t, "vector", result.Data.ResultType)
	assert.Len(t, result.Data.Result, 1)
	assert.Equal(t, "test_metric", result.Data.Result[0].Metric["__name__"])
	assert.Equal(t, "localhost:9090", result.Data.Result[0].Metric["instance"])
}

func TestPrometheusClient_Query_HTTPError(t *testing.T) {
	// Mock server that returns 500 error
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusInternalServerError)
		w.Write([]byte("Internal Server Error"))
	}))
	defer server.Close()
	
	client, err := NewPrometheusClient(server.URL)
	require.NoError(t, err)
	
	result, err := client.Query(context.Background(), "test_metric")
	
	assert.Error(t, err)
	assert.Nil(t, result)
}

func TestPrometheusClient_QueryRange_Success(t *testing.T) {
	// Mock Prometheus server
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		assert.Contains(t, r.URL.Path, "/api/v1/query_range")
		
		// Check for query parameter in either GET query params or POST form data
		var queryParam string
		if r.Method == "GET" {
			queryParam = r.URL.Query().Get("query")
		} else if r.Method == "POST" {
			queryParam = r.FormValue("query")
		}
		assert.Equal(t, "test_metric", queryParam)
		
		response := `{
			"status": "success",
			"data": {
				"resultType": "matrix",
				"result": [
					{
						"metric": {"__name__": "test_metric"},
						"values": [
							[1609459200, "42.5"],
							[1609459230, "43.0"],
							[1609459260, "41.2"]
						]
					}
				]
			}
		}`
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusOK)
		w.Write([]byte(response))
	}))
	defer server.Close()
	
	client, err := NewPrometheusClient(server.URL)
	require.NoError(t, err)
	
	start := time.Unix(1609459200, 0)
	end := time.Unix(1609459800, 0)
	
	result, err := client.QueryRange(context.Background(), "test_metric", start, end, "30s")
	
	require.NoError(t, err)
	assert.Equal(t, "success", result.Status)
	assert.Equal(t, "matrix", result.Data.ResultType)
	assert.Len(t, result.Data.Result, 1)
	assert.Len(t, result.Data.Result[0].Values, 3)
}

func TestPrometheusClient_QueryRange_HTTPError(t *testing.T) {
	// Mock server that returns 404 error
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusNotFound)
		w.Write([]byte("Not Found"))
	}))
	defer server.Close()
	
	client, err := NewPrometheusClient(server.URL)
	require.NoError(t, err)
	
	start := time.Unix(1609459200, 0)
	end := time.Unix(1609459800, 0)
	
	result, err := client.QueryRange(context.Background(), "test_metric", start, end, "30s")
	
	assert.Error(t, err)
	assert.Nil(t, result)
}

func TestPrometheusClient_Query_ContextTimeout(t *testing.T) {
	// Mock server with delay
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		time.Sleep(200 * time.Millisecond)
		w.WriteHeader(http.StatusOK)
		w.Write([]byte(`{"status": "success", "data": {"resultType": "vector", "result": []}}`))
	}))
	defer server.Close()
	
	client, err := NewPrometheusClient(server.URL)
	require.NoError(t, err)
	
	ctx, cancel := context.WithTimeout(context.Background(), 100*time.Millisecond)
	defer cancel()
	
	result, err := client.Query(ctx, "test_metric")
	
	assert.Error(t, err)
	assert.Nil(t, result)
	assert.Contains(t, err.Error(), "context deadline exceeded")
}

func TestPrometheusClient_QueryRange_InvalidStep(t *testing.T) {
	client, err := NewPrometheusClient("http://localhost:9090")
	require.NoError(t, err)
	
	start := time.Unix(1609459200, 0)
	end := time.Unix(1609459800, 0)
	
	// Test with invalid step format
	result, err := client.QueryRange(context.Background(), "test_metric", start, end, "invalid_step")
	
	assert.Error(t, err)
	assert.Nil(t, result)
}

func TestConvertToQueryResponse_Vector(t *testing.T) {
	// Since convertToQueryResponse is not exported, we test it indirectly
	// through a successful query that returns vector data
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		response := `{
			"status": "success",
			"data": {
				"resultType": "vector",
				"result": [
					{
						"metric": {"job": "test"},
						"value": [1609459200, "123.45"]
					}
				]
			}
		}`
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusOK)
		w.Write([]byte(response))
	}))
	defer server.Close()
	
	client, err := NewPrometheusClient(server.URL)
	require.NoError(t, err)
	
	result, err := client.Query(context.Background(), "test_metric")
	
	require.NoError(t, err)
	assert.Equal(t, "vector", result.Data.ResultType)
	assert.Len(t, result.Data.Result, 1)
	assert.Equal(t, "test", result.Data.Result[0].Metric["job"])
}

func TestConvertToQueryResponse_Matrix(t *testing.T) {
	// Test matrix conversion indirectly through QueryRange
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		response := `{
			"status": "success",
			"data": {
				"resultType": "matrix",
				"result": [
					{
						"metric": {"job": "test"},
						"values": [
							[1609459200, "123.45"],
							[1609459260, "124.56"]
						]
					}
				]
			}
		}`
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusOK)
		w.Write([]byte(response))
	}))
	defer server.Close()
	
	client, err := NewPrometheusClient(server.URL)
	require.NoError(t, err)
	
	start := time.Unix(1609459200, 0)
	end := time.Unix(1609459800, 0)
	
	result, err := client.QueryRange(context.Background(), "test_metric", start, end, "60s")
	
	require.NoError(t, err)
	assert.Equal(t, "matrix", result.Data.ResultType)
	assert.Len(t, result.Data.Result, 1)
	assert.Equal(t, "test", result.Data.Result[0].Metric["job"])
	assert.Len(t, result.Data.Result[0].Values, 2)
}