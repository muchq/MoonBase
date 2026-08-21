import { describe, expect, it } from 'vitest';
import { DEFAULT_EXPIRY, describeExpiry, EXPIRY_OPTIONS } from '../expiry';

const MINUTE = 60 * 1000;
const HOUR = 60 * MINUTE;
const DAY = 24 * HOUR;

describe('EXPIRY_OPTIONS', () => {
  it('pins every option, the top one five minutes under the API ceiling', () => {
    // Under, never at: the server checks its own clock, and a client
    // running seconds fast must not turn the 30-day button into a 400.
    expect(EXPIRY_OPTIONS.map((option) => [option.label, option.ms])).toEqual([
      ['1 hour', HOUR],
      ['1 day', DAY],
      ['7 days', 7 * DAY],
      ['30 days', 30 * DAY - 5 * MINUTE],
    ]);
  });

  it('defaults to 7 days, like v1 always minted', () => {
    expect(DEFAULT_EXPIRY.label).toBe('7 days');
    expect(EXPIRY_OPTIONS).toContain(DEFAULT_EXPIRY);
  });
});

describe('describeExpiry', () => {
  it('speaks minutes, hours, then days — switching at 48 hours', () => {
    expect(describeExpiry(20 * MINUTE)).toBe('in 20 minutes');
    expect(describeExpiry(MINUTE)).toBe('in 1 minute');
    expect(describeExpiry(3 * HOUR)).toBe('in 3 hours');
    expect(describeExpiry(47 * HOUR)).toBe('in 47 hours');
    expect(describeExpiry(49 * HOUR)).toBe('in 2 days');
    expect(describeExpiry(7 * DAY)).toBe('in 7 days');
  });

  it('calls the past expired', () => {
    expect(describeExpiry(0)).toBe('expired');
    expect(describeExpiry(-5)).toBe('expired');
  });
});
