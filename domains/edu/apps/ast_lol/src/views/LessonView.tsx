import { useEffect } from 'react';
import { Link, Navigate, useParams } from 'react-router';
import { lessonHtml } from 'virtual:astlol-content';
import StepNav from '../components/StepNav';
import { lessonById, tierOfStep } from '../curriculum/registry';
import { markCompleted } from '../state/progress';

export default function LessonView() {
  const { id = '' } = useParams();
  const lesson = lessonById(id);

  // Reading a lesson completes it; there is nothing to grade.
  useEffect(() => {
    if (lesson !== undefined) markCompleted(lesson.id);
  }, [lesson]);

  if (lesson === undefined) return <Navigate to="/" replace />;
  const tier = tierOfStep(lesson.id);

  return (
    <>
      <nav className="crumbs">
        <Link to="/">curriculum</Link>
        {tier !== undefined && ` / tier ${tier.number} — ${tier.title}`}
      </nav>
      <article className="prose">
        <h2>{lesson.title}</h2>
        {/* Course-authored markdown rendered at build time; no user content. */}
        <div dangerouslySetInnerHTML={{ __html: lessonHtml[lesson.id] ?? '' }} />
      </article>
      <aside className="reading">
        <h3>Further reading</h3>
        <ul>
          {lesson.reading.map((r) => (
            <li key={r.url}>
              <a href={r.url} target="_blank" rel="noreferrer">
                {r.title}
              </a>
              {r.note !== undefined && <span className="note"> — {r.note}</span>}
            </li>
          ))}
        </ul>
      </aside>
      <StepNav currentId={lesson.id} />
    </>
  );
}
