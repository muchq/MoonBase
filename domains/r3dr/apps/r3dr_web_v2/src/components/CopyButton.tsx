import { useEffect, useRef, useState } from 'react';

// clipboard.writeText needs a secure context; the textarea/execCommand path
// covers plain-http dev and older mobile browsers.
async function copyText(text: string): Promise<boolean> {
  try {
    await navigator.clipboard.writeText(text);
    return true;
  } catch (_) {
    try {
      const area = document.createElement('textarea');
      area.value = text;
      area.style.position = 'fixed';
      area.style.left = '-9999px';
      document.body.appendChild(area);
      area.select();
      const ok = document.execCommand('copy');
      document.body.removeChild(area);
      return ok;
    } catch (_) {
      return false;
    }
  }
}

export default function CopyButton({ text, compact = false }: { text: string; compact?: boolean }) {
  const [state, setState] = useState<'idle' | 'copied' | 'failed'>('idle');
  const timer = useRef<number | undefined>(undefined);

  useEffect(() => () => window.clearTimeout(timer.current), []);

  async function copy() {
    setState((await copyText(text)) ? 'copied' : 'failed');
    window.clearTimeout(timer.current);
    timer.current = window.setTimeout(() => setState('idle'), 2000);
  }

  return (
    <button
      type="button"
      className={`copy-btn${compact ? ' copy-btn--compact' : ''}${state === 'copied' ? ' copied' : ''}`}
      onClick={copy}
      aria-label={`Copy ${text}`}
    >
      {state === 'idle' ? 'Copy' : state === 'copied' ? 'Copied ✓' : 'Press ⌘C'}
    </button>
  );
}
