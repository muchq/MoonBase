package stats

import (
	"bufio"
	"compress/gzip"
	"encoding/csv"
	"fmt"
	"io"
	"net/netip"
	"sort"
	"strings"
)

// Locator answers which country an address is in, as an ISO 3166-1 alpha-2
// code, or "" when it does not know. The rollup keys geo rows on the
// answer, so the vocabulary is the ~250 codes plus UnknownCountry.
type Locator interface {
	Country(ip string) string
}

// UnknownCountry is the geo row for an address no locator placed: a private
// range, a malformed field, or no database loaded at all.
const UnknownCountry = "--"

// NoLocator places nothing; the geo rows all read UnknownCountry.
type NoLocator struct{}

func (NoLocator) Country(string) string { return "" }

// Geo is an in-memory range table built from DB-IP's country CSV
// (dbip-country-lite: start,end,country per line, IPv4 and IPv6 mixed).
// Some hundreds of thousands of ranges, binary-searched; no database
// library, because a sorted slice is the whole data structure.
type Geo struct {
	ranges []ipRange // sorted by start, non-overlapping
}

type ipRange struct {
	start, end netip.Addr
	country    string
}

// ParseDBIP reads the CSV. Lines that do not parse are counted and
// skipped rather than failing the load — one bad row in a vendor file
// must not switch geo off — but a file with no usable rows is an error,
// since that is a wrong object, not a bad line.
func ParseDBIP(r io.Reader) (*Geo, int, error) {
	reader := csv.NewReader(bufio.NewReader(r))
	reader.FieldsPerRecord = -1
	reader.ReuseRecord = true
	var ranges []ipRange
	skipped := 0
	for {
		record, err := reader.Read()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, skipped, fmt.Errorf("reading geo csv: %w", err)
		}
		if len(record) < 3 {
			skipped++
			continue
		}
		start, err1 := netip.ParseAddr(strings.TrimSpace(record[0]))
		end, err2 := netip.ParseAddr(strings.TrimSpace(record[1]))
		country := strings.ToUpper(strings.TrimSpace(record[2]))
		if err1 != nil || err2 != nil || len(country) != 2 || start.Is4() != end.Is4() || end.Less(start) {
			skipped++
			continue
		}
		ranges = append(ranges, ipRange{start: start.Unmap(), end: end.Unmap(), country: country})
	}
	if len(ranges) == 0 {
		return nil, skipped, fmt.Errorf("geo csv holds no usable ranges (%d lines skipped)", skipped)
	}
	sort.Slice(ranges, func(i, j int) bool { return ranges[i].start.Less(ranges[j].start) })
	return &Geo{ranges: ranges}, skipped, nil
}

// Country finds the range holding ip. Addresses come from Caddy's log as
// text; anything that does not parse is "", like an address in no range.
func (g *Geo) Country(ip string) string {
	addr, err := netip.ParseAddr(ip)
	if err != nil {
		return ""
	}
	addr = addr.Unmap()
	// The first range starting after addr; the candidate is the one before it.
	// netip orders every IPv4 address before every IPv6 one, so a v6 address
	// whose predecessor is a v4 range fails the end check like any other gap.
	i := sort.Search(len(g.ranges), func(i int) bool { return addr.Less(g.ranges[i].start) })
	if i == 0 {
		return ""
	}
	candidate := g.ranges[i-1]
	if candidate.end.Less(addr) {
		return ""
	}
	return candidate.country
}

// LoadGeo reads the CSV object (gzipped when the key says so) from the
// bucket the logs live in. The key is the operator's: DB-IP publishes a
// new file monthly under CC BY 4.0, and the dashboard carries the
// attribution.
func LoadGeo(objects ObjectStore, key string) (*Geo, int, error) {
	body, err := objects.Get(key)
	if err != nil {
		return nil, 0, fmt.Errorf("fetching geo database %s: %w", key, err)
	}
	defer body.Close()
	var reader io.Reader = body
	if strings.HasSuffix(key, ".gz") {
		gz, err := gzip.NewReader(body)
		if err != nil {
			return nil, 0, fmt.Errorf("geo database %s is not gzip: %w", key, err)
		}
		defer gz.Close()
		reader = gz
	}
	return ParseDBIP(reader)
}

// GeoKey is one row of the per-day geo rollup: where a host's traffic of
// each class came from. Bounded by the country vocabulary times the four
// classes.
type GeoKey struct {
	Date       string
	Host       string
	AgentClass string
	Country    string
}

// GeoStat is what a GeoKey accumulates: requests, how many were refused
// (403), and how many were scanner probes — the three columns the "where
// does junk traffic come from" question reads.
type GeoStat struct {
	Requests int64
	Blocked  int64
	Probes   int64
}

func (r *Rollup) addGeo(date, host, agentClass, ip string, status int, probed bool) {
	country := r.Geo.Country(ip)
	if country == "" {
		country = UnknownCountry
	}
	key := GeoKey{Date: date, Host: host, AgentClass: agentClass, Country: country}
	stat := r.Countries[key]
	stat.Requests++
	if status == 403 {
		stat.Blocked++
	}
	if probed {
		stat.Probes++
	}
	r.Countries[key] = stat
}
