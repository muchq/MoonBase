import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import { BrowserRouter } from 'react-router-dom'
import Dashboard from '../Dashboard'
import type { ListProjectsResponse, ProjectSummary } from '../../types/api'

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

const mockProjectSummary: ProjectSummary = {
  project: {
    id: 'project-123',
    name: 'My Awesome Project',
    path: '/path/to/project',
    tool_type: 'bazel',
    description: 'A test project',
    created_at: '2024-01-01T10:00:00Z',
    updated_at: '2024-01-01T10:00:00Z'
  },
  last_build_time: '2024-01-01T12:00:00Z',
  last_build_status: 'completed',
  total_builds: 5
}

const mockProjectsResponse: ListProjectsResponse = {
  projects: [mockProjectSummary],
  total: 1,
  has_more: false
}

describe('Dashboard', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('renders the dashboard title', () => {
    mockFetch.mockImplementation(() => new Promise(() => {})) // Never resolves
    
    renderWithRouter(<Dashboard />)
    
    expect(screen.getByText('Build Pal Dashboard')).toBeInTheDocument()
  })

  it('renders welcome message after loading', async () => {
    mockFetch.mockResolvedValueOnce({
      ok: true,
      json: async () => ({ projects: [], total: 0, has_more: false })
    })
    
    renderWithRouter(<Dashboard />)
    
    await waitFor(() => {
      expect(screen.getByText('Welcome to Build Pal - your unified build monitoring tool.')).toBeInTheDocument()
    })
  })

  it('shows loading state initially', () => {
    mockFetch.mockImplementation(() => new Promise(() => {})) // Never resolves
    
    renderWithRouter(<Dashboard />)
    
    expect(screen.getByText('Loading projects...')).toBeInTheDocument()
  })

  it('renders empty state when no projects exist', async () => {
    mockFetch.mockResolvedValueOnce({
      ok: true,
      json: async () => ({ projects: [], total: 0, has_more: false })
    })
    
    renderWithRouter(<Dashboard />)
    
    await waitFor(() => {
      expect(screen.getByText('Projects')).toBeInTheDocument()
      expect(screen.getByText('No projects configured yet.')).toBeInTheDocument()
      expect(screen.getByText((_content, element) => {
        return (element?.tagName === 'P' && element?.textContent?.includes('in a project directory to get started')) ?? false
      })).toBeInTheDocument()
    })
  })

  it('renders project grid when projects exist', async () => {
    mockFetch.mockResolvedValueOnce({
      ok: true,
      json: async () => mockProjectsResponse
    })
    
    renderWithRouter(<Dashboard />)
    
    await waitFor(() => {
      expect(screen.getByText('My Awesome Project')).toBeInTheDocument()
      expect(screen.getByText('BAZEL')).toBeInTheDocument()
      expect(screen.getByText('/path/to/project')).toBeInTheDocument()
      expect(screen.getByText('Status: Completed')).toBeInTheDocument()
      expect(screen.getByText('5 builds')).toBeInTheDocument()
    })
  })

  it('displays project status indicators correctly', async () => {
    const projectWithFailedBuild: ProjectSummary = {
      ...mockProjectSummary,
      last_build_status: 'failed'
    }
    
    mockFetch.mockResolvedValueOnce({
      ok: true,
      json: async () => ({
        projects: [projectWithFailedBuild],
        total: 1,
        has_more: false
      })
    })
    
    renderWithRouter(<Dashboard />)
    
    await waitFor(() => {
      expect(screen.getByText('Status: Failed')).toBeInTheDocument()
    })
  })

  it('displays running build status', async () => {
    const projectWithRunningBuild: ProjectSummary = {
      ...mockProjectSummary,
      last_build_status: 'running'
    }
    
    mockFetch.mockResolvedValueOnce({
      ok: true,
      json: async () => ({
        projects: [projectWithRunningBuild],
        total: 1,
        has_more: false
      })
    })
    
    renderWithRouter(<Dashboard />)
    
    await waitFor(() => {
      expect(screen.getByText('Status: Running')).toBeInTheDocument()
    })
  })

  it('handles projects with no builds', async () => {
    const projectWithNoBuilds: ProjectSummary = {
      ...mockProjectSummary,
      last_build_time: undefined,
      last_build_status: undefined,
      total_builds: 0
    }
    
    mockFetch.mockResolvedValueOnce({
      ok: true,
      json: async () => ({
        projects: [projectWithNoBuilds],
        total: 1,
        has_more: false
      })
    })
    
    renderWithRouter(<Dashboard />)
    
    await waitFor(() => {
      expect(screen.getByText('Status: No builds')).toBeInTheDocument()
      expect(screen.getByText('Never')).toBeInTheDocument()
      expect(screen.getByText('0 builds')).toBeInTheDocument()
    })
  })

  it('shows error state when API fails', async () => {
    mockFetch.mockRejectedValueOnce(new Error('Network error'))
    
    renderWithRouter(<Dashboard />)
    
    await waitFor(() => {
      expect(screen.getByText(/Error: Network error/)).toBeInTheDocument()
      expect(screen.getByText('Make sure the Build Pal server is running.')).toBeInTheDocument()
    })
  })

  it('shows error state when API returns non-ok status', async () => {
    mockFetch.mockResolvedValueOnce({
      ok: false,
      status: 500
    })
    
    renderWithRouter(<Dashboard />)
    
    await waitFor(() => {
      expect(screen.getByText(/Error: Failed to fetch projects: 500/)).toBeInTheDocument()
    })
  })

  it('creates navigation links to project history', async () => {
    mockFetch.mockResolvedValueOnce({
      ok: true,
      json: async () => mockProjectsResponse
    })
    
    renderWithRouter(<Dashboard />)
    
    await waitFor(() => {
      const projectLink = screen.getByRole('link')
      expect(projectLink).toHaveAttribute('href', '/projects/project-123')
    })
  })
})