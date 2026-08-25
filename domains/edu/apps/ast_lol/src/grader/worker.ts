import { challengeById } from '../curriculum/registry';
import { gradeSubmission } from './harness';
import type { GradeEvent, GradeRequest } from './types';

/**
 * Module worker wrapping the grading harness. User code runs here, off the
 * main thread, so an infinite loop freezes only this worker — the client
 * enforces the time budget by terminating it.
 */

const post = (event: GradeEvent) => {
  (self as unknown as { postMessage: (e: GradeEvent) => void }).postMessage(event);
};

self.onmessage = (e: MessageEvent<GradeRequest>) => {
  const { requestId, challengeId, submission, customTests } = e.data;
  const challenge = challengeById(challengeId);
  if (challenge === undefined) {
    post({ type: 'fatal', requestId, error: `Unknown challenge '${challengeId}'` });
    return;
  }
  try {
    const report = gradeSubmission(challenge, submission, customTests, {
      onTestStart: (id, name) => post({ type: 'test-start', requestId, id, name }),
      onTestResult: (result) => post({ type: 'test-result', requestId, result }),
    });
    post({ type: 'done', requestId, report });
  } catch (err) {
    post({
      type: 'fatal',
      requestId,
      error: err instanceof Error ? err.message : String(err),
    });
  }
};
