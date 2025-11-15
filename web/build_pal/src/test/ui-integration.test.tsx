import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { render, screen, waitFor, fireEvent } from '@testing-library/react'
import { BrowserRouter, MemoryRouter } from 'react-router-dom'
import userEvent from '@testing-library/user-event'
import App from '../App'
import type { 
  ListProjectsResponse, 
  ProjectSummary, 
  Build, 
  BuildStatusResponse,
  LogsResponse,
  BuildSummary 
} from '../types/api'

// Mock fetch globally
const mockFetch = vi.fn()
global.fetch = mockFetch

// Test data
const mockProject: ProjectSummary = {
  project: {
    id: 'project-123',
    name: 'Test Project',
    path: '/path/to/test/project',
    tool_type: 'bazel',
    description: 'A test project for integration testing',
    created_at: '2024-01-01T10:00:00Z',
    updated_at: '2024-01-01T10:00:00Z'
  },
  last_build_time: '2024-01-01T12:00:00Z',
  last_build_status: 'completed',
  total_builds: 3
}

const mockBuild: Build = {
  id: 'build-456',
  project_id: 'project-123',
  command: 'bazel test //...',
  status: 'completed',
  execution_mode: 'async',
  environment: 'native',
  triggered_from: 'cli',
  start_time: '2024-01-01T12:00:00Z',
  end_time: '2024-01-01T12:05:00Z',
  duration_ms: 300000,
  exit_code: 0,
  working_directory: '/path/to/test/project',
  logs_stored: true,
  git_context: {
    branch: 'main',
    commit_hash: 'abc123def456789',
    commit_message: 'Add integration tests',
    author: 'Test User <test@example.com>',
    has_uncommitted_changes: false
  }
}

const mockBuildSummary: BuildSummary = {
  build: mockBuild,
  error_count: 0,
  warning_count: 2,
  parsed_errors: [
    {
      error_type: 'warning',
      severity: 'warning',
      message: 'Deprecated API usage detected',
      file: 'src/main.rs',
      line: 42,
      column: 10
    }
  ],
  test_results: {
    total: 15,
    passed: 15,
    failed: 0,
    failed_tests: [],
    skipped: 0
  }
}

const mockLogs: LogsResponse = {
  build_id: 'build-456',
  logs: [
    {
      line_number: 1,
      timestamp: '2024-01-01T12:00:01Z',
      content: 'Starting build...',
      level: 'info'
    },
    {
      line_number: 2,
      timestamp: '2024-01-01T12:00:02Z',
      content: 'Loading BUILD files...',
      level: 'info'
    },
    {
      line_number: 3,
      timestamp: '2024-01-01T12:00:03Z',
      content: 'Running tests...',
      level: 'info'
    },
    {
      line_number: 4,
      timestamp: '2024-01-01T12:05:00Z',
      content: 'Build completed successfully',
      level: 'info'
    }
  ],
  total_lines: 4,
  has_more: false
}

describe('UI Integration Tests', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  afterEach(() => {
    vi.clearAllTimers()
  })

  describe('Dashboard to Project Navigation', () => {
    it('should navigate from dashboard to project history when clicking on a project', async () => {
      // Mock API responses
      mockFetch
        .mockResolvedValueOnce({
          ok: true,
          json: async () => ({
            projects: [mockProject],
            total: 1,
            has_more: false
          } as ListProjectsResponse)
        })
        // Mock the project history API call
        .mockResolvedValueOnce({
          ok: true,
          json: async () => ({
            builds: []
          })
        })

      render(
        <MemoryRouter initialEntries={['/']}>
          <App />
        </MemoryRouter>
      )

      // Wait for dashboard to load
      await waitFor(() => {
        expect(screen.getByText('Build Pal Dashboard')).toBeInTheDocument()
      })

      // Wait for project to appear
      await waitFor(() => {
        expect(screen.getByText('Test Project')).toBeInTheDocument()
      })

      // Click on the project link (not the navigation links)
      const projectLink = screen.getByRole('link', { name: /Test Project/ })
      expect(projectLink).toHaveAttribute('href', '/projects/project-123')

      fireEvent.click(projectLink)

      // Should navigate to project history
      await waitFor(() => {
        expect(screen.getByText('Project History')).toBeInTheDocument()
        expect(screen.getByText('← Back to Dashboard')).toBeInTheDocument()
        expect(screen.getByText('Recent Builds (0)')).toBeInTheDocument()
      })
    })

    it('should display project status indicators correctly on dashboard', async () => {
      const failedProject: ProjectSummary = {
        ...mockProject,
        last_build_status: 'failed'
      }

      mockFetch.mockResolvedValueOnce({
        ok: true,
        json: async () => ({
          projects: [failedProject],
          total: 1,
          has_more: false
        })
      })

      render(
        <BrowserRouter>
          <App />
        </BrowserRouter>
      )

      await waitFor(() => {
        expect(screen.getByText('Status: Failed')).toBeInTheDocument()
      })
    })

    it('should handle empty project list correctly', async () => {
      mockFetch.mockResolvedValueOnce({
        ok: true,
        json: async () => ({
          projects: [],
          total: 0,
          has_more: false
        })
      })

      render(
        <BrowserRouter>
          <App />
        </BrowserRouter>
      )

      await waitFor(() => {
        expect(screen.getByText('No projects configured yet.')).toBeInTheDocument()
        // Check for the specific paragraph with the build_pal command
        expect(screen.getByText((_content, element) => {
          return element?.tagName === 'P' &&
                 element?.textContent?.includes('build_pal') &&
                 element?.textContent?.includes('in a project directory') || false
        })).toBeInTheDocument()
      })
    })
  })

  describe('Build Viewer Integration', () => {
    it('should display complete build information with logs', async () => {
      // Mock all API calls for build viewer
      mockFetch
        .mockResolvedValueOnce({
          ok: true,
          json: async () => ({ build: mockBuild } as BuildStatusResponse)
        })
        .mockResolvedValueOnce({
          ok: true,
          json: async () => mockBuildSummary
        })
        .mockResolvedValueOnce({
          ok: true,
          json: async () => mockLogs
        })

      render(
        <MemoryRouter initialEntries={['/builds/build-456']}>
          <App />
        </MemoryRouter>
      )

      // Wait for build information to load
      await waitFor(() => {
        expect(screen.getByText('Build: bazel test //...')).toBeInTheDocument()
      })

      // Check build status
      expect(screen.getByText('COMPLETED')).toBeInTheDocument()
      expect(screen.getByText('ID: build-456')).toBeInTheDocument()

      // Check git context
      await waitFor(() => {
        expect(screen.getByText('Git Context')).toBeInTheDocument()
        expect(screen.getByText('main')).toBeInTheDocument()
        expect(screen.getByText('abc123de')).toBeInTheDocument()
        expect(screen.getByText('Test User <test@example.com>')).toBeInTheDocument()
      })

      // Check build summary
      await waitFor(() => {
        expect(screen.getByText('Build Summary')).toBeInTheDocument()
        expect(screen.getByText('Errors:')).toBeInTheDocument()
        expect(screen.getByText('Warnings:')).toBeInTheDocument()
      })

      // Check logs
      await waitFor(() => {
        expect(screen.getByText('Build Logs')).toBeInTheDocument()
        expect(screen.getByText('4 lines')).toBeInTheDocument()
        expect(screen.getByText('Starting build...')).toBeInTheDocument()
        expect(screen.getByText('Build completed successfully')).toBeInTheDocument()
      })
    })

    it('should show cancel button for running builds', async () => {
      const runningBuild: Build = {
        ...mockBuild,
        status: 'running',
        end_time: undefined,
        duration_ms: undefined,
        exit_code: undefined
      }

      mockFetch
        .mockResolvedValueOnce({
          ok: true,
          json: async () => ({ build: runningBuild })
        })
        .mockResolvedValueOnce({
          ok: true,
          json: async () => mockLogs
        })

      render(
        <MemoryRouter initialEntries={['/builds/build-456']}>
          <App />
        </MemoryRouter>
      )

      await waitFor(() => {
        expect(screen.getByText('RUNNING')).toBeInTheDocument()
        expect(screen.getByText('Cancel Build')).toBeInTheDocument()
      })
    })

    it('should handle build cancellation', async () => {
      const runningBuild: Build = {
        ...mockBuild,
        status: 'running',
        end_time: undefined
      }

      // Initial fetch for running build
      mockFetch
        .mockResolvedValueOnce({
          ok: true,
          json: async () => ({ build: runningBuild })
        })
        .mockResolvedValueOnce({
          ok: true,
          json: async () => mockLogs
        })
        // Cancel request
        .mockResolvedValueOnce({
          ok: true
        })
        // Refresh after cancel
        .mockResolvedValueOnce({
          ok: true,
          json: async () => ({ build: { ...runningBuild, status: 'cancelled' } })
        })

      const user = userEvent.setup()

      render(
        <MemoryRouter initialEntries={['/builds/build-456']}>
          <App />
        </MemoryRouter>
      )

      await waitFor(() => {
        expect(screen.getByText('Cancel Build')).toBeInTheDocument()
      })

      // Click cancel button
      const cancelButton = screen.getByText('Cancel Build')
      await user.click(cancelButton)

      // Verify DELETE request was made
      await waitFor(() => {
        expect(mockFetch).toHaveBeenCalledWith(
          'http://localhost:8080/api/builds/build-456',
          { method: 'DELETE' }
        )
      })
    })

    it('should handle build not found error', async () => {
      mockFetch.mockResolvedValueOnce({
        ok: false,
        status: 404,
        statusText: 'Not Found'
      })

      render(
        <MemoryRouter initialEntries={['/builds/nonexistent']}>
          <App />
        </MemoryRouter>
      )

      await waitFor(() => {
        expect(screen.getByText('Error')).toBeInTheDocument()
        expect(screen.getByText(/Failed to fetch build: Not Found/)).toBeInTheDocument()
      })
    })
  })

  describe('Real-time Updates Simulation', () => {
    it('should show live indicator for running builds', async () => {
      const runningBuild: Build = {
        ...mockBuild,
        status: 'running',
        end_time: undefined
      }

      mockFetch
        .mockResolvedValueOnce({
          ok: true,
          json: async () => ({ build: runningBuild })
        })
        .mockResolvedValueOnce({
          ok: true,
          json: async () => mockLogs
        })

      render(
        <MemoryRouter initialEntries={['/builds/build-456']}>
          <App />
        </MemoryRouter>
      )

      await waitFor(() => {
        expect(screen.getByText('RUNNING')).toBeInTheDocument()
      })

      // Should show live indicator
      await waitFor(() => {
        expect(screen.getByText('● Live')).toBeInTheDocument()
      })
    })

    it('should not show live indicator for completed builds', async () => {
      mockFetch
        .mockResolvedValueOnce({
          ok: true,
          json: async () => ({ build: mockBuild })
        })
        .mockResolvedValueOnce({
          ok: true,
          json: async () => mockBuildSummary
        })
        .mockResolvedValueOnce({
          ok: true,
          json: async () => mockLogs
        })

      render(
        <MemoryRouter initialEntries={['/builds/build-456']}>
          <App />
        </MemoryRouter>
      )

      await waitFor(() => {
        expect(screen.getByText('COMPLETED')).toBeInTheDocument()
      })

      // Should not show live indicator
      expect(screen.queryByText('● Live')).not.toBeInTheDocument()
    })
  })

  describe('Error Handling Integration', () => {
    it('should handle API errors gracefully across components', async () => {
      // Dashboard API error
      mockFetch.mockRejectedValueOnce(new Error('Network error'))

      render(
        <BrowserRouter>
          <App />
        </BrowserRouter>
      )

      await waitFor(() => {
        expect(screen.getByText(/Error: Network error/)).toBeInTheDocument()
      }, { timeout: 10000 })
    })

    it('should handle server errors with proper status codes', async () => {
      mockFetch.mockResolvedValueOnce({
        ok: false,
        status: 500
      })

      render(
        <BrowserRouter>
          <App />
        </BrowserRouter>
      )

      await waitFor(() => {
        expect(screen.getByText(/Failed to fetch projects: 500/)).toBeInTheDocument()
      }, { timeout: 10000 })
    })
  })

  describe('Navigation Integration', () => {
    it('should handle direct navigation to build viewer', async () => {
      mockFetch
        .mockResolvedValueOnce({
          ok: true,
          json: async () => ({ build: mockBuild })
        })
        .mockResolvedValueOnce({
          ok: true,
          json: async () => mockBuildSummary
        })
        .mockResolvedValueOnce({
          ok: true,
          json: async () => mockLogs
        })

      render(
        <MemoryRouter initialEntries={['/builds/build-456']}>
          <App />
        </MemoryRouter>
      )

      await waitFor(() => {
        expect(screen.getByText('Build: bazel test //...')).toBeInTheDocument()
        expect(screen.getByText('COMPLETED')).toBeInTheDocument()
      }, { timeout: 10000 })
    })

    it('should handle invalid routes gracefully', async () => {
      render(
        <MemoryRouter initialEntries={['/invalid-route']}>
          <App />
        </MemoryRouter>
      )

      // Should still render the layout
      expect(screen.getByText('Build Pal')).toBeInTheDocument()
    })
  })

  describe('Data Flow Integration', () => {
    it('should properly serialize and deserialize API data', async () => {
      const complexProject: ProjectSummary = {
        project: {
          id: 'complex-project',
          name: 'Complex Project',
          path: '/path/with spaces/and-special-chars',
          tool_type: 'maven',
          description: 'A project with complex data',
          created_at: '2024-01-01T10:00:00Z',
          updated_at: '2024-01-01T10:00:00Z'
        },
        last_build_time: '2024-01-01T12:00:00Z',
        last_build_status: 'failed',
        total_builds: 0
      }

      mockFetch.mockResolvedValueOnce({
        ok: true,
        json: async () => ({
          projects: [complexProject],
          total: 1,
          has_more: false
        })
      })

      render(
        <BrowserRouter>
          <App />
        </BrowserRouter>
      )

      await waitFor(() => {
        expect(screen.getByText('Complex Project')).toBeInTheDocument()
        expect(screen.getByText('MAVEN')).toBeInTheDocument()
        expect(screen.getByText('Status: Failed')).toBeInTheDocument()
      }, { timeout: 10000 })
    })

    it('should handle optional fields correctly', async () => {
      const minimalProject: ProjectSummary = {
        project: {
          id: 'minimal-project',
          name: 'Minimal Project',
          path: '/minimal',
          tool_type: 'gradle',
          created_at: '2024-01-01T10:00:00Z',
          updated_at: '2024-01-01T10:00:00Z'
          // description is optional and omitted
        },
        // last_build_time and last_build_status are optional and omitted
        total_builds: 0
      }

      mockFetch.mockResolvedValueOnce({
        ok: true,
        json: async () => ({
          projects: [minimalProject],
          total: 1,
          has_more: false
        })
      })

      render(
        <BrowserRouter>
          <App />
        </BrowserRouter>
      )

      await waitFor(() => {
        expect(screen.getByText('Minimal Project')).toBeInTheDocument()
        expect(screen.getByText('GRADLE')).toBeInTheDocument()
        expect(screen.getByText('Status: No builds')).toBeInTheDocument()
        expect(screen.getByText('Never')).toBeInTheDocument()
      }, { timeout: 10000 })
    })
  })
})