import { describe, it, expect, beforeAll, afterAll } from 'vitest'
import { 
  BuildRequest, 
  BuildResponse, 
  CLIConfig, 
  DEFAULT_CLI_CONFIG,
  BuildStatusResponse,
  LogsResponse,
  HealthResponse
} from '../types'

// Mock server responses for integration testing
// In a real integration test, this would connect to the actual Rust server
describe('API Integration Tests', () => {
  const mockServerUrl = 'http://localhost:8080'
  
  beforeAll(async () => {
    // In real tests, this would start the Rust server
    console.log('Starting integration tests...')
  })

  afterAll(async () => {
    // In real tests, this would stop the Rust server
    console.log('Integration tests completed')
  })

  it('should handle build request/response serialization', () => {
    const config: CLIConfig = {
      ...DEFAULT_CLI_CONFIG,
      name: 'integration-test-project',
      retention_duration_days: 14
    }

    const buildRequest: BuildRequest = {
      project_path: '/path/to/test/project',
      command: 'bazel build //...',
      config,
      execution_mode: 'async',
      triggered_from: 'web'
    }

    // Test serialization
    const serialized = JSON.stringify(buildRequest)
    expect(serialized).toContain('integration-test-project')
    expect(serialized).toContain('retention_duration_days')
    expect(serialized).toContain('14')

    // Test deserialization
    const deserialized: BuildRequest = JSON.parse(serialized)
    expect(deserialized.config.name).toBe('integration-test-project')
    expect(deserialized.config.retention_duration_days).toBe(14)
    expect(deserialized.execution_mode).toBe('async')
  })

  it('should handle build response format', () => {
    const buildResponse: BuildResponse = {
      build_id: '123e4567-e89b-12d3-a456-426614174000',
      web_url: `${mockServerUrl}/builds/123e4567-e89b-12d3-a456-426614174000`,
      status: 'created'
    }

    const serialized = JSON.stringify(buildResponse)
    const deserialized: BuildResponse = JSON.parse(serialized)

    expect(deserialized.build_id).toBe(buildResponse.build_id)
    expect(deserialized.web_url).toContain('localhost:8080')
    expect(deserialized.status).toBe('created')
  })

  it('should handle build status response format', () => {
    const statusResponse: BuildStatusResponse = {
      build: {
        id: '123e4567-e89b-12d3-a456-426614174000',
        project_id: '123e4567-e89b-12d3-a456-426614174001',
        command: 'bazel test //...',
        status: 'running',
        execution_mode: 'async',
        environment: 'native',
        triggered_from: 'cli',
        start_time: '2023-01-01T00:00:00Z',
        working_directory: '/path/to/project',
        logs_stored: true
      },
      logs_available: true,
      web_url: `${mockServerUrl}/builds/123e4567-e89b-12d3-a456-426614174000`
    }

    const serialized = JSON.stringify(statusResponse)
    const deserialized: BuildStatusResponse = JSON.parse(serialized)

    expect(deserialized.build.status).toBe('running')
    expect(deserialized.logs_available).toBe(true)
    expect(deserialized.build.command).toBe('bazel test //...')
  })

  it('should handle logs response format', () => {
    const logsResponse: LogsResponse = {
      build_id: '123e4567-e89b-12d3-a456-426614174000',
      logs: [
        {
          line_number: 1,
          content: 'Starting build...',
          timestamp: '2023-01-01T00:00:01Z',
          level: 'info'
        },
        {
          line_number: 2,
          content: 'Loading BUILD files...',
          timestamp: '2023-01-01T00:00:02Z',
          level: 'info'
        }
      ],
      total_lines: 100,
      has_more: true
    }

    const serialized = JSON.stringify(logsResponse)
    const deserialized: LogsResponse = JSON.parse(serialized)

    expect(deserialized.logs).toHaveLength(2)
    expect(deserialized.logs[0].content).toBe('Starting build...')
    expect(deserialized.total_lines).toBe(100)
    expect(deserialized.has_more).toBe(true)
  })

  it('should handle health response format', () => {
    const healthResponse: HealthResponse = {
      status: 'healthy',
      version: '0.1.0',
      uptime_seconds: 3600,
      active_builds: 2,
      database_connected: true
    }

    const serialized = JSON.stringify(healthResponse)
    const deserialized: HealthResponse = JSON.parse(serialized)

    expect(deserialized.status).toBe('healthy')
    expect(deserialized.version).toBe('0.1.0')
    expect(deserialized.active_builds).toBe(2)
    expect(deserialized.database_connected).toBe(true)
  })

  it('should validate enum values match Rust expectations', () => {
    // Test that our TypeScript enums match what Rust expects
    const config: CLIConfig = {
      tool: 'bazel',
      name: 'enum-test',
      mode: 'sync',
      retention: 'error',
      retention_duration_days: 30,
      environment: 'docker'
    }

    const serialized = JSON.stringify(config)
    
    // Verify lowercase serialization (serde rename_all = "lowercase")
    expect(serialized).toContain('"tool":"bazel"')
    expect(serialized).toContain('"mode":"sync"')
    expect(serialized).toContain('"retention":"error"')
    expect(serialized).toContain('"environment":"docker"')
  })

  it('should handle optional fields correctly', () => {
    // Test config with minimal fields
    const minimalConfig: CLIConfig = {
      tool: 'maven',
      name: 'minimal-project',
      mode: 'async',
      retention: 'all',
      environment: 'native'
      // retention_duration_days is optional and omitted
    }

    const serialized = JSON.stringify(minimalConfig)
    const deserialized: CLIConfig = JSON.parse(serialized)

    expect(deserialized.retention_duration_days).toBeUndefined()
    expect(deserialized.description).toBeUndefined()
    expect(deserialized.docker).toBeUndefined()
    expect(deserialized.ai).toBeUndefined()
  })

  it('should handle git context serialization', () => {
    const buildRequest: BuildRequest = {
      project_path: '/path/to/project',
      command: 'gradle build',
      config: DEFAULT_CLI_CONFIG,
      execution_mode: 'sync',
      triggered_from: 'cli',
      git_context: {
        branch: 'feature/new-feature',
        commit_hash: 'abc123def456',
        commit_message: 'Add new feature',
        author: 'developer@example.com',
        has_uncommitted_changes: true,
        diff: 'diff --git a/file.txt...'
      }
    }

    const serialized = JSON.stringify(buildRequest)
    const deserialized: BuildRequest = JSON.parse(serialized)

    expect(deserialized.git_context?.branch).toBe('feature/new-feature')
    expect(deserialized.git_context?.has_uncommitted_changes).toBe(true)
    expect(deserialized.git_context?.author).toBe('developer@example.com')
  })
})

// Contract validation tests
describe('API Contract Validation', () => {
  it('should ensure all required fields are present in BuildRequest', () => {
    const requiredFields = [
      'project_path',
      'command', 
      'config',
      'execution_mode',
      'triggered_from'
    ]

    const buildRequest: BuildRequest = {
      project_path: '/test',
      command: 'test',
      config: DEFAULT_CLI_CONFIG,
      execution_mode: 'async',
      triggered_from: 'test'
    }

    const serialized = JSON.stringify(buildRequest)
    
    requiredFields.forEach(field => {
      expect(serialized).toContain(`"${field}"`)
    })
  })

  it('should ensure all required fields are present in CLIConfig', () => {
    const requiredFields = [
      'tool',
      'name',
      'mode',
      'retention',
      'environment'
    ]

    const config = DEFAULT_CLI_CONFIG
    const serialized = JSON.stringify(config)
    
    requiredFields.forEach(field => {
      expect(serialized).toContain(`"${field}"`)
    })
  })

  it('should handle snake_case to camelCase field mapping', () => {
    // Verify that our TypeScript uses snake_case to match Rust serde defaults
    const config: CLIConfig = {
      ...DEFAULT_CLI_CONFIG,
      retention_duration_days: 7 // snake_case in TypeScript to match Rust
    }

    const serialized = JSON.stringify(config)
    expect(serialized).toContain('retention_duration_days')
    expect(serialized).not.toContain('retentionDurationDays')
  })
})