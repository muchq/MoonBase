import { describe, it, expect } from 'vitest'
import { render, screen } from '@testing-library/react'
import { BrowserRouter } from 'react-router-dom'
import App from '../App'

const renderWithRouter = (component: React.ReactElement) => {
  return render(
    <BrowserRouter>
      {component}
    </BrowserRouter>
  )
}

describe('App', () => {
  it('renders the app with layout', () => {
    renderWithRouter(<App />)
    
    // Should render the Layout component with Build Pal brand
    expect(screen.getByText('Build Pal')).toBeInTheDocument()
  })

  it('renders the dashboard by default', () => {
    renderWithRouter(<App />)
    
    // Should render the Dashboard component by default
    expect(screen.getByText('Build Pal Dashboard')).toBeInTheDocument()
  })
})