package clock

import "time"

type Clock interface {
	Now() time.Time
}

type SystemUtcClock struct {
}

// Now implements the Clock interface
func (*SystemUtcClock) Now() time.Time {
	return time.Now().UTC()
}

func NewSystemUtcClock() Clock {
	return &SystemUtcClock{}
}

// TestClock is a Clock implementation for use in tests
type TestClock struct {
	unixSeconds int64
}

// Now implements the Clock interface
func (c *TestClock) Now() time.Time {
	return time.Unix(c.unixSeconds, 0)
}

func (c *TestClock) Tick(secs int64) {
	c.unixSeconds += secs
}

func NewTestClock() *TestClock {
	return &TestClock{}
}
