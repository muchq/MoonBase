import { describe, expect, it } from 'vitest';
import { DEFAULT_EXPIRY, describeExpiry, EXPIRY_OPTIONS } from '../expiry';

describe('EXPIRY_OPTIONS', () => {
  it('tops out at the API ceiling of exactly 30 days', () => {
    const max = Math.max(...EXPIRY_OPTIONS.map((option) => option.ms));
    expect(max).toBe(30 * 24 * 60 * 60 * 1000);
  });

  it('defaults to 7 days, like v1 always minted', () => {
    expect(DEFAULT_EXPIRY.label).toBe('7 days');
    expect(EXPIRY_OPTIONS).toContain(DEFAULT_EXPIRY);
  });
});

describe('describeExpiry', () => {
  it('speaks minutes, hours, then days', () => {
    expect(describeExpiry(20 * 60 * 1000)).toBe('in 20 minutes');
    expect(describeExpiry(60 * 1000)).toBe('in 1 minute');
    expect(describeExpiry(3 * 60 * 60 * 1000)).toBe('in 3 hours');
    expect(describeExpiry(7 * 24 * 60 * 60 * 1000)).toBe('in 7 days');
  });

  it('calls the past expired', () => {
    expect(describeExpiry(0)).toBe('expired');
    expect(describeExpiry(-5)).toBe('expired');
  });
});
