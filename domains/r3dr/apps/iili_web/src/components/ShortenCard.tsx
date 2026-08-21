import { useState } from 'react';
import { shorten, shortLink, shortLinkLabel } from '../api';
import { DEFAULT_EXPIRY, describeExpiry, EXPIRY_OPTIONS, type ExpiryOption } from '../expiry';
import type { RecentLink } from '../recent';
import CopyButton from './CopyButton';

// Bare domains are the common paste; give them the scheme the API requires.
// Schemes are case-insensitive (the API's pattern is not), so an uppercased
// http(s) is lowercased rather than bounced with a confusing message. Other
// schemes pass through untouched and fail validation.
export function normalizeUrl(input: string): string {
  const trimmed = input.trim();
  if (/^https?:\/\//i.test(trimmed)) {
    return trimmed.replace(/^[^:]+/, (scheme) => scheme.toLowerCase());
  }
  if (trimmed === '' || /^[a-z][a-z0-9+.-]*:/i.test(trimmed)) return trimmed;
  return `https://${trimmed}`;
}

// The API's traits (@length 11–1000, @pattern ^https?://), mirrored for
// instant feedback. Lengths count code points, like the server does. The
// server still enforces them; wire_test pins that.
export function validateUrl(url: string): string | null {
  if (url === '') return 'Paste a link first.';
  if (!/^https?:\/\//.test(url)) return 'Links must start with http:// or https://.';
  const length = [...url].length;
  if (length < 11) return 'That URL looks too short.';
  if (length > 1000) return 'URLs top out at 1000 characters.';
  return null;
}

export default function ShortenCard({ onMinted }: { onMinted: (link: RecentLink) => void }) {
  const [input, setInput] = useState('');
  const [expiry, setExpiry] = useState<ExpiryOption>(DEFAULT_EXPIRY);
  const [pending, setPending] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [minted, setMinted] = useState<RecentLink | null>(null);

  async function handleSubmit(event: React.FormEvent) {
    event.preventDefault();
    if (pending) return;
    const submitted = input;
    const longUrl = normalizeUrl(submitted);
    const problem = validateUrl(longUrl);
    if (problem) {
      setError(problem);
      setMinted(null);
      return;
    }
    setPending(true);
    setError(null);
    const expiresAt = Date.now() + expiry.ms;
    try {
      const { slug } = await shorten(longUrl, expiresAt);
      const link = { slug, longUrl, expiresAt };
      setMinted(link);
      // Keep anything typed while the request was in flight.
      setInput((current) => (current === submitted ? '' : current));
      onMinted(link);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Something went wrong');
      setMinted(null);
    } finally {
      setPending(false);
    }
  }

  return (
    <section className="card">
      <form onSubmit={handleSubmit} noValidate>
        <label className="field-label" htmlFor="long-url">
          Long link
        </label>
        <div className="field-row">
          <input
            id="long-url"
            type="url"
            inputMode="url"
            autoComplete="off"
            spellCheck={false}
            placeholder="https://example.com/somewhere/very/deep"
            value={input}
            onChange={(event) => setInput(event.target.value)}
            autoFocus
          />
          {/* Never disabled: disabling the focused button dumps keyboard
              focus on <body>. Re-entry is guarded in handleSubmit. */}
          <button type="submit" className="submit-btn" aria-busy={pending}>
            {pending ? 'Shortening…' : 'Shorten'}
          </button>
        </div>

        <div className="expiry" role="group" aria-labelledby="expiry-label">
          <span className="expiry-label" id="expiry-label">
            Expires after
          </span>
          {EXPIRY_OPTIONS.map((option) => (
            <button
              key={option.label}
              type="button"
              aria-pressed={option === expiry}
              className={`chip${option === expiry ? ' chip--on' : ''}`}
              onClick={() => setExpiry(option)}
            >
              {option.label}
            </button>
          ))}
        </div>
      </form>

      {/* Live regions stay mounted; a region injected together with its
          content is unreliably announced. */}
      <p className={error ? 'message error' : 'live-slot'} role="alert">
        {error}
      </p>

      <div className={minted ? 'result' : 'live-slot'} role="status">
        {minted && (
          <>
            <div className="result-row">
              <a
                className="result-link"
                href={shortLink(minted.slug)}
                target="_blank"
                rel="noreferrer"
              >
                {shortLinkLabel(minted.slug)}
              </a>
              <CopyButton text={shortLink(minted.slug)} />
            </div>
            <p className="result-note">
              <span className="result-target">{minted.longUrl}</span>
              <span>· expires {describeExpiry(minted.expiresAt - Date.now())}</span>
            </p>
          </>
        )}
      </div>
    </section>
  );
}
