package clock

import (
	"github.com/stretchr/testify/assert"
	"testing"
)

func TestTestClock(t *testing.T) {
	testClock := &TestClock{}
	assert.True(t, testClock.Now().Unix() == 0, "initialized test clock at zero")

	testClock.Tick(100)
	assert.True(t, testClock.Now().Unix() == 100, "test clock ticked 100 seconds")
}
