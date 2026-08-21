import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import CopyButton from '../components/CopyButton';

// fireEvent, not user-event: userEvent.setup() installs its own clipboard
// stub, which would shadow the one under test.

function stubClipboard(writeText: (text: string) => Promise<void>) {
  Object.defineProperty(navigator, 'clipboard', {
    value: { writeText },
    configurable: true,
  });
}

describe('CopyButton', () => {
  it('copies the exact text and confirms', async () => {
    const writeText = vi.fn().mockResolvedValue(undefined);
    stubClipboard(writeText);
    render(<CopyButton text="https://i.iili.uk/r/AQA" />);

    fireEvent.click(screen.getByRole('button', { name: 'Copy https://i.iili.uk/r/AQA' }));

    expect(await screen.findByRole('button', { name: 'Copied' })).toHaveTextContent('Copied ✓');
    expect(writeText).toHaveBeenCalledWith('https://i.iili.uk/r/AQA');
  });

  it('admits failure when no copy path works', async () => {
    stubClipboard(vi.fn().mockRejectedValue(new Error('denied')));
    document.execCommand = vi.fn().mockReturnValue(false);
    render(<CopyButton text="https://i.iili.uk/r/AQA" />);

    fireEvent.click(screen.getByRole('button', { name: /^Copy / }));

    expect(await screen.findByRole('button', { name: 'Copy failed' })).toBeInTheDocument();
  });

  it('falls back to execCommand when the clipboard API is refused', async () => {
    stubClipboard(vi.fn().mockRejectedValue(new Error('denied')));
    document.execCommand = vi.fn().mockReturnValue(true);
    render(<CopyButton text="https://i.iili.uk/r/AQA" />);

    fireEvent.click(screen.getByRole('button', { name: /^Copy / }));

    expect(await screen.findByRole('button', { name: 'Copied' })).toBeInTheDocument();
    expect(document.execCommand).toHaveBeenCalledWith('copy');
  });
});
