package prom_proxy

import (
	"context"
	"log"
	"sync"
	"time"
)

// CacheEntry represents a cached response with timestamp
type CacheEntry[T any] struct {
	Data      T
	Timestamp time.Time
}

// MetricsCache holds all cached responses
type MetricsCache struct {
	mu sync.RWMutex

	// Point-in-time metrics
	systemMetrics   *CacheEntry[*SystemMetrics]
	portraitMetrics *CacheEntry[*PortraitMetrics]

	// Timeseries data for each range
	systemTimeseries   map[TimeRange]*CacheEntry[*TimeSeriesResponse]
	portraitTimeseries map[TimeRange]*CacheEntry[*TimeSeriesResponse]

	// Background refresh control
	ctx       context.Context
	cancel    context.CancelFunc
	promClient PrometheusQuerier
	refreshInterval time.Duration
}

// NewMetricsCache creates a new cache instance and starts background refresh
func NewMetricsCache(promClient PrometheusQuerier, refreshInterval time.Duration) *MetricsCache {
	ctx, cancel := context.WithCancel(context.Background())
	
	cache := &MetricsCache{
		systemTimeseries:   make(map[TimeRange]*CacheEntry[*TimeSeriesResponse]),
		portraitTimeseries: make(map[TimeRange]*CacheEntry[*TimeSeriesResponse]),
		ctx:               ctx,
		cancel:            cancel,
		promClient:        promClient,
		refreshInterval:   refreshInterval,
	}

	// Initialize timeseries maps for all valid time ranges
	for _, tr := range []TimeRange{Last30Minutes, LastDay, LastWeek} {
		cache.systemTimeseries[tr] = nil
		cache.portraitTimeseries[tr] = nil
	}

	// Start background refresh goroutine
	go cache.refreshLoop()

	return cache
}

// Close stops the background refresh goroutine
func (c *MetricsCache) Close() {
	c.cancel()
}

// refreshLoop runs background cache updates
func (c *MetricsCache) refreshLoop() {
	// Initial population
	c.refreshAll()

	ticker := time.NewTicker(c.refreshInterval)
	defer ticker.Stop()

	for {
		select {
		case <-c.ctx.Done():
			return
		case <-ticker.C:
			c.refreshAll()
		}
	}
}

// refreshAll updates all cache entries
func (c *MetricsCache) refreshAll() {
	log.Printf("Refreshing metrics cache...")
	start := time.Now()

	// Create a context with timeout for all operations
	ctx, cancel := context.WithTimeout(c.ctx, 30*time.Second)
	defer cancel()

	// Create a temporary handler to use existing logic
	handler := &MetricsHandler{promClient: c.promClient}

	// Refresh point-in-time metrics concurrently
	var wg sync.WaitGroup

	// System metrics
	wg.Add(1)
	go func() {
		defer wg.Done()
		if systemMetrics, err := handler.fetchSystemMetrics(ctx); err == nil {
			c.setSystemMetrics(systemMetrics)
		} else {
			log.Printf("Failed to refresh system metrics: %v", err)
		}
	}()

	// Portrait metrics
	wg.Add(1)
	go func() {
		defer wg.Done()
		if portraitMetrics, err := handler.fetchPortraitMetrics(ctx); err == nil {
			c.setPortraitMetrics(portraitMetrics)
		} else {
			log.Printf("Failed to refresh portrait metrics: %v", err)
		}
	}()

	// System timeseries for each range
	for _, tr := range []TimeRange{Last30Minutes, LastDay, LastWeek} {
		wg.Add(1)
		go func(timeRange TimeRange) {
			defer wg.Done()
			if tsResponse, err := handler.fetchSystemMetricsTimeSeries(ctx, timeRange); err == nil {
				c.setSystemTimeseries(timeRange, tsResponse)
			} else {
				log.Printf("Failed to refresh system timeseries for %s: %v", timeRange, err)
			}
		}(tr)
	}

	// Portrait timeseries for each range
	for _, tr := range []TimeRange{Last30Minutes, LastDay, LastWeek} {
		wg.Add(1)
		go func(timeRange TimeRange) {
			defer wg.Done()
			if tsResponse, err := handler.fetchPortraitMetricsTimeSeries(ctx, timeRange); err == nil {
				c.setPortraitTimeseries(timeRange, tsResponse)
			} else {
				log.Printf("Failed to refresh portrait timeseries for %s: %v", timeRange, err)
			}
		}(tr)
	}

	wg.Wait()
	log.Printf("Cache refresh completed in %v", time.Since(start))
}

// Getters with read locks
func (c *MetricsCache) GetSystemMetrics() *SystemMetrics {
	c.mu.RLock()
	defer c.mu.RUnlock()
	if c.systemMetrics != nil {
		return c.systemMetrics.Data
	}
	return nil
}

func (c *MetricsCache) GetPortraitMetrics() *PortraitMetrics {
	c.mu.RLock()
	defer c.mu.RUnlock()
	if c.portraitMetrics != nil {
		return c.portraitMetrics.Data
	}
	return nil
}

func (c *MetricsCache) GetSystemTimeseries(timeRange TimeRange) *TimeSeriesResponse {
	c.mu.RLock()
	defer c.mu.RUnlock()
	if entry := c.systemTimeseries[timeRange]; entry != nil {
		return entry.Data
	}
	return nil
}

func (c *MetricsCache) GetPortraitTimeseries(timeRange TimeRange) *TimeSeriesResponse {
	c.mu.RLock()
	defer c.mu.RUnlock()
	if entry := c.portraitTimeseries[timeRange]; entry != nil {
		return entry.Data
	}
	return nil
}

// Setters with write locks
func (c *MetricsCache) setSystemMetrics(data *SystemMetrics) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.systemMetrics = &CacheEntry[*SystemMetrics]{
		Data:      data,
		Timestamp: time.Now(),
	}
}

func (c *MetricsCache) setPortraitMetrics(data *PortraitMetrics) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.portraitMetrics = &CacheEntry[*PortraitMetrics]{
		Data:      data,
		Timestamp: time.Now(),
	}
}

func (c *MetricsCache) setSystemTimeseries(timeRange TimeRange, data *TimeSeriesResponse) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.systemTimeseries[timeRange] = &CacheEntry[*TimeSeriesResponse]{
		Data:      data,
		Timestamp: time.Now(),
	}
}

func (c *MetricsCache) setPortraitTimeseries(timeRange TimeRange, data *TimeSeriesResponse) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.portraitTimeseries[timeRange] = &CacheEntry[*TimeSeriesResponse]{
		Data:      data,
		Timestamp: time.Now(),
	}
}

// GetCacheInfo returns information about cache status
func (c *MetricsCache) GetCacheInfo() map[string]interface{} {
	c.mu.RLock()
	defer c.mu.RUnlock()

	info := make(map[string]interface{})
	
	if c.systemMetrics != nil {
		info["system_metrics_cached_at"] = c.systemMetrics.Timestamp
	}
	if c.portraitMetrics != nil {
		info["portrait_metrics_cached_at"] = c.portraitMetrics.Timestamp
	}

	systemTS := make(map[string]interface{})
	for tr, entry := range c.systemTimeseries {
		if entry != nil {
			systemTS[string(tr)] = entry.Timestamp
		}
	}
	info["system_timeseries"] = systemTS

	portraitTS := make(map[string]interface{})
	for tr, entry := range c.portraitTimeseries {
		if entry != nil {
			portraitTS[string(tr)] = entry.Timestamp
		}
	}
	info["portrait_timeseries"] = portraitTS

	return info
}