package stats

import (
	"bytes"
	"compress/gzip"
	"errors"
	"strings"
	"testing"
)

const dbipSample = `1.0.0.0,1.0.0.255,AU
1.0.1.0,1.0.3.255,CN
57.141.0.0,57.141.255.255,US
195.178.110.0,195.178.110.255,GB
not,a,row
2001:db8::,2001:db8:ffff:ffff:ffff:ffff:ffff:ffff,DE
9.9.9.9,9.9.9.1,ZZ
10.0.0.0,10.255.255.255,xx
`

func TestGeoPlacesAddressesByRangeAndSaysNothingOutsideThem(t *testing.T) {
	geo, skipped, err := ParseDBIP(strings.NewReader(dbipSample))
	if err != nil {
		t.Fatal(err)
	}
	// The unparseable row and the inverted range are skipped; the lowercase
	// country is a real row, uppercased.
	if skipped != 2 {
		t.Errorf("skipped = %d, want 2", skipped)
	}
	cases := map[string]string{
		"1.0.0.7":         "AU",
		"1.0.0.255":       "AU", // range end is inclusive
		"1.0.1.0":         "CN", // and so is the next start
		"1.0.4.0":         "",   // the gap after CN
		"57.141.3.4":      "US",
		"195.178.110.199": "GB",
		"195.178.111.1":   "",
		"2001:db8::1":     "DE",
		"2001:db9::1":     "",
		"::ffff:1.0.0.7":  "AU", // v4-mapped v6, as some stacks log it
		"10.1.2.3":        "XX",
		"0.0.0.1":         "", // before the first range
		"255.255.255.255": "",
		"not an address":  "",
		"":                "",
		"1.0.0.7 extra":   "",
		"192.168.1.1":     "",
		"2001:db8::":      "DE",
		"9.9.9.5":         "", // the inverted range never loaded
		"1.0.0.7\n":       "",
		"1.0.3.255":       "CN",
		"1.0.0.0":         "AU",
		"1.0.256.1":       "",
		"57.140.255.255":  "",
		"57.142.0.0":      "",
		"195.178.110.0":   "GB",
		"195.178.109.255": "",
		"ffff::1":         "",
		"::1":             "",
		"1.0.0.7%eth0":    "",
		"[1.0.0.7]":       "",
		"01.0.0.7":        "",
		"1.0.0.7/32":      "",
		"1.0.0.7:8080":    "",
	}
	for ip, want := range cases {
		if got := geo.Country(ip); got != want {
			t.Errorf("Country(%q) = %q, want %q", ip, got, want)
		}
	}
	if NoLocator.Country(NoLocator{}, "1.0.0.7") != "" {
		t.Error("NoLocator placed an address")
	}
}

func TestAGeoFileWithNoUsableRowsIsAnError(t *testing.T) {
	if _, _, err := ParseDBIP(strings.NewReader("not,a,row\n")); err == nil {
		t.Error("a database that places nothing loaded without complaint")
	}
	if _, _, err := ParseDBIP(strings.NewReader("")); err == nil {
		t.Error("an empty database loaded without complaint")
	}
}

func TestLoadGeoReadsAGzippedObjectAndReportsAMissingOne(t *testing.T) {
	var buf bytes.Buffer
	w := gzip.NewWriter(&buf)
	if _, err := w.Write([]byte(dbipSample)); err != nil {
		t.Fatal(err)
	}
	if err := w.Close(); err != nil {
		t.Fatal(err)
	}
	objects := &fakeObjects{objects: map[string][]byte{
		"geo/dbip-country-lite.csv.gz": buf.Bytes(),
		"geo/plain.csv":                []byte(dbipSample),
	}, fail: map[string]error{"geo/missing.csv": errors.New("404")}}

	for _, key := range []string{"geo/dbip-country-lite.csv.gz", "geo/plain.csv"} {
		geo, _, err := LoadGeo(objects, key)
		if err != nil {
			t.Fatalf("%s: %v", key, err)
		}
		if geo.Country("57.141.3.4") != "US" {
			t.Errorf("%s loaded but places nothing", key)
		}
	}
	if _, _, err := LoadGeo(objects, "geo/missing.csv"); err == nil {
		t.Error("a missing database loaded without complaint")
	}
	if _, _, err := LoadGeo(&fakeObjects{objects: map[string][]byte{"geo/x.gz": []byte("not gzip")}}, "geo/x.gz"); err == nil {
		t.Error("a .gz that is not gzip loaded without complaint")
	}
}
