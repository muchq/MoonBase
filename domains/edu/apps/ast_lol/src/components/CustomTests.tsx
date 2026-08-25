import { useState } from 'react';
import type { ChallengeDef } from '../curriculum/types';
import type { CustomTestSpec } from '../grader/types';

export interface CustomTestsProps {
  challenge: ChallengeDef;
  tests: CustomTestSpec[];
  onChange: (tests: CustomTestSpec[]) => void;
}

/**
 * User-authored tests: input only — the reference solution (or the
 * challenge's semantic checker) supplies the expected answer at run time.
 */
export default function CustomTests({ challenge, tests, onChange }: CustomTestsProps) {
  const [name, setName] = useState('');
  const [source, setSource] = useState('');
  if (challenge.custom === null) return null;

  const add = () => {
    if (source.trim() === '') return;
    const id = `custom-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 7)}`;
    onChange([...tests, { id, name: name.trim() === '' ? source.trim() : name.trim(), source }]);
    setName('');
    setSource('');
  };

  return (
    <div className="panel">
      <div className="panel-header">
        <span>Your tests ({tests.length})</span>
      </div>
      <div className="custom-help">{challenge.custom.describe}</div>
      {tests.map((t) => (
        <div className="custom-test-row" key={t.id}>
          <input className="name" type="text" value={t.name} readOnly aria-label="test name" />
          <input className="source" type="text" value={t.source} readOnly aria-label="test input" />
          <button
            type="button"
            className="btn"
            onClick={() => onChange(tests.filter((x) => x.id !== t.id))}
          >
            Remove
          </button>
        </div>
      ))}
      <div className="custom-test-row">
        <input
          className="name"
          type="text"
          placeholder="name (optional)"
          value={name}
          aria-label="new test name"
          onChange={(e) => setName(e.target.value)}
        />
        <input
          className="source"
          type="text"
          placeholder={challenge.custom.placeholder}
          value={source}
          aria-label="new test input"
          onChange={(e) => setSource(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === 'Enter') add();
          }}
        />
        <button type="button" className="btn" onClick={add}>
          Add test
        </button>
      </div>
    </div>
  );
}
