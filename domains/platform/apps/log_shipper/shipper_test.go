package log_shipper

import (
	"bytes"
	"compress/gzip"
	"errors"
	"io"
	"os"
	"path/filepath"
	"testing"
)

type fakeUploader struct {
	puts map[string][]byte
	fail map[string]error
}

func newFakeUploader() *fakeUploader {
	return &fakeUploader{puts: map[string][]byte{}, fail: map[string]error{}}
}

func (f *fakeUploader) Put(key string, body []byte) error {
	if err := f.fail[key]; err != nil {
		return err
	}
	f.puts[key] = body
	return nil
}

func writeFile(t *testing.T, dir, name, contents string) string {
	t.Helper()
	path := filepath.Join(dir, name)
	if err := os.WriteFile(path, []byte(contents), 0o644); err != nil {
		t.Fatal(err)
	}
	return path
}

func gunzip(t *testing.T, data []byte) string {
	t.Helper()
	r, err := gzip.NewReader(bytes.NewReader(data))
	if err != nil {
		t.Fatal(err)
	}
	out, err := io.ReadAll(r)
	if err != nil {
		t.Fatal(err)
	}
	return string(out)
}

func TestShipsARolledFileUnderItsDatePartitionAndDeletesIt(t *testing.T) {
	dir := t.TempDir()
	path := writeFile(t, dir, "access-2026-08-30T12-00-00.000.log", `{"ts":1}`)
	uploader := newFakeUploader()

	shipped, err := (&Shipper{Dir: dir, Source: "caddy", Uploader: uploader}).ShipOnce()

	if err != nil || shipped != 1 {
		t.Fatalf("ShipOnce = (%d, %v), want (1, nil)", shipped, err)
	}
	key := "logs/source=caddy/dt=2026-08-30/access-2026-08-30T12-00-00.000.log.gz"
	body, ok := uploader.puts[key]
	if !ok {
		t.Fatalf("nothing uploaded under %s; got keys %v", key, keys(uploader.puts))
	}
	if got := gunzip(t, body); got != `{"ts":1}` {
		t.Errorf("uploaded body = %q, want the file contents", got)
	}
	if _, err := os.Stat(path); !errors.Is(err, os.ErrNotExist) {
		t.Errorf("rolled file still on disk after upload; the disk never drains")
	}
}

// Caddy compresses rolled files itself when configured to; those are
// uploaded byte-for-byte, not gzipped a second time.
func TestAnAlreadyGzippedRollIsUploadedAsIs(t *testing.T) {
	dir := t.TempDir()
	var buf bytes.Buffer
	w := gzip.NewWriter(&buf)
	if _, err := w.Write([]byte(`{"ts":2}`)); err != nil {
		t.Fatal(err)
	}
	if err := w.Close(); err != nil {
		t.Fatal(err)
	}
	writeFile(t, dir, "access-2026-08-30T13-00-00.000.log.gz", buf.String())
	uploader := newFakeUploader()

	if _, err := (&Shipper{Dir: dir, Source: "caddy", Uploader: uploader}).ShipOnce(); err != nil {
		t.Fatal(err)
	}
	key := "logs/source=caddy/dt=2026-08-30/access-2026-08-30T13-00-00.000.log.gz"
	if got := gunzip(t, uploader.puts[key]); got != `{"ts":2}` {
		t.Errorf("uploaded body = %q, want the original contents exactly once through gzip", got)
	}
}

// The live file is the one Caddy still writes; touching it loses lines.
// Anything else that is not a rolled log (a stray file, a directory) is not
// this program's to delete.
func TestTheLiveLogAndUnrecognizedFilesAreLeftAlone(t *testing.T) {
	dir := t.TempDir()
	writeFile(t, dir, "access.log", "live")
	writeFile(t, dir, "notes.txt", "keep")
	if err := os.Mkdir(filepath.Join(dir, "sub"), 0o755); err != nil {
		t.Fatal(err)
	}
	uploader := newFakeUploader()

	shipped, err := (&Shipper{Dir: dir, Source: "caddy", Uploader: uploader}).ShipOnce()

	if err != nil || shipped != 0 {
		t.Fatalf("ShipOnce = (%d, %v), want (0, nil)", shipped, err)
	}
	for _, name := range []string{"access.log", "notes.txt"} {
		if _, statErr := os.Stat(filepath.Join(dir, name)); statErr != nil {
			t.Errorf("%s was touched: %v", name, statErr)
		}
	}
}

// A failed upload must leave the file for the next pass — deletion is only
// ever the consequence of a 200 — and must not stop the other files from
// shipping.
func TestAFailedUploadKeepsTheFileAndTheRestStillShip(t *testing.T) {
	dir := t.TempDir()
	failing := writeFile(t, dir, "access-2026-08-30T14-00-00.000.log", "a")
	writeFile(t, dir, "access-2026-08-30T15-00-00.000.log", "b")
	uploader := newFakeUploader()
	uploader.fail["logs/source=caddy/dt=2026-08-30/access-2026-08-30T14-00-00.000.log.gz"] =
		errors.New("s3 said 500")

	shipped, err := (&Shipper{Dir: dir, Source: "caddy", Uploader: uploader}).ShipOnce()

	if shipped != 1 {
		t.Errorf("shipped = %d, want the non-failing file to have gone through", shipped)
	}
	if err == nil {
		t.Error("ShipOnce reported success while an upload failed")
	}
	if _, statErr := os.Stat(failing); statErr != nil {
		t.Errorf("failed upload's file was deleted; that data is gone: %v", statErr)
	}
}

func keys(m map[string][]byte) []string {
	var out []string
	for k := range m {
		out = append(out, k)
	}
	return out
}
