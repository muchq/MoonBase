import { describe, it, expect } from 'vitest'
import { 
  BuildStatus, 
  ExecutionMode, 
  Environment, 
  RetentionPolicy, 
  BuildTool,
  Build,
  Project,
  GitContext,
  CLIConfig,
  BuildRequest,
  BuildResponse,
  LogLine,
  getAvailableCommands,
  validateConfig,
  DEFAULT_CLI_CONFIG
} from '../types'

describe('Type Definitions', () => {
  it('should have correct enum values', () => {
    // Test that enum values match Rust serde expectations
    const buildStatus: BuildStatus = 'queued'
    const executionMode: ExecutionMode = 'async'
    const environment: Environment = 'native'
    const retentionPolicy: RetentionPolicy = 'all'
    const buildTool: BuildTool = 'bazel'

    expect(buildStatus).toBe('queued')
    expect(executionMode).toBe('async')
    expect(environment).toBe('native')
    expect(retentionPolicy).toBe('all')
    expect(buildTool).toBe('bazel')
  })

  it('should create valid Build objects', () => {
    const build: Build = {
      id: '123e4567-e89b-12d3-a456-426614174000',
      project_id: '123e4567-e89b-12d3-a456-426614174001',
      command: 'bazel build //...',
      status: 'running',
      execution_mode: 'async',
      environment: 'native',
      triggered_from: 'cli',
      start_time: '2023-01-01T00:00:00Z',
      working_directory: '/path/to/project',
      logs_stored: true
    }

    expect(build.id).toBeDefined()
    expect(build.status).toBe('running')
    expect(build.execution_mode).toBe('async')
  })

  it('should create valid Project objects', () => {
    const project: Project = {
      id: '123e4567-e89b-12d3-a456-426614174000',
      name: 'test-project',
      path: '/path/to/project',
      tool_type: 'bazel',
      created_at: '2023-01-01T00:00:00Z',
      updated_at: '2023-01-01T00:00:00Z'
    }

    expect(project.name).toBe('test-project')
    expect(project.tool_type).toBe('bazel')
  })

  it('should create valid GitContext objects', () => {
    const gitContext: GitContext = {
      branch: 'main',
      commit_hash: 'abc123',
      commit_message: 'Initial commit',
      author: 'test@example.com',
      has_uncommitted_changes: false
    }

    expect(gitContext.branch).toBe('main')
    expect(gitContext.has_uncommitted_changes).toBe(false)
  })

  it('should create valid CLIConfig objects', () => {
    const config: CLIConfig = {
      tool: 'bazel',
      name: 'test-project',
      mode: 'async',
      retention: 'all',
      retention_duration_days: 7,
      environment: 'native'
    }

    expect(config.tool).toBe('bazel')
    expect(config.mode).toBe('async')
    expect(config.retention_duration_days).toBe(7)
  })

  it('should create valid BuildRequest objects', () => {
    const request: BuildRequest = {
      project_path: '/path/to/project',
      command: 'bazel build //...',
      config: DEFAULT_CLI_CONFIG,
      execution_mode: 'async',
      triggered_from: 'cli'
    }

    expect(request.command).toBe('bazel build //...')
    expect(request.triggered_from).toBe('cli')
  })

  it('should create valid BuildResponse objects', () => {
    const response: BuildResponse = {
      build_id: '123e4567-e89b-12d3-a456-426614174000',
      web_url: 'http://localhost:8080/builds/123',
      status: 'created'
    }

    expect(response.build_id).toBeDefined()
    expect(response.web_url).toContain('localhost')
  })

  it('should create valid LogLine objects', () => {
    const logLine: LogLine = {
      line_number: 1,
      content: 'Starting build...',
      timestamp: '2023-01-01T00:00:00Z',
      level: 'info'
    }

    expect(logLine.line_number).toBe(1)
    expect(logLine.content).toBe('Starting build...')
  })
})

describe('Utility Functions', () => {
  it('should return correct available commands for each build tool', () => {
    expect(getAvailableCommands('bazel')).toContain('build')
    expect(getAvailableCommands('bazel')).toContain('test')
    
    expect(getAvailableCommands('maven')).toContain('compile')
    expect(getAvailableCommands('maven')).toContain('package')
    
    expect(getAvailableCommands('gradle')).toContain('build')
    expect(getAvailableCommands('gradle')).toContain('assemble')
  })

  it('should validate config correctly', () => {
    const validConfig: CLIConfig = {
      tool: 'bazel',
      name: 'test-project',
      mode: 'async',
      retention: 'all',
      retention_duration_days: 7,
      environment: 'native'
    }

    expect(validateConfig(validConfig)).toHaveLength(0)

    const invalidConfig: CLIConfig = {
      ...validConfig,
      name: ''
    }

    const errors = validateConfig(invalidConfig)
    expect(errors).toContain('Project name cannot be empty')
  })

  it('should validate retention duration', () => {
    const configWithZeroDuration: CLIConfig = {
      tool: 'bazel',
      name: 'test-project',
      mode: 'async',
      retention: 'all',
      retention_duration_days: 0,
      environment: 'native'
    }

    const errors = validateConfig(configWithZeroDuration)
    expect(errors).toContain('Retention duration must be greater than 0 days')
  })

  it('should validate docker config', () => {
    const configWithEmptyDockerImage: CLIConfig = {
      tool: 'bazel',
      name: 'test-project',
      mode: 'async',
      retention: 'all',
      retention_duration_days: 7,
      environment: 'docker',
      docker: {
        image: '',
        workdir: '/workspace',
        volumes: [],
        environment: {},
        rsync_options: [],
        sync_strategy: 'pre-build'
      }
    }

    const errors = validateConfig(configWithEmptyDockerImage)
    expect(errors).toContain('Docker image cannot be empty')
  })

  it('should validate AI config', () => {
    const configWithEmptyAIKey: CLIConfig = {
      tool: 'bazel',
      name: 'test-project',
      mode: 'async',
      retention: 'all',
      environment: 'native',
      ai: {
        enabled: true,
        provider: 'openai',
        api_key: '',
        model: 'gpt-4'
      }
    }

    const errors = validateConfig(configWithEmptyAIKey)
    expect(errors).toContain('AI API key cannot be empty when AI is enabled')
  })
})