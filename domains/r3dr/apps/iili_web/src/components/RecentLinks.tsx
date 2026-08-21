import { shortLink, shortLinkLabel } from '../api';
import { describeExpiry } from '../expiry';
import type { RecentLink } from '../recent';
import CopyButton from './CopyButton';

export default function RecentLinks({
  links,
  onClear,
}: {
  links: RecentLink[];
  onClear: () => void;
}) {
  if (links.length === 0) return null;
  return (
    <section className="recent" aria-labelledby="recent-heading">
      <div className="recent-head">
        <h2 id="recent-heading">Recent links</h2>
        <button
          type="button"
          className="ghost-btn"
          onClick={onClear}
          aria-label="Clear recent links"
        >
          Clear
        </button>
      </div>
      <ul className="recent-list">
        {links.map((link) => (
          <li key={link.slug} className="recent-row">
            <div className="recent-urls">
              <a href={shortLink(link.slug)} target="_blank" rel="noreferrer">
                {shortLinkLabel(link.slug)}
              </a>
              <span className="recent-target" title={link.longUrl}>
                {link.longUrl}
              </span>
              <span className="recent-expiry">
                expires {describeExpiry(link.expiresAt - Date.now())}
              </span>
            </div>
            <CopyButton text={shortLink(link.slug)} compact />
          </li>
        ))}
      </ul>
    </section>
  );
}
