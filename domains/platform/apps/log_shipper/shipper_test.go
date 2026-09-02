package log_shipper

import (
	"bytes"
	"compress/gzip"
	"errors"
	"io"
	"os"
	"path/filepath"
	"testing"
	"time"
)

type fakeUploader struct {
	puts map[string][]byte
	fail map[string]error
}

func newFakeUploader() *fakeUploader {
	return &fakeUploader{puts: map[string][]byte{}, fail: map[string]error{}}
}

func (f *fakeUploader) Put(key string, body io.ReadSeeker, size int64) error {
	if err := f.fail[key]; err != nil {
		return err
	}
	data, err := io.ReadAll(body)
	if err != nil {
		return err
	}
	if int64(len(data)) != size {
		return errors.New("declared size does not match the body")
	}
	f.puts[key] = data
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

func gzipped(t *testing.T, contents string) string {
	t.Helper()
	var buf bytes.Buffer
	w := gzip.NewWriter(&buf)
	if _, err := w.Write([]byte(contents)); err != nil {
		t.Fatal(err)
	}
	if err := w.Close(); err != nil {
		t.Fatal(err)
	}
	return buf.String()
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

	shipped, _, err := (&Shipper{Dir: dir, Source: "caddy", Uploader: uploader}).ShipOnce()

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
	writeFile(t, dir, "access-2026-08-30T13-00-00.000.log.gz", gzipped(t, `{"ts":2}`))
	uploader := newFakeUploader()

	if _, _, err := (&Shipper{Dir: dir, Source: "caddy", Uploader: uploader}).ShipOnce(); err != nil {
		t.Fatal(err)
	}
	key := "logs/source=caddy/dt=2026-08-30/access-2026-08-30T13-00-00.000.log.gz"
	if got := gunzip(t, uploader.puts[key]); got != `{"ts":2}` {
		t.Errorf("uploaded body = %q, want the original contents exactly once through gzip", got)
	}
}

// The live file is the one Caddy still writes; touching it loses lines.
// Anything else that is not a rolled log (a stray file, a directory) is not
// this program's to delete. Every file left without an upload attempt is
// counted, so a pass that recognizes nothing in a non-empty directory is
// visible in the logs rather than indistinguishable from an empty one.
func TestTheLiveLogAndUnrecognizedFilesAreLeftAloneButCounted(t *testing.T) {
	dir := t.TempDir()
	writeFile(t, dir, "access.log", "live")
	writeFile(t, dir, "notes.txt", "keep")
	if err := os.Mkdir(filepath.Join(dir, "sub"), 0o755); err != nil {
		t.Fatal(err)
	}
	uploader := newFakeUploader()

	shipped, skipped, err := (&Shipper{Dir: dir, Source: "caddy", Uploader: uploader}).ShipOnce()

	if err != nil || shipped != 0 {
		t.Fatalf("ShipOnce = (%d, %v), want (0, nil)", shipped, err)
	}
	if skipped != 2 {
		t.Errorf("skipped = %d, want 2: the live log and the stray file, not the directory", skipped)
	}
	for _, name := range []string{"access.log", "notes.txt"} {
		if _, statErr := os.Stat(filepath.Join(dir, name)); statErr != nil {
			t.Errorf("%s was touched: %v", name, statErr)
		}
	}
}

// Caddy ≥2.11 rolls with timberjack, which always appends the roll reason
// to the backup name: access-<timestamp>-size.log, -time, or a custom
// sanitized word. These are the names production actually writes.
func TestARollWithARotationReasonSuffixShips(t *testing.T) {
	dir := t.TempDir()
	writeFile(t, dir, "access-2026-08-31T18-14-52.509-size.log.gz", gzipped(t, `{"ts":3}`))
	writeFile(t, dir, "access-2026-09-01T00-00-00.000-time.log", `{"ts":4}`)
	// The reason is any \w word, not an enumeration — sanitized custom
	// reasons reach filenames via RotateWithReason.
	writeFile(t, dir, "access-2026-08-31T19-00-00.000-sig_hup1.log", `{"ts":5}`)
	// Roll-shaped but not a roll: a matched name gets DELETED after upload,
	// so the boundary must not widen past one reason word.
	writeFile(t, dir, "access-2026-08-31T18-14-52.509-size.extra.log", "not ours")
	uploader := newFakeUploader()

	shipped, skipped, err := (&Shipper{Dir: dir, Source: "caddy", Uploader: uploader}).ShipOnce()

	if err != nil || shipped != 3 || skipped != 1 {
		t.Fatalf("ShipOnce = (%d, %d, %v), want the three reason-suffixed rolls shipped and the near-miss skipped",
			shipped, skipped, err)
	}
	sized := "logs/source=caddy/dt=2026-08-31/access-2026-08-31T18-14-52.509-size.log.gz"
	if got := gunzip(t, uploader.puts[sized]); got != `{"ts":3}` {
		t.Errorf("size-triggered roll uploaded as %q; got keys %v", got, keys(uploader.puts))
	}
	timed := "logs/source=caddy/dt=2026-09-01/access-2026-09-01T00-00-00.000-time.log.gz"
	if got := gunzip(t, uploader.puts[timed]); got != `{"ts":4}` {
		t.Errorf("time-triggered roll uploaded as %q; got keys %v", got, keys(uploader.puts))
	}
	custom := "logs/source=caddy/dt=2026-08-31/access-2026-08-31T19-00-00.000-sig_hup1.log.gz"
	if got := gunzip(t, uploader.puts[custom]); got != `{"ts":5}` {
		t.Errorf("custom-reason roll uploaded as %q; got keys %v", got, keys(uploader.puts))
	}
	if _, statErr := os.Stat(filepath.Join(dir, "access-2026-08-31T18-14-52.509-size.extra.log")); statErr != nil {
		t.Errorf("the near-miss file was touched: %v", statErr)
	}
}

// A failed upload must leave the file for the next pass — deletion is only
// ever the consequence of a 200 — and must not stop the other files from
// shipping.
// logback rolls one_d4's query events as query_events-<date>T<hour>.log.gz
// (the shared logback.xml); the name is shaped like Caddy's rolls on purpose,
// so the same shipper moves them under the hour's date.
func TestALogbackHourlyRollShipsUnderItsDate(t *testing.T) {
	dir := t.TempDir()
	writeFile(t, dir, "query_events-2026-09-01T14.log.gz", gzipped(t, `{"message":"query_event"}`))
	writeFile(t, dir, "query_events.log", "live\n")
	uploader := newFakeUploader()

	shipped, skipped, err := (&Shipper{Dir: dir, Source: "one_d4", Uploader: uploader}).ShipOnce()

	if err != nil || shipped != 1 || skipped != 1 {
		t.Fatalf("ShipOnce = (%d, %d, %v); want the roll shipped and the live file skipped",
			shipped, skipped, err)
	}
	key := "logs/source=one_d4/dt=2026-09-01/query_events-2026-09-01T14.log.gz"
	if got := gunzip(t, uploader.puts[key]); got != `{"message":"query_event"}` {
		t.Errorf("uploaded under %v, want %s with the roll's contents", keys(uploader.puts), key)
	}
}

func TestAFailedUploadKeepsTheFileAndTheRestStillShip(t *testing.T) {
	dir := t.TempDir()
	failing := writeFile(t, dir, "access-2026-08-30T14-00-00.000.log", "a")
	writeFile(t, dir, "access-2026-08-30T15-00-00.000.log", "b")
	uploader := newFakeUploader()
	uploader.fail["logs/source=caddy/dt=2026-08-30/access-2026-08-30T14-00-00.000.log.gz"] =
		errors.New("s3 said 500")

	shipped, skipped, err := (&Shipper{Dir: dir, Source: "caddy", Uploader: uploader}).ShipOnce()

	if shipped != 1 {
		t.Errorf("shipped = %d, want the non-failing file to have gone through", shipped)
	}
	// A failure is not a skip: skipped is the operator's unrecognized-name
	// signal, and failures already surface through the returned error.
	if skipped != 0 {
		t.Errorf("skipped = %d, want 0; a failed upload is reported by the error, not the skip count", skipped)
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

// Caddy compresses a finished roll in place: while the compressor runs, the
// complete .log and a truncated .gz coexist and would ship to the SAME key,
// the corrupt one second — silently replacing the good object before both
// files are deleted. A .gz whose .log sibling still exists is therefore not
// finished, and is not touched.
func TestAGzWhoseLogSiblingStillExistsIsSkipped(t *testing.T) {
	dir := t.TempDir()
	writeFile(t, dir, "access-2026-08-30T16-00-00.000.log", "complete")
	writeFile(t, dir, "access-2026-08-30T16-00-00.000.log.gz", "truncated garbage")
	uploader := newFakeUploader()

	shipped, skipped, err := (&Shipper{Dir: dir, Source: "caddy", Uploader: uploader}).ShipOnce()

	if err != nil || shipped != 1 || skipped != 1 {
		t.Fatalf("ShipOnce = (%d, %d, %v), want only the .log to ship and the .gz counted as skipped",
			shipped, skipped, err)
	}
	key := "logs/source=caddy/dt=2026-08-30/access-2026-08-30T16-00-00.000.log.gz"
	if got := gunzip(t, uploader.puts[key]); got != "complete" {
		t.Errorf("the key holds %q; the mid-compression .gz must never reach it", got)
	}
	if _, statErr := os.Stat(filepath.Join(dir, "access-2026-08-30T16-00-00.000.log.gz")); statErr != nil {
		t.Errorf("the in-progress .gz was touched: %v", statErr)
	}
}

// .shipping-*.gztmp is this program's own temp file, orphaned only by a
// crash mid-pass. Left alone it would inflate the skipped count on every
// pass forever, drifting the baseline the README's monitoring signal
// assumes; a quiet one is junk and gets removed.
func TestAnOrphanedTempFileIsSweptAndNotCounted(t *testing.T) {
	dir := t.TempDir()
	orphan := writeFile(t, dir, ".shipping-42.gztmp", "half a gzip")
	uploader := newFakeUploader()

	shipped, skipped, err := (&Shipper{Dir: dir, Source: "caddy", Uploader: uploader}).ShipOnce()

	if err != nil || shipped != 0 || skipped != 0 {
		t.Fatalf("ShipOnce = (%d, %d, %v), want the orphan in neither count", shipped, skipped, err)
	}
	if _, statErr := os.Stat(orphan); !errors.Is(statErr, os.ErrNotExist) {
		t.Error("the orphaned temp file was not swept")
	}
}

// A temp file younger than MinAge may belong to a pass that is still
// running in another process lifetime (a restart mid-gzip); it is left for
// a later pass to sweep.
func TestAFreshTempFileIsNotSwept(t *testing.T) {
	dir := t.TempDir()
	fresh := writeFile(t, dir, ".shipping-43.gztmp", "still growing")
	uploader := newFakeUploader()

	_, skipped, err := (&Shipper{Dir: dir, Source: "caddy", Uploader: uploader, MinAge: time.Minute}).ShipOnce()

	if err != nil || skipped != 0 {
		t.Fatalf("ShipOnce = (_, %d, %v), want the fresh temp uncounted and no error", skipped, err)
	}
	if _, statErr := os.Stat(fresh); statErr != nil {
		t.Errorf("the fresh temp file was touched: %v", statErr)
	}
}

// A stat failure that is not "file already gone" must surface in the
// returned error. Counting it as skipped would make a permission drift on
// the mount read exactly like unrecognized roll names, steering diagnosis
// the wrong way.
func TestAStatFailureIsAnErrorNotASkip(t *testing.T) {
	if os.Geteuid() == 0 {
		t.Skip("root ignores directory permissions")
	}
	dir := t.TempDir()
	writeFile(t, dir, "access-2026-08-30T18-00-00.000-size.log", "data")
	if err := os.Chmod(dir, 0o600); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { os.Chmod(dir, 0o755) })
	uploader := newFakeUploader()

	s := &Shipper{Dir: dir, Source: "caddy", Uploader: uploader, MinAge: time.Minute}
	shipped, skipped, err := s.ShipOnce()

	if shipped != 0 || skipped != 0 {
		t.Errorf("ShipOnce counts = (%d, %d), want the unstattable file in neither bucket", shipped, skipped)
	}
	if err == nil {
		t.Error("a stat failure other than not-exist was silently absorbed")
	}
}

// A file younger than MinAge may still be being written; it waits for the
// next pass.
func TestAFileYoungerThanMinAgeIsLeftForTheNextPass(t *testing.T) {
	dir := t.TempDir()
	path := writeFile(t, dir, "access-2026-08-30T17-00-00.000.log", "fresh")
	uploader := newFakeUploader()

	s := &Shipper{Dir: dir, Source: "caddy", Uploader: uploader, MinAge: time.Minute}
	shipped, skipped, err := s.ShipOnce()

	if err != nil || shipped != 0 || skipped != 1 {
		t.Fatalf("ShipOnce = (%d, %d, %v), want the fresh file skipped and counted", shipped, skipped, err)
	}
	if err := os.Chtimes(path, time.Now(), time.Now().Add(-2*time.Minute)); err != nil {
		t.Fatal(err)
	}
	if shipped, _, err = s.ShipOnce(); err != nil || shipped != 1 {
		t.Errorf("ShipOnce after aging = (%d, %v), want it shipped", shipped, err)
	}
}

func TestSourcesParseAsLabelledDirectoriesAndRejectTheAmbiguous(t *testing.T) {
	sources, err := ParseSources(" caddy=/var/log/caddy, one_d4=/var/log/one_d4 ")
	if err != nil {
		t.Fatal(err)
	}
	want := []Source{{"caddy", "/var/log/caddy"}, {"one_d4", "/var/log/one_d4"}}
	if len(sources) != 2 || sources[0] != want[0] || sources[1] != want[1] {
		t.Errorf("ParseSources = %v, want %v", sources, want)
	}
	for _, bad := range []string{"", "/var/log/caddy", "caddy=", "=/var/log/caddy",
		"caddy=/a,caddy=/b", "a/b=/var/log/x"} {
		if _, err := ParseSources(bad); err == nil {
			t.Errorf("ParseSources(%q) accepted; a wrong partition label is a silent misfile", bad)
		}
	}
}
