import React from 'react';
import { render, screen } from '@testing-library/react';
import { afterEach, describe, it, expect } from 'vitest';
import { cleanup } from '@testing-library/react';
import Pagination from '../components/Pagination';

afterEach(cleanup);

describe('Pagination', () => {
  it('enables Next when hasMore=true even if offset+limit >= total', () => {
    render(
      <Pagination
        offset={0}
        limit={25}
        total={25}
        onLimitChange={() => {}}
        onPrev={() => {}}
        onNext={() => {}}
        hasMore={true}
      />
    );
    expect(screen.getByRole('button', { name: 'Next' })).not.toBeDisabled();
  });

  it('disables Next when hasMore=false', () => {
    render(
      <Pagination
        offset={0}
        limit={25}
        total={100}
        onLimitChange={() => {}}
        onPrev={() => {}}
        onNext={() => {}}
        hasMore={false}
      />
    );
    expect(screen.getByRole('button', { name: 'Next' })).toBeDisabled();
  });

  it('falls back to offset+limit>=total when hasMore is not provided', () => {
    render(
      <Pagination
        offset={75}
        limit={25}
        total={100}
        onLimitChange={() => {}}
        onPrev={() => {}}
        onNext={() => {}}
      />
    );
    expect(screen.getByRole('button', { name: 'Next' })).toBeDisabled();
  });
});
