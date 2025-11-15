// TypeScript interfaces corresponding to Rust API types

import type { CLIConfig } from './config';

export type BuildStatus = 'queued' | 'running' | 'completed' | 'failed' | 'cancelled';
export type ExecutionMode = 'sync' | 'async';
export type Environment = 'native' | 'docker';
export type RetentionPolicy = 'all' | 'error';
export type BuildTool = 'bazel' | 'maven' | 'gradle';
export type ErrorSeverity = 'error' | 'warning' | 'info';
export type SyncStrategy = 'pre-build' | 'continuous' | 'post-build';

export interface GitContext {
  branch?: string;
  commit_hash?: string;
  commit_message?: string;
  author?: string;
  has_uncommitted_changes: boolean;
  diff?: string;
}

export interface Project {
  id: string;
  name: string;
  path: string;
  tool_type: BuildTool;
  description?: string;
  created_at: string;
  updated_at: string;
}

export interface Build {
  id: string;
  project_id: string;
  command: string;
  status: BuildStatus;
  execution_mode: ExecutionMode;
  environment: Environment;
  triggered_from: string;
  start_time: string;
  end_time?: string;
  duration_ms?: number;
  exit_code?: number;
  git_context?: GitContext;
  working_directory: string;
  logs_stored: boolean;
}

export interface TestResults {
  total: number;
  passed: number;
  failed: number;
  skipped: number;
  failed_tests: FailedTest[];
}

export interface FailedTest {
  name: string;
  class?: string;
  message: string;
  stack_trace?: string;
}

export interface ParsedError {
  file?: string;
  line?: number;
  column?: number;
  message: string;
  error_type: string;
  severity: ErrorSeverity;
}

export interface BuildSummary {
  build: Build;
  error_count: number;
  warning_count: number;
  test_results?: TestResults;
  parsed_errors: ParsedError[];
}

export interface AIAnalysis {
  id: string;
  build_id: string;
  provider: string;
  model: string;
  summary: string;
  suggested_fixes: SuggestedFix[];
  confidence: number;
  analysis_time_ms: number;
  created_at: string;
}

export interface SuggestedFix {
  title: string;
  description: string;
  confidence: number;
  fix_type: string;
  code_changes?: string;
}

// API Request/Response types
export interface BuildRequest {
  project_path: string;
  command: string;
  config: CLIConfig;
  git_context?: GitContext;
  execution_mode: ExecutionMode;
  triggered_from: string;
}

export interface BuildResponse {
  build_id: string;
  web_url: string;
  status: string;
}

export interface CancelBuildRequest {
  build_id: string;
}

export interface BuildStatusResponse {
  build: Build;
  logs_available: boolean;
  web_url: string;
}

export interface LogsRequest {
  build_id: string;
  start_line?: number;
  end_line?: number;
}

export interface LogsResponse {
  build_id: string;
  logs: LogLine[];
  total_lines: number;
  has_more: boolean;
}

export interface LogLine {
  line_number: number;
  timestamp?: string;
  content: string;
  level?: string;
}

export interface ListProjectsRequest {
  limit?: number;
  offset?: number;
}

export interface ListProjectsResponse {
  projects: ProjectSummary[];
  total: number;
  has_more: boolean;
}

export interface ProjectSummary {
  project: Project;
  last_build_time?: string;
  last_build_status?: string;
  total_builds: number;
}

export interface ProjectHistoryRequest {
  project_id: string;
  limit?: number;
  offset?: number;
  status_filter?: string;
  branch_filter?: string;
}

export interface ProjectHistoryResponse {
  builds: Build[];
  total: number;
  has_more: boolean;
}

export interface HealthResponse {
  status: string;
  version: string;
  uptime_seconds: number;
  active_builds: number;
  database_connected: boolean;
}

export interface ErrorResponse {
  error: string;
  code: string;
  details?: unknown;
}

// WebSocket message types
export type WebSocketMessage =
  | { type: 'BuildStarted'; data: { build_id: string } }
  | { type: 'BuildLog'; data: { build_id: string; line: LogLine } }
  | { type: 'BuildCompleted'; data: { build_id: string; exit_code: number } }
  | { type: 'BuildCancelled'; data: { build_id: string } }
  | { type: 'BuildError'; data: { build_id: string; error: string } }
  | { type: 'Subscribe'; data: { build_id: string } }
  | { type: 'Unsubscribe'; data: { build_id: string } }
  | { type: 'Ping'; data: null }
  | { type: 'Pong'; data: null };