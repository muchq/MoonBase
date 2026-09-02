package stats

import (
	"fmt"
	"strings"
)

// Lines as logback's JsonEncoder writes them for one_d4's query event
// (LogbackConfigTest pins the shape): kvpList is a list of one-entry
// objects, every value a string.
func eventLine(pairs ...string) string {
	var kvps []string
	for i := 0; i+1 < len(pairs); i += 2 {
		kvps = append(kvps, fmt.Sprintf(`{"%s":"%s"}`, pairs[i], pairs[i+1]))
	}
	return `{"level":"INFO","loggerName":"com.muchq.games.one_d4.query_event","message":"query_event","kvpList":[` +
		strings.Join(kvps, ",") + `]}`
}
