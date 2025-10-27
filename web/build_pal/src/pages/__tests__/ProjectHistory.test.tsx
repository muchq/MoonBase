import { describe, it, expect, vi } from 'vitest'
import { render, screen } from '@testing-library/react'
import { BrowserRouter } from 'react-router-dom'
import ProjectHistory from '../ProjectHistory'

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
  it('renders the project history title', () => {
    renderWithRouter(<ProjectHistory />)
    
    expect(screen.getByText('Project History')).toBeInTheDocument()
  })

  it('displays the project ID from params', () => {
    renderWithRouter(<ProjectHistory />)
    
    expect(screen.getByText('Viewing history for project: test-project-456')).toBeInTheDocument()
  })

  it('renders recent builds section', () => {
    renderWithRouter(<ProjectHistory />)
    
    expect(screen.getByText('Recent Builds')).toBeInTheDocument()
    expect(screen.getByText('No builds found for this project.')).toBeInTheDocument()
  })
})