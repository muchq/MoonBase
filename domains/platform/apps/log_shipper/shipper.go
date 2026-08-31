package log_shipper

import (
	"bytes"
	"compress/gzip"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
)

// Uploader is the one seam between shipping logic and S3, so the logic
// tests against a map and the S3 client tests against an HTTP server.
type Uploader interface {
	Put(key string, body []byte) error
}

// Shipper moves Caddy's rolled access logs into the partitioned S3 layout
// the stats pipeline queries (#1457): logs/source=<source>/dt=<date>/<file>.
//
// Only rolled files are touched — the live access.log is Caddy's, and so is
// anything in the directory that does not look like a roll. Deletion is
// strictly the consequence of a successful upload; a failed file stays for
// the next pass.
type Shipper struct {
	Dir      string
	Source   string
	Uploader Uploader
}

// A rolled log as Caddy's roller names it: base-<timestamp>.log, optionally
// .gz when Caddy compressed it. The date group is the partition key — the
// roll time, which is when the last line in the file was written.
var rolledLog = regexp.MustCompile(`^.+-(\d{4}-\d{2}-\d{2})T[\d.\-]+\.log(\.gz)?$`)

// ShipOnce uploads every rolled file currently in Dir and deletes what it
// uploaded. Per-file failures do not stop the rest; they are joined into
// the returned error with the count of files that did ship.
func (s *Shipper) ShipOnce() (int, error) {
	entries, err := os.ReadDir(s.Dir)
	if err != nil {
		return 0, err
	}
	shipped := 0
	var errs []error
	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}
		match := rolledLog.FindStringSubmatch(entry.Name())
		if match == nil {
			continue
		}
		if err := s.shipFile(entry.Name(), match[1], match[2] == ".gz"); err != nil {
			errs = append(errs, fmt.Errorf("%s: %w", entry.Name(), err))
			continue
		}
		shipped++
	}
	return shipped, errors.Join(errs...)
}

func (s *Shipper) shipFile(name, date string, alreadyGzipped bool) error {
	path := filepath.Join(s.Dir, name)
	contents, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	key := "logs/source=" + s.Source + "/dt=" + date + "/" + name
	if !alreadyGzipped {
		var buf bytes.Buffer
		w := gzip.NewWriter(&buf)
		if _, err := w.Write(contents); err != nil {
			return err
		}
		if err := w.Close(); err != nil {
			return err
		}
		contents = buf.Bytes()
		key += ".gz"
	}
	if err := s.Uploader.Put(key, contents); err != nil {
		return err
	}
	return os.Remove(path)
}
