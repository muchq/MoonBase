import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import { BrowserRouter } from 'react-router-dom'
import BuildViewer from '../BuildViewer'
import type { Build, LogsResponse } from '../../types/api'

// Mock useParams to return a test ID
vi.mock('react-router-dom', async () => {
  const actual = await vi.importActual('react-router-dom')
  return {
    ...actual,
    useParams: () => ({ id: 'test-build-123' })
  }
})

// Mock fetch
const mockFetch = vi.fn()
global.fetch = mockFetch

const renderWithRouter = (component: React.ReactElement) => {
  return render(
    <BrowserRouter>
      {component}
    </BrowserRouter>
  )
}

const mockBuild: Build = {
  id: 'test-build-123',
  project_id: 'project-456',
  command: 'bazel test //...',
  status: 'completed',
  execution_mode: 'async',
  environment: 'native',
  triggered_from: 'cli',
  start_time: '2024-01-01T10:00:00Z',
  end_time: '2024-01-01T10:05:00Z',
  duration_ms: 300000,
  exit_code: 0,
  working_directory: '/path/to/project',
  logs_stored: true,
  git_context: {
    branch: 'main',
    commit_hash: 'abc123def456',
    commit_message: 'Add new feature',
    author: 'John Doe',
    has_uncommitted_changes: false
  }
}

const mockLogsResponse: LogsResponse = {
  build_id: 'test-build-123',
  logs: [
    {
      line_number: 1,
      timestamp: '2024-01-01T10:00:01Z',
      content: 'Starting build...',
      level: 'info'
    },
    {
      line_number: 2,
      timestamp: '2024-01-01T10:00:02Z',
      content: 'Running tests...',
      level: 'info'
    }
  ],
  total_lines: 2,
  has_more: false
}

describe('BuildViewer', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('displays loading state initially', () => {
    mockFetch.mockImplementation(() => new Promise(() => {})) // Never resolves
    
    renderWithRouter(<BuildViewer />)
    
    expect(screen.getByText('Loading build information...')).toBeInTheDocument()
  })

  it('renders build information correctly', async () => {
    mockFetch
      .mockResolvedValueOnce({
        ok: true,
        json: async () => ({ build: mockBuild })
      })
      .mockResolvedValueOnce({
        ok: false,
        status: 404
      })
      .mockResolvedValueOnce({
        ok: true,
        json: async () => mockLogsResponse
      })
    
    renderWithRouter(<BuildViewer />)
    
    await waitFor(() => {
      expect(screen.getByText('Build: bazel test //...')).toBeInTheDocument()
      expect(screen.getByText('COMPLETED')).toBeInTheDocument()
      expect(screen.getByText('ID: test-build-123')).toBeInTheDocument()
    })
  })

  it('displays git context information', async () => {
    mockFetch
      .mockResolvedValueOnce({
        ok: true,
        json: async () => ({ build: mockBuild })
      })
      .mockResolvedValueOnce({
        ok: false,
        status: 404
      })
      .mockResolvedValueOnce({
        ok: true,
        json: async () => mockLogsResponse
      })
    
    renderWithRouter(<BuildViewer />)
    
    await waitFor(() => {
      expect(screen.getByText('Git Context')).toBeInTheDocument()
      expect(screen.getByText('main')).toBeInTheDocument()
      expect(screen.getByText('abc123de')).toBeInTheDocument()
      expect(screen.getByText('John Doe')).toBeInTheDocument()
    })
  })

  it('displays build logs correctly', async () => {
    mockFetch
      .mockResolvedValueOnce({
        ok: true,
        json: async () => ({ build: mockBuild })
      })
      .mockResolvedValueOnce({
        ok: false,
        status: 404
      })
      .mockResolvedValueOnce({
        ok: true,
        json: async () => mockLogsResponse
      })
    
    renderWithRouter(<BuildViewer />)
    
    await waitFor(() => {
      expect(screen.getByText('Build Logs')).toBeInTheDocument()
      expect(screen.getByText('2 lines')).toBeInTheDocument()
      expect(screen.getByText('Starting build...')).toBeInTheDocument()
      expect(screen.getByText('Running tests...')).toBeInTheDocument()
    })
  })

  it('shows cancel button for running builds', async () => {
    const runningBuild = { ...mockBuild, status: 'running' as const, end_time: undefined }
    
    mockFetch
      .mockResolvedValueOnce({
        ok: true,
        json: async () => ({ build: runningBuild })
      })
      .mockResolvedValueOnce({
        ok: false,
        status: 404
      })
      .mockResolvedValueOnce({
        ok: true,
        json: async () => mockLogsResponse
      })
    
    renderWithRouter(<BuildViewer />)
    
    await waitFor(() => {
      expect(screen.getByText('Cancel Build')).toBeInTheDocument()
      expect(screen.getByText('RUNNING')).toBeInTheDocument()
    })
  })
})