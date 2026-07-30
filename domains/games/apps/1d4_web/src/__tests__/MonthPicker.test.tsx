import React, { useState } from 'react';
import { render, screen, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { describe, it, expect, vi, afterEach } from 'vitest';
import MonthPicker, {
  currentMonth,
  formatMonth,
  monthValue,
} from '../components/MonthPicker';

/**
 * MonthPicker is a controlled component, so the harness owns the value the way
 * a real caller does — otherwise "picking a month updates the trigger" would be
 * untestable and selection assertions would only ever see the initial value.
 */
function Picker({
  initialValue = '2024-03',
  onChange,
  min,
  max,
}: {
  initialValue?: string;
  onChange?: (value: string) => void;
  min?: string;
  max?: string;
}) {
  const [value, setValue] = useState(initialValue);
  return (
    <MonthPicker
      id="month"
      label="Start month"
      value={value}
      onChange={(next) => {
        setValue(next);
        onChange?.(next);
      }}
      min={min}
      max={max}
    />
  );
}

const trigger = () => screen.getByRole('button', { name: /^Start month/ });
const popover = () => screen.queryByRole('dialog');
const cell = (month: string) => screen.getByRole('button', { name: month });
const shownYear = () => within(screen.getByRole('dialog')).getByText(/^\d{4}$/);

afterEach(() => {
  vi.useRealTimers();
});

describe('MonthPicker value helpers', () => {
  it('zero-pads single-digit months so values sort in calendar order', () => {
    expect(monthValue(2024, 0)).toBe('2024-01');
    expect(monthValue(2024, 8)).toBe('2024-09');
    expect(monthValue(2024, 11)).toBe('2024-12');
    // The whole min/max implementation leans on this ordering property.
    expect(monthValue(2024, 8) < monthValue(2024, 11)).toBe(true);
    expect(monthValue(2024, 11) < monthValue(2025, 0)).toBe(true);
  });

  it('renders values as "Mon YYYY" and passes through anything unparseable', () => {
    expect(formatMonth('2024-03')).toBe('Mar 2024');
    expect(formatMonth('2024-12')).toBe('Dec 2024');
    expect(formatMonth('')).toBe('');
    expect(formatMonth('2024-13')).toBe('2024-13');
    expect(formatMonth('nonsense')).toBe('nonsense');
  });

  it('reads the current month in local time, not UTC', () => {
    vi.useFakeTimers({ toFake: ['Date'] });
    // 23:30 on Dec 31 in a UTC+1 zone: UTC is still 22:30 on Dec 31, but a
    // viewer two hours further east has already rolled into January.
    vi.setSystemTime(new Date(2024, 11, 31, 23, 30));
    expect(currentMonth()).toBe('2024-12');

    vi.setSystemTime(new Date(2025, 0, 1, 0, 30));
    expect(currentMonth()).toBe('2025-01');
  });
});

describe('MonthPicker', () => {
  it('shows the selected month on the trigger, named by its field label', () => {
    render(<Picker initialValue="2024-03" />);
    expect(trigger()).toHaveTextContent('Mar 2024');
    expect(trigger()).toHaveAccessibleName('Start month Mar 2024');
  });

  it('opens and closes from the trigger, tracking aria-expanded', async () => {
    const user = userEvent.setup();
    render(<Picker />);

    expect(popover()).not.toBeInTheDocument();
    expect(trigger()).toHaveAttribute('aria-expanded', 'false');

    await user.click(trigger());
    expect(popover()).toBeInTheDocument();
    expect(trigger()).toHaveAttribute('aria-expanded', 'true');

    await user.click(trigger());
    expect(popover()).not.toBeInTheDocument();
    expect(trigger()).toHaveAttribute('aria-expanded', 'false');
  });

  it("opens on the selected value's year with that month marked selected", async () => {
    const user = userEvent.setup();
    render(<Picker initialValue="2019-11" />);

    await user.click(trigger());
    expect(shownYear()).toHaveTextContent('2019');
    expect(cell('Nov')).toHaveAttribute('aria-pressed', 'true');
    expect(cell('Mar')).toHaveAttribute('aria-pressed', 'false');
  });

  it('emits canonical zero-padded YYYY-MM and closes when a month is picked', async () => {
    const user = userEvent.setup();
    const onChange = vi.fn();
    render(<Picker initialValue="2024-11" onChange={onChange} />);

    await user.click(trigger());
    await user.click(cell('Mar'));

    // Not "2024-3" — the API only accepts the padded form.
    expect(onChange).toHaveBeenCalledExactlyOnceWith('2024-03');
    expect(popover()).not.toBeInTheDocument();
    expect(trigger()).toHaveTextContent('Mar 2024');
  });

  it('steps the year without emitting a change until a month is picked', async () => {
    const user = userEvent.setup();
    const onChange = vi.fn();
    render(<Picker initialValue="2024-03" onChange={onChange} />);

    await user.click(trigger());
    await user.click(screen.getByRole('button', { name: 'Previous year' }));

    expect(shownYear()).toHaveTextContent('2023');
    expect(onChange).not.toHaveBeenCalled();
    // Mar 2023 is a different month from the selected Mar 2024.
    expect(cell('Mar')).toHaveAttribute('aria-pressed', 'false');

    await user.click(cell('Mar'));
    expect(onChange).toHaveBeenCalledExactlyOnceWith('2023-03');
  });

  it("reopens on the value's year after the stepper was moved", async () => {
    const user = userEvent.setup();
    render(<Picker initialValue="2024-03" />);

    await user.click(trigger());
    await user.click(screen.getByRole('button', { name: 'Previous year' }));
    expect(shownYear()).toHaveTextContent('2023');

    await user.keyboard('{Escape}');
    await user.click(trigger());
    expect(shownYear()).toHaveTextContent('2024');
  });

  it('disables months after max and the step past its year', async () => {
    const user = userEvent.setup();
    render(<Picker initialValue="2024-03" min="2020-01" max="2024-05" />);

    await user.click(trigger());
    expect(cell('May')).toBeEnabled();
    expect(cell('Jun')).toBeDisabled();
    expect(cell('Dec')).toBeDisabled();
    expect(screen.getByRole('button', { name: 'Next year' })).toBeDisabled();
    expect(screen.getByRole('button', { name: 'Previous year' })).toBeEnabled();
  });

  it('disables months before min and the step past its year', async () => {
    const user = userEvent.setup();
    render(<Picker initialValue="2024-08" min="2024-05" max="2026-12" />);

    await user.click(trigger());
    expect(cell('May')).toBeEnabled();
    expect(cell('Apr')).toBeDisabled();
    expect(cell('Jan')).toBeDisabled();
    expect(screen.getByRole('button', { name: 'Previous year' })).toBeDisabled();
    expect(screen.getByRole('button', { name: 'Next year' })).toBeEnabled();
  });

  it('bounds itself to 2007 through the current month by default', async () => {
    vi.useFakeTimers({ toFake: ['Date'] });
    vi.setSystemTime(new Date(2024, 5, 15));
    const user = userEvent.setup({ delay: null });
    render(<Picker initialValue="2024-06" />);

    await user.click(trigger());
    // Nothing in the future is offered, not even later this year.
    expect(cell('Jun')).toBeEnabled();
    expect(cell('Jul')).toBeDisabled();
    expect(cell('Dec')).toBeDisabled();
    expect(screen.getByRole('button', { name: 'Next year' })).toBeDisabled();

    // ...and the floor is chess.com's earliest archive year.
    await user.keyboard('{Escape}');
    render(<Picker initialValue="2007-01" />);
    await user.click(screen.getAllByRole('button', { name: /^Start month/ })[1]);
    const dialog = screen.getByRole('dialog');
    expect(within(dialog).getByRole('button', { name: 'Jan' })).toBeEnabled();
    expect(
      within(dialog).getByRole('button', { name: 'Previous year' })
    ).toBeDisabled();
  });

  it('closes on Escape and returns focus to the trigger', async () => {
    const user = userEvent.setup();
    render(<Picker />);

    await user.click(trigger());
    await user.keyboard('{Escape}');

    expect(popover()).not.toBeInTheDocument();
    expect(trigger()).toHaveFocus();
  });

  it('closes on an outside click without selecting anything', async () => {
    const user = userEvent.setup();
    const onChange = vi.fn();
    render(
      <>
        <Picker onChange={onChange} />
        <button type="button">elsewhere</button>
      </>
    );

    await user.click(trigger());
    await user.click(screen.getByRole('button', { name: 'elsewhere' }));

    expect(popover()).not.toBeInTheDocument();
    expect(onChange).not.toHaveBeenCalled();
  });

  it('never submits the form it lives in', async () => {
    const user = userEvent.setup();
    const onSubmit = vi.fn((e: React.FormEvent) => e.preventDefault());
    render(
      <form onSubmit={onSubmit}>
        <Picker />
        <button type="submit">Enqueue</button>
      </form>
    );

    await user.click(trigger());
    await user.click(screen.getByRole('button', { name: 'Previous year' }));
    await user.click(cell('Mar'));
    expect(onSubmit).not.toHaveBeenCalled();

    // Control: proves this environment really does submit on a button click,
    // so the assertion above is about type="button" and not about jsdom.
    await user.click(screen.getByRole('button', { name: 'Enqueue' }));
    expect(onSubmit).toHaveBeenCalledOnce();
  });

  it('puts the caller-supplied class on the wrapper for layout', () => {
    const { container } = render(
      <MonthPicker
        id="month"
        label="Start month"
        value="2024-03"
        onChange={() => {}}
        className="enqueue-month"
      />
    );
    expect(container.querySelector('.month-picker')).toHaveClass('enqueue-month');
  });
});
