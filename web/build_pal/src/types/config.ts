// TypeScript interfaces corresponding to Rust config types

import { BuildTool, Environment, ExecutionMode, RetentionPolicy, SyncStrategy } from './api';

export interface CLIConfig {
  tool: BuildTool;
  name: string;
  description?: string;
  mode: ExecutionMode;
  retention: RetentionPolicy;
  retention_duration_days?: number;
  environment: Environment;
  parsing?: PluginConfig;
  docker?: DockerConfig;
  ai?: AIConfig;
}

export interface PluginConfig {
  plugin: string;
  config?: Record<string, unknown>;
}

export interface DockerConfig {
  image: string;
  workdir: string;
  volumes: string[];
  environment: Record<string, string>;
  rsync_options: string[];
  sync_strategy: SyncStrategy;
}

export interface AIConfig {
  enabled: boolean;
  provider: string;
  api_key: string;
  model: string;
  max_tokens?: number;
}

// Default configurations
export const DEFAULT_CLI_CONFIG: CLIConfig = {
  tool: 'bazel',
  name: 'unnamed-project',
  mode: 'async',
  retention: 'all',
  retention_duration_days: 7, // Default to 1 week
  environment: 'native',
};

export const DEFAULT_DOCKER_CONFIG: DockerConfig = {
  image: 'ubuntu:22.04',
  workdir: '/workspace',
  volumes: ['./:/workspace'],
  environment: {},
  rsync_options: [
    '--exclude=node_modules',
    '--exclude=.git',
    '--exclude=target/',
    '--exclude=build/',
    '--compress-level=6',
    '--preserve-perms',
  ],
  sync_strategy: 'pre-build',
};

// Utility functions
export function getAvailableCommands(tool: BuildTool): string[] {
  switch (tool) {
    case 'bazel':
      return ['build', 'test', 'run', 'clean', 'query'];
    case 'maven':
      return ['compile', 'test', 'package', 'install', 'clean', 'verify'];
    case 'gradle':
      return ['build', 'test', 'assemble', 'clean', 'check', 'run'];
    default:
      return [];
  }
}

export function validateConfig(config: CLIConfig): string[] {
  const errors: string[] = [];

  if (!config.name || config.name.trim() === '') {
    errors.push('Project name cannot be empty');
  }

  if (config.retention_duration_days !== undefined && config.retention_duration_days <= 0) {
    errors.push('Retention duration must be greater than 0 days');
  }

  if (config.docker) {
    if (!config.docker.image || config.docker.image.trim() === '') {
      errors.push('Docker image cannot be empty');
    }
    if (!config.docker.workdir || config.docker.workdir.trim() === '') {
      errors.push('Docker workdir cannot be empty');
    }
  }

  if (config.ai && config.ai.enabled) {
    if (!config.ai.api_key || config.ai.api_key.trim() === '') {
      errors.push('AI API key cannot be empty when AI is enabled');
    }
  }

  return errors;
}