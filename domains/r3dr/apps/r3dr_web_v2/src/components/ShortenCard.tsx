import { useState } from 'react';
import { shorten, shortLink } from '../api';
import { DEFAULT_EXPIRY, describeExpiry, EXPIRY_OPTIONS, type ExpiryOption } from '../expiry';
import type { RecentLink } from '../recent';
import CopyButton from './CopyButton';

// Bare domains are the common paste; give them the scheme the API requires.
// Other schemes pass through untouched and fail validation with the message.
export function normalizeUrl(input: string): string {
  const trimmed = input.trim();
  if (trimmed === '' || /^[a-z][a-z0-9+.-]*:/i.test(trimmed)) return trimmed;
  return `https://${trimmed}`;
}

// The API's traits (@length 11–1000, @pattern ^https?://), mirrored for
// instant feedback. The server still enforces them; wire_test pins that.
export function validateUrl(url: string): string | null {
  if (url === '') return 'Paste a link first.';
  if (!/^https?:\/\//.test(url)) return 'Links must start with http:// or https://.';
  if (url.length < 11) return 'That URL looks too short.';
  if (url.length > 1000) return 'URLs top out at 1000 characters.';
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
    const longUrl = normalizeUrl(input);
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
      setInput('');
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
          <button type="submit" className="submit-btn" disabled={pending}>
            {pending ? 'Shortening…' : 'Shorten'}
          </button>
        </div>

        <div className="expiry" role="radiogroup" aria-label="Expires after">
          <span className="expiry-label">Expires after</span>
          {EXPIRY_OPTIONS.map((option) => (
            <button
              key={option.label}
              type="button"
              role="radio"
              aria-checked={option === expiry}
              className={`chip${option === expiry ? ' chip--on' : ''}`}
              onClick={() => setExpiry(option)}
            >
              {option.label}
            </button>
          ))}
        </div>
      </form>

      {error && (
        <p className="message error" role="alert">
          {error}
        </p>
      )}

      {minted && (
        <div className="result" role="status">
          <div className="result-row">
            <a className="result-link" href={shortLink(minted.slug)} target="_blank" rel="noreferrer">
              r3dr.net/r/{minted.slug}
            </a>
            <CopyButton text={shortLink(minted.slug)} />
          </div>
          <p className="result-note">
            <span className="result-target">{minted.longUrl}</span>
            <span className="result-expiry"> · expires {describeExpiry(minted.expiresAt - Date.now())}</span>
          </p>
        </div>
      )}
    </section>
  );
}
