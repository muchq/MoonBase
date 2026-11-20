import { useEffect } from 'react';
import { Link, Navigate, useParams } from 'react-router';
import { lessonHtml } from 'virtual:astlol-content';
import StepNav from '../components/StepNav';
import { lessonById, tierOfStep } from '../curriculum/registry';
import { markVisited, markRead, useProgress } from '../state/progress';
import LessonPractice from '../components/LessonPractice';
import { tierGuide } from '../curriculum/learning';

export default function LessonView() {
  const { id = '' } = useParams();
  const lesson = lessonById(id);

  const progress = useProgress();
  // A visit is not evidence of reading or understanding.
  useEffect(() => {
    if (lesson !== undefined) markVisited(lesson.id);
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
        {tier && <p className="learning-outcome">{tierGuide[tier.id].outcome}</p>}
        {/* Course-authored markdown rendered at build time; no user content. */}
        <div dangerouslySetInnerHTML={{ __html: lessonHtml[lesson.id] ?? '' }} />
      </article>
      <div className="lesson-actions">
        <button className="btn" type="button" onClick={() => markRead(lesson.id)} disabled={progress.read[lesson.id] !== undefined}>
          {progress.read[lesson.id] ? 'Read' : 'Mark as read'}
        </button>
        <span>Read, practice, then implement. You can move on at any time.</span>
      </div>
      <LessonPractice key={lesson.id} lessonId={lesson.id} />
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
