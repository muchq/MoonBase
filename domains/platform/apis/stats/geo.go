package stats

import (
	"bufio"
	"compress/gzip"
	"encoding/csv"
	"fmt"
	"io"
	"net/netip"
	"os"
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

// ParseDBIP reads the CSV. Rows that do not parse, and rows that overlap
// an earlier one, are counted and skipped rather than failing the load —
// one bad row in a vendor file must not switch geo off — but a file with
// no usable rows is an error, since that is a wrong object, not a bad
// line. Country codes are interned: the reader allocates a string per
// record, and a two-byte code must not keep its whole line alive.
func ParseDBIP(r io.Reader) (*Geo, int, error) {
	reader := csv.NewReader(bufio.NewReader(r))
	reader.FieldsPerRecord = -1
	reader.ReuseRecord = true
	reader.LazyQuotes = true
	codes := map[string]string{}
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
		if err1 != nil || err2 != nil || len(country) != 2 {
			skipped++
			continue
		}
		// Compared as stored: a v4-mapped v6 end against a v6 start is an
		// inverted range once both are unmapped, whatever they looked like.
		start, end = start.Unmap(), end.Unmap()
		if start.Is4() != end.Is4() || end.Less(start) {
			skipped++
			continue
		}
		interned, ok := codes[country]
		if !ok {
			interned = strings.Clone(country)
			codes[country] = interned
		}
		ranges = append(ranges, ipRange{start: start, end: end, country: interned})
	}
	sort.SliceStable(ranges, func(i, j int) bool { return ranges[i].start.Less(ranges[j].start) })
	// The lookup reads one predecessor, so ranges must not overlap; a row
	// that starts inside the one before it is dropped and counted.
	kept := ranges[:0]
	for _, candidate := range ranges {
		if len(kept) > 0 && !kept[len(kept)-1].end.Less(candidate.start) {
			skipped++
			continue
		}
		kept = append(kept, candidate)
	}
	if len(kept) == 0 {
		return nil, skipped, fmt.Errorf("geo csv holds no usable ranges (%d lines skipped)", skipped)
	}
	return &Geo{ranges: kept}, skipped, nil
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

// LoadGeoFile reads the CSV from disk, gunzipping when the name says so.
// The file rides in the image: DB-IP publishes a new one monthly under
// CC BY 4.0, and muchq.com/stats carries the attribution.
func LoadGeoFile(path string) (*Geo, int, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, 0, fmt.Errorf("opening geo database: %w", err)
	}
	defer file.Close()
	var reader io.Reader = file
	if strings.HasSuffix(path, ".gz") {
		gz, err := gzip.NewReader(file)
		if err != nil {
			return nil, 0, fmt.Errorf("geo database %s is not gzip: %w", path, err)
		}
		defer gz.Close()
		reader = gz
	}
	return ParseDBIP(reader)
}

// Locate is the boot path: an empty path means no database, and a file
// that will not load is reported for the caller to run without — an
// all-"--" table with an error in the log, never a boot failure that
// hides every other table behind it.
func Locate(path string) (Locator, int, error) {
	if path == "" {
		return NoLocator{}, 0, nil
	}
	geo, skipped, err := LoadGeoFile(path)
	if err != nil {
		return NoLocator{}, 0, err
	}
	return geo, skipped, nil
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
