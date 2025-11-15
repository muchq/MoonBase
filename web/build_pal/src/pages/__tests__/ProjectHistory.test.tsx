import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import { BrowserRouter } from 'react-router-dom'
import ProjectHistory from '../ProjectHistory'
import type { ProjectHistoryResponse } from '../../types/api'

// Mock fetch globally
const mockFetch = vi.fn()
global.fetch = mockFetch

// Mock useParams to return a test ID
vi.mock('react-router-dom', async () => {
  const actual = await vi.importActual('react-router-dom')
  return {
    ...actual,
    useParams: () => ({ id: 'test-project-456' })
  }
})

const renderWithRouter = (component: React.ReactElement) => {
  return render(
    <BrowserRouter>
      {component}
    </BrowserRouter>
  )
}

describe('ProjectHistory', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('renders the project history title', async () => {
    mockFetch.mockResolvedValueOnce({
      ok: true,
      json: async () => ({
        builds: []
      } as ProjectHistoryResponse)
    })

    renderWithRouter(<ProjectHistory />)

    expect(screen.getByText('Project History')).toBeInTheDocument()

    // Wait for loading to complete
    await waitFor(() => {
      expect(screen.queryByText('Loading builds...')).not.toBeInTheDocument()
    })
  })

  it('displays the back to dashboard link', async () => {
    mockFetch.mockResolvedValueOnce({
      ok: true,
      json: async () => ({
        builds: []
      } as ProjectHistoryResponse)
    })

    renderWithRouter(<ProjectHistory />)

    await waitFor(() => {
      expect(screen.getByText('← Back to Dashboard')).toBeInTheDocument()
    })
  })

  it('renders recent builds section', async () => {
    mockFetch.mockResolvedValueOnce({
      ok: true,
      json: async () => ({
        builds: []
      } as ProjectHistoryResponse)
    })

    renderWithRouter(<ProjectHistory />)

    await waitFor(() => {
      expect(screen.getByText('Recent Builds (0)')).toBeInTheDocument()
      expect(screen.getByText('No builds found for this project.')).toBeInTheDocument()
    })
  })
})