package log_shipper

import (
	"compress/gzip"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"log/slog"
	"os"
	"path/filepath"
	"regexp"
	"strings"
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

// A Source is one directory of rolls and the partition label they ship
// under: logs/source=<Label>/. One shipper process serves several, since a
// pass is per directory and the credentials are the same.
type Source struct {
	Label string
	Dir   string
}

// ParseSources reads the LOG_DIRS setting, "label=dir,label=dir". Labels
// are partition names, so they are one word each and distinct.
func ParseSources(spec string) ([]Source, error) {
	var sources []Source
	seen := map[string]bool{}
	for _, item := range strings.Split(spec, ",") {
		item = strings.TrimSpace(item)
		if item == "" {
			continue
		}
		label, dir, ok := strings.Cut(item, "=")
		label, dir = strings.TrimSpace(label), strings.TrimSpace(dir)
		if !ok || label == "" || dir == "" {
			return nil, fmt.Errorf("LOG_DIRS entry %q is not label=dir", item)
		}
		if strings.ContainsAny(label, "/= ") {
			return nil, fmt.Errorf("LOG_DIRS label %q is not a partition name", label)
		}
		if seen[label] {
			return nil, fmt.Errorf("LOG_DIRS names source %q twice", label)
		}
		if seen[dir] {
			return nil, fmt.Errorf("LOG_DIRS names directory %q twice; the first label would take every file", dir)
		}
		seen[label], seen[dir] = true, true
		sources = append(sources, Source{Label: label, Dir: dir})
	}
	if len(sources) == 0 {
		return nil, errors.New("LOG_DIRS names no directory")
	}
	return sources, nil
}

// A rolled log as Caddy's roller names it, optionally .gz when Caddy
// compressed it. Caddy ≥2.11 rolls with timberjack, which always appends a
// roll reason after the timestamp (base-<timestamp>-size.log): size, time,
// or initial from Caddy itself, and the library sanitizes caller-chosen
// reasons to word characters — so the trailing segment is one optional \w+
// rather than an enumeration. Older lumberjack names carry no reason and
// still match. The date group is the partition key — the roll time, which
// is when the last line in the file was written.
//
// logback (one_d4's query events) rolls to the same shape, through an
// intermediate: the live file is renamed to <roll>.log<nanos>.tmp and
// compressed asynchronously to <roll>.log.gz, the .tmp going away after.
// A JVM killed mid-compression leaves the .tmp behind, uncompressed and
// complete; it ships under the roll's own name once it has been quiet.
// Groups: the roll name up to .log, the date, the .tmp suffix, the .gz.
var rolledLog = regexp.MustCompile(`^(.+-(\d{4}-\d{2}-\d{2})T[\d.\-]+(?:-\w+)?\.log)(\d+\.tmp)?(\.gz)?$`)

// ShipOnce uploads every rolled file currently in Dir and deletes what it
// uploaded. Per-file failures do not stop the rest; they are joined into
// the returned error with the count of files that did ship. skipped counts
// every regular file left in place without an upload attempt — the live
// log, names the roller pattern does not recognize, mid-compression
// siblings, files younger than MinAge or already deleted when stat'd (any
// other stat failure joins the returned error) — so a pass that ships
// nothing from a non-empty directory is distinguishable from an empty one
// in the pass log.
func (s *Shipper) ShipOnce() (shipped, skipped int, err error) {
	entries, err := os.ReadDir(s.Dir)
	if err != nil {
		return 0, 0, err
	}
	present := map[string]bool{}
	for _, entry := range entries {
		present[entry.Name()] = true
	}
	var errs []error
	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}
		// .shipping-*.gztmp is this program's own temp file (gzipToTemp).
		// One orphaned by a crash mid-pass would otherwise sit in the
		// skipped count on every pass forever, drifting the baseline the
		// README's monitoring signal assumes. A quiet one is junk by
		// definition: the only writer is this loop, later in this pass.
		if strings.HasPrefix(entry.Name(), ".shipping-") && strings.HasSuffix(entry.Name(), ".gztmp") {
			if info, err := entry.Info(); err == nil && time.Since(info.ModTime()) >= s.MinAge {
				if err := os.Remove(filepath.Join(s.Dir, entry.Name())); err != nil {
					errs = append(errs, fmt.Errorf("%s: %w", entry.Name(), err))
				}
			}
			continue
		}
		match := rolledLog.FindStringSubmatch(entry.Name())
		if match == nil {
			skipped++
			continue
		}
		alreadyGzipped := match[4] == ".gz"
		// Caddy compresses a finished roll in place: while its compressor
		// runs, the complete .log and a truncated .gz coexist, and both
		// would ship to the same key — the corrupt one last. A .gz whose
		// .log sibling is still present is by definition unfinished.
		if alreadyGzipped && present[entry.Name()[:len(entry.Name())-len(".gz")]] {
			skipped++
			continue
		}
		if s.MinAge > 0 {
			info, err := entry.Info()
			if err != nil {
				// Gone between ReadDir and here means Caddy's compressor or
				// roll_keep pruning got it first — nothing to ship. Any
				// other stat failure (a permission drift on the mount) must
				// reach the error log, or every pass would read as
				// unrecognized names.
				if errors.Is(err, fs.ErrNotExist) {
					skipped++
				} else {
					errs = append(errs, fmt.Errorf("%s: stat: %w", entry.Name(), err))
				}
				continue
			}
			if time.Since(info.ModTime()) < s.MinAge {
				skipped++
				continue
			}
		}
		if err := s.shipFile(entry.Name(), match[1], match[2], alreadyGzipped); err != nil {
			errs = append(errs, fmt.Errorf("%s: %w", entry.Name(), err))
			continue
		}
		shipped++
	}
	return shipped, skipped, errors.Join(errs...)
}

// shipFile uploads one file under the roll's name — the file's own, less
// any intermediate suffix — and deletes it on success.
func (s *Shipper) shipFile(name, rollName, date string, alreadyGzipped bool) error {
	path := filepath.Join(s.Dir, name)
	key := "logs/source=" + s.Source + "/dt=" + date + "/" + rollName
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
	}
	if !strings.HasSuffix(key, ".gz") {
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

// ShipAll runs one pass over every source. A source whose pass fails —
// its directory missing, an upload refused — is logged and the rest still
// ship; the next pass retries it. Every pass is logged, even an idle one:
// a skipped count that never drains past the live log while shipped stays
// zero is the signature of a roll-name format the pattern no longer matches.
func ShipAll(shippers []*Shipper, logger *slog.Logger) {
	for _, s := range shippers {
		shipped, skipped, err := s.ShipOnce()
		if err != nil {
			logger.Error("shipping pass finished with failures", "source", s.Source,
				"shipped", shipped, "skipped", skipped, "error", err)
			continue
		}
		logger.Info("shipping pass complete", "source", s.Source,
			"shipped", shipped, "skipped", skipped)
	}
}
