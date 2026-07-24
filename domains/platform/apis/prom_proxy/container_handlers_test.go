package prom_proxy

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// ---------------------------------------------------------------- unit

func TestImageTag(t *testing.T) {
	tests := []struct {
		name  string
		image string
		want  string
	}{
		{"pinned image", "ghcr.io/muchq/mithril:abc123", "abc123"},
		{"latest", "ghcr.io/muchq/mithril:latest", "latest"},
		{"third party with version", "caddy:2-alpine", "2-alpine"},
		// Negative: nothing to report rather than something wrong.
		{"untagged", "ghcr.io/muchq/mithril", ""},
		{"empty", "", ""},
		// A registry port is a colon that is not a tag; reading it as one
		// would report "5000" as the running revision.
		{"registry port, no tag", "registry.local:5000/mithril", ""},
		{"registry port with tag", "registry.local:5000/mithril:abc123", "abc123"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			assert.Equal(t, tt.want, imageTag(tt.image))
		})
	}
}

func TestContainerDisplayName(t *testing.T) {
	tests := []struct {
		name string
		in   string
		want string
	}{
		{"compose naming", "ubuntu-golf_hub-1", "golf_hub"},
		{"hyphenated service", "ubuntu-microgpt-serve-1", "microgpt-serve"},
		// Double-digit replicas: a bare "-1" strip would turn this into
		// "svc0" rather than "svc".
		{"double digit index", "ubuntu-svc-10", "svc"},
		{"no project prefix", "mithril-1", "mithril"},
		{"no index", "cadvisor", "cadvisor"},
		// Negative: a trailing segment that isn't an index must survive.
		{"non numeric suffix", "ubuntu-one_d4-postgres", "one_d4-postgres"},
		{"empty", "", ""},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			assert.Equal(t, tt.want, containerDisplayName(tt.in))
		})
	}
}

func TestMatchesContainer(t *testing.T) {
	assert.True(t, matchesContainer("ubuntu-mithril-1", "mithril"), "by service name")
	assert.True(t, matchesContainer("ubuntu-mithril-1", "ubuntu-mithril-1"), "by container name")
	assert.False(t, matchesContainer("ubuntu-mithril-1", "posterize"), "different service")
	// Substring must not match, or "d4" would resolve to one_d4.
	assert.False(t, matchesContainer("ubuntu-one_d4-1", "d4"), "substring")
}

// ---------------------------------------------------------------- integration

// A host whose posterize is crash-looping on the current revision while
// golf_hub is healthy — the shape of the incident these endpoints exist for.
func containerFixture() *mockPrometheusClient {
	return &mockPrometheusClient{queryResponses: map[string]*QueryResponse{
		`count by (name, image) (container_last_seen)`: {
			Status: "success",
			Data: struct {
				ResultType string   `json:"resultType"`
				Result     []Result `json:"result"`
			}{
				ResultType: "vector",
				Result: []Result{
					{Metric: map[string]string{"name": "ubuntu-posterize-1", "image": "ghcr.io/muchq/posterize:abc123"}, Value: []interface{}{1609459200.0, "1"}},
					{Metric: map[string]string{"name": "ubuntu-golf_hub-1", "image": "ghcr.io/muchq/golf_hub:abc123"}, Value: []interface{}{1609459200.0, "1"}},
					{Metric: map[string]string{"name": "caddy", "image": "caddy:2-alpine"}, Value: []interface{}{1609459200.0, "1"}},
				},
			},
		},
		`changes(container_start_time_seconds{name="ubuntu-posterize-1"}[1h])`: scalarResponse("47"),
		`time()-container_start_time_seconds{name="ubuntu-posterize-1"}`:       scalarResponse("8"),
		`changes(container_start_time_seconds{name="ubuntu-golf_hub-1"}[1h])`:  scalarResponse("0"),
		`time()-container_start_time_seconds{name="ubuntu-golf_hub-1"}`:        scalarResponse("86400"),
	}}
}

func TestGetContainers(t *testing.T) {
	handler := NewMetricsHandler(containerFixture())
	w := httptest.NewRecorder()
	handler.GetContainers(w, httptest.NewRequest(http.MethodGet, "/metrics/v1/containers", nil))

	require.Equal(t, http.StatusOK, w.Code)
	var response ContainerMetrics
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	require.Len(t, response.Containers, 3)

	byName := map[string]ContainerStats{}
	for _, c := range response.Containers {
		byName[c.Name] = c
	}

	posterize := byName["ubuntu-posterize-1"]
	assert.True(t, posterize.CrashLooping)
	assert.Equal(t, 47.0, posterize.RestartsLastHour)
	assert.Equal(t, "abc123", posterize.Version, "running revision from the image tag")

	assert.False(t, byName["ubuntu-golf_hub-1"].CrashLooping, "healthy peer unaffected")
	// Infrastructure containers emit no app metrics, so this listing is the
	// only place they appear at all.
	assert.Equal(t, "2-alpine", byName["caddy"].Version)
}

func TestGetContainers_PrometheusDown(t *testing.T) {
	handler := NewMetricsHandler(&mockPrometheusClient{queryError: assert.AnError})
	w := httptest.NewRecorder()
	handler.GetContainers(w, httptest.NewRequest(http.MethodGet, "/metrics/v1/containers", nil))
	assert.Equal(t, http.StatusInternalServerError, w.Code)
}

func TestGetContainerDetail(t *testing.T) {
	tests := []struct {
		name       string
		requested  string
		wantStatus int
		wantName   string
	}{
		{"by service name", "posterize", http.StatusOK, "ubuntu-posterize-1"},
		{"by container name", "ubuntu-posterize-1", http.StatusOK, "ubuntu-posterize-1"},
		{"infrastructure container", "caddy", http.StatusOK, "caddy"},
		{"unknown", "nope", http.StatusNotFound, ""},
		{"substring of a real one", "d4", http.StatusNotFound, ""},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			handler := NewMetricsHandler(containerFixture())
			req := httptest.NewRequest(http.MethodGet, "/metrics/v1/container/"+tt.requested, nil)
			req.SetPathValue("name", tt.requested)
			w := httptest.NewRecorder()
			handler.GetContainerDetail(w, req)

			require.Equal(t, tt.wantStatus, w.Code)
			if tt.wantStatus != http.StatusOK {
				return
			}
			var got ContainerStats
			require.NoError(t, json.Unmarshal(w.Body.Bytes(), &got))
			assert.Equal(t, tt.wantName, got.Name)
		})
	}
}

func TestGetContainerTimeSeries(t *testing.T) {
	tests := []struct {
		name       string
		requested  string
		timeRange  string
		wantStatus int
	}{
		{"by service name", "posterize", "1d", http.StatusOK},
		{"by container name", "ubuntu-posterize-1", "30m", http.StatusOK},
		{"unknown container", "nope", "1d", http.StatusNotFound},
		{"invalid range", "posterize", "99y", http.StatusBadRequest},
		// Range is validated before the container is resolved, so a request
		// that is wrong twice reports the range rather than a 404.
		{"unknown container and invalid range", "nope", "99y", http.StatusBadRequest},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			mock := containerFixture()
			mock.queryRangeResponse = &QueryResponse{}
			handler := NewMetricsHandler(mock)
			req := httptest.NewRequest(http.MethodGet, "/metrics/v1/container/"+tt.requested+"/timeseries/"+tt.timeRange, nil)
			req.SetPathValue("name", tt.requested)
			req.SetPathValue("range", tt.timeRange)
			w := httptest.NewRecorder()
			handler.GetContainerTimeSeries(w, req)

			assert.Equal(t, tt.wantStatus, w.Code)
			if tt.wantStatus == http.StatusOK {
				var response TimeSeriesResponse
				require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
				assert.Equal(t, tt.timeRange, response.TimeRange)
				assert.NotNil(t, response.Series, "series is [] not null")
			}
		})
	}
}
