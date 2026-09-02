package stats

import (
	"bytes"
	"compress/gzip"
	"errors"
	"strings"
	"testing"
)

// Rows in the shape of dbip-country-lite-YYYY-MM.csv (start,end,country,
// no header, v4 and v6 mixed; the sample addresses are made up), plus the
// malformed rows a vendor file could carry: short, unparseable, a name
// rather than a code, a v4/v6 pair, inverted, overlapping, and one with
// a trailing field, which loads.
const dbipSample = `1.0.0.0,1.0.0.255,AU
1.0.1.0,1.0.3.255,CN
57.141.0.0,57.141.255.255,US
195.178.110.0,195.178.110.255,GB
not,a,row
1.2.3.0,1.2.3.255
2.0.0.0,2.0.0.255,United States
3.0.0.0,2001:db8::,FR
2001:db8::,2001:db8:ffff:ffff:ffff:ffff:ffff:ffff,DE
9.9.9.9,9.9.9.1,ZZ
1.0.2.0,1.0.2.255,NZ
4.0.0.0,4.0.0.255,FR,extra
10.0.0.0,10.255.255.255,xx
`

func TestGeoPlacesAddressesByRangeAndSaysNothingOutsideThem(t *testing.T) {
	geo, skipped, err := ParseDBIP(strings.NewReader(dbipSample))
	if err != nil {
		t.Fatal(err)
	}
	// Six rows skipped: unparseable, short, a country name, a v4/v6 pair, an
	// inverted range, and the NZ row nested inside CN's. The lowercase
	// country and the row with a trailing field are real rows.
	if skipped != 6 {
		t.Errorf("skipped = %d, want 6", skipped)
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
		"4.0.0.7":         "FR", // the trailing field is ignored
		"2.0.0.7":         "",   // a name is not a code
		"3.0.0.7":         "",   // a v4/v6 pair never loaded
		"1.0.2.7":         "CN", // the overlapping NZ row lost to the range it sits in
		"1.0.3.0":         "CN", // and the rest of CN's range is not lost with it
		"0.0.0.1":         "",   // before the first range
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
		geo, skipped, err := LoadGeo(objects, key)
		if err != nil {
			t.Fatalf("%s: %v", key, err)
		}
		if geo.Country("57.141.3.4") != "US" || skipped != 6 {
			t.Errorf("%s loaded but places nothing, or lost the skip count (%d)", key, skipped)
		}
	}
	if _, _, err := LoadGeo(objects, "geo/missing.csv"); err == nil {
		t.Error("a missing database loaded without complaint")
	}
	if _, _, err := LoadGeo(&fakeObjects{objects: map[string][]byte{"geo/x.gz": []byte("not gzip")}}, "geo/x.gz"); err == nil {
		t.Error("a .gz that is not gzip loaded without complaint")
	}
}

// A quoted field with a stray quote is a bad row, not a dead database.
func TestAStrayQuoteIsOneSkippedRowNotAFailedLoad(t *testing.T) {
	geo, skipped, err := ParseDBIP(strings.NewReader("1.0.0.0,1.0.0.255,AU\n\"1.0.1.0,1.0.1.255,\"C\"N\n"))
	if err != nil || skipped != 1 || geo.Country("1.0.0.7") != "AU" {
		t.Errorf("ParseDBIP = (%v, %d, %v); want the good row kept and the bad one counted", geo != nil, skipped, err)
	}
}

func TestLocateNeverFailsABootAndRetriesABucketThatIsNotThereYet(t *testing.T) {
	geo, _, err := Locate(&fakeObjects{}, "", 3, func() {})
	if _, none := geo.(NoLocator); !none || err != nil {
		t.Errorf("Locate with no key = (%T, %v), want NoLocator and no error", geo, err)
	}

	objects := &fakeObjects{objects: map[string][]byte{"geo/plain.csv": []byte(dbipSample)},
		fail: map[string]error{"geo/plain.csv": errors.New("503 slow down")}}
	waits := 0
	// The bucket answers on the third try.
	geo, _, err = Locate(objects, "geo/plain.csv", 5, func() {
		waits++
		if waits == 2 {
			delete(objects.fail, "geo/plain.csv")
		}
	})
	if err != nil || geo.Country("57.141.3.4") != "US" || waits != 2 {
		t.Errorf("Locate after a flaky bucket = (%v, %v, %d waits); want the database on the third try", geo, err, waits)
	}

	geo, _, err = Locate(&fakeObjects{fail: map[string]error{"geo/missing.csv": errors.New("404")}},
		"geo/missing.csv", 3, func() {})
	if _, none := geo.(NoLocator); !none || err == nil {
		t.Errorf("Locate with a key that never loads = (%T, %v); want NoLocator and the error to log", geo, err)
	}
}
