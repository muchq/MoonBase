package stats

import (
	"archive/tar"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// The pinned DB-IP file through the real parser: the fixture in geo_test.go
// says what the format is, this says the vendor still agrees. Data deps
// resolve relative to the runfiles root.
func TestThePinnedVendorFileLoadsAndPlacesKnownAddresses(t *testing.T) {
	geo, skipped, err := LoadGeoFile(vendorFile(t))
	if err != nil {
		t.Fatal(err)
	}
	// The file has hundreds of thousands of ranges and no bad rows. Both
	// bounds are loose on purpose: a new month moves the count, a parser
	// that silently drops half the file does not.
	if len(geo.ranges) < 100_000 {
		t.Errorf("only %d ranges loaded from the vendor file", len(geo.ranges))
	}
	if skipped > 100 {
		t.Errorf("%d rows of the vendor file were skipped; the format has drifted from what ParseDBIP reads", skipped)
	}
	// Meta's crawler range from the git.muchq.com guard, and a Google
	// resolver: allocations that have been where they are for years.
	for ip, want := range map[string]string{"57.141.3.4": "US", "8.8.8.8": "US"} {
		if got := geo.Country(ip); got != want {
			t.Errorf("Country(%s) = %q, want %q", ip, got, want)
		}
	}
	if geo.Country("10.1.2.3") != "" {
		t.Error("a private address was placed in a country")
	}
}

// The image lays the file where main.go's default looks: one string, pinned
// from both sides through the layer tar the image is built from.
func TestTheImageLayerPutsTheFileWhereTheBinaryLooks(t *testing.T) {
	file, err := os.Open(layerTar(t))
	if err != nil {
		t.Fatal(err)
	}
	defer file.Close()
	reader := tar.NewReader(file)
	var names []string
	for {
		header, err := reader.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			t.Fatal(err)
		}
		names = append(names, "/"+strings.TrimPrefix(header.Name, "./"))
	}
	for _, name := range names {
		if name == DefaultGeoDBPath {
			return
		}
	}
	t.Errorf("geo_layer holds %v; the binary reads %s", names, DefaultGeoDBPath)
}

func vendorFile(t *testing.T) string {
	t.Helper()
	return runfile(t, "DBIP_COUNTRY_LITE")
}

func layerTar(t *testing.T) string {
	t.Helper()
	return runfile(t, "GEO_LAYER_TAR")
}

// Bazel passes data files by rootpath in env (BUILD.bazel's go_test env):
// relative to the runfiles root, while the test runs in its package
// directory, so they are joined back through TEST_SRCDIR.
func runfile(t *testing.T, name string) string {
	t.Helper()
	path := os.Getenv(name)
	if path == "" {
		t.Skipf("%s not set; run under bazel test", name)
	}
	return filepath.Join(os.Getenv("TEST_SRCDIR"), os.Getenv("TEST_WORKSPACE"), path)
}
