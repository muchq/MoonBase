package log_shipper

import (
	"compress/gzip"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"time"
)

// Uploader is the one seam between shipping logic and S3, so the logic
// tests against a map and the S3 client tests against an HTTP server. The
// body is a ReadSeeker, not a byte slice: a roll is up to a gigabyte and
// the container's memory cap is a fraction of that, so nothing in this
// package may hold a whole file in memory. Seekable because signing needs
// one pass for the payload hash and the send needs a second.
type Uploader interface {
	Put(key string, body io.ReadSeeker, size int64) error
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
	// MinAge holds a file back until it has been quiet this long. Caddy's
	// roller writes in place, so a file's presence does not mean it is
	// finished; age is the cheap proxy for "nobody is still writing this".
	MinAge time.Duration
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
	present := map[string]bool{}
	for _, entry := range entries {
		present[entry.Name()] = true
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
		alreadyGzipped := match[2] == ".gz"
		// Caddy compresses a finished roll in place: while its compressor
		// runs, the complete .log and a truncated .gz coexist, and both
		// would ship to the same key — the corrupt one last. A .gz whose
		// .log sibling is still present is by definition unfinished.
		if alreadyGzipped && present[entry.Name()[:len(entry.Name())-len(".gz")]] {
			continue
		}
		if s.MinAge > 0 {
			info, err := entry.Info()
			if err != nil || time.Since(info.ModTime()) < s.MinAge {
				continue
			}
		}
		if err := s.shipFile(entry.Name(), match[1], alreadyGzipped); err != nil {
			errs = append(errs, fmt.Errorf("%s: %w", entry.Name(), err))
			continue
		}
		shipped++
	}
	return shipped, errors.Join(errs...)
}

func (s *Shipper) shipFile(name, date string, alreadyGzipped bool) error {
	path := filepath.Join(s.Dir, name)
	key := "logs/source=" + s.Source + "/dt=" + date + "/" + name
	uploadPath := path
	if !alreadyGzipped {
		// Compressed to a temp file, not a buffer: the input can be a
		// gigabyte and the memory cap is a fraction of that. The suffix is
		// one the rolled-log pattern can never match, so a crash mid-write
		// leaves junk a later pass ignores rather than uploads.
		tmp, err := s.gzipToTemp(path)
		if err != nil {
			return err
		}
		defer os.Remove(tmp)
		uploadPath = tmp
		key += ".gz"
	}
	file, err := os.Open(uploadPath)
	if err != nil {
		return err
	}
	defer file.Close()
	info, err := file.Stat()
	if err != nil {
		return err
	}
	if err := s.Uploader.Put(key, file, info.Size()); err != nil {
		return err
	}
	return os.Remove(path)
}

func (s *Shipper) gzipToTemp(path string) (string, error) {
	src, err := os.Open(path)
	if err != nil {
		return "", err
	}
	defer src.Close()
	tmp, err := os.CreateTemp(s.Dir, ".shipping-*.gztmp")
	if err != nil {
		return "", err
	}
	w := gzip.NewWriter(tmp)
	_, copyErr := io.Copy(w, src)
	closeErr := errors.Join(w.Close(), tmp.Close())
	if err := errors.Join(copyErr, closeErr); err != nil {
		os.Remove(tmp.Name())
		return "", err
	}
	return tmp.Name(), nil
}
