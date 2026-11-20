import { useState } from 'react';
import { lessonPractice, type PracticeCheck } from '../curriculum/learning';
import { getProgress, markCompleted, markPracticed, useProgress } from '../state/progress';

function Question({ item, index, lessonId, onPass }: { item: PracticeCheck; index: number; lessonId: string; onPass: () => void }) {
  const [selected, setSelected] = useState<number | null>(null);
  const [checked, setChecked] = useState(false);
  const progress = useProgress();
  const key = `${lessonId}:${index}`;
  const passed = progress.practiced[key] !== undefined;
  return <fieldset className="practice-question">
    <legend><span className="eyebrow">{item.kind}</span> {item.question}</legend>
    {item.options.map((option, i) => <label className="practice-option" key={option}>
      <input type="radio" name={key} checked={selected === i} onChange={() => { setSelected(i); setChecked(false); }} />
      {option}
    </label>)}
    <button className="btn" type="button" disabled={selected === null} onClick={() => {
      setChecked(true);
      if (selected === item.answer) { markPracticed(key); onPass(); }
    }}>Check reasoning</button>
    {passed && <span className="practice-status">Practiced</span>}
    {checked && <p role="status" className="practice-feedback">{selected === item.answer ? 'Correct. ' : 'Revisit this example. '}{item.explanation}</p>}
  </fieldset>;
}

export default function LessonPractice({ lessonId }: { lessonId: string }) {
  const items = lessonPractice[lessonId] ?? [];
  if (!items.length) return null;
  const onPass = () => {
    if (items.every((_, i) => getProgress().practiced[`${lessonId}:${i}`] !== undefined)) markCompleted(lessonId);
  };
  return <section className="lesson-practice" aria-labelledby="practice-heading">
    <h3 id="practice-heading">Check your understanding</h3>
    <p>Predict before running code. These checks are optional; your practice is saved.</p>
    {items.map((item, index) => <Question key={`${lessonId}:${index}`} {...{item,index,lessonId,onPass}} />)}
  </section>;
}
