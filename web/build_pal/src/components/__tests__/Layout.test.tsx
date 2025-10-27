import { describe, it, expect } from 'vitest'
import { render, screen } from '@testing-library/react'
import { BrowserRouter } from 'react-router-dom'
import Layout from '../Layout'

const renderWithRouter = (component: React.ReactElement) => {
  return render(
    <BrowserRouter>
      {component}
    </BrowserRouter>
  )
}

describe('Layout', () => {
  it('renders the Build Pal brand', () => {
    renderWithRouter(
      <Layout>
        <div>Test content</div>
      </Layout>
    )
    
    expect(screen.getByText('Build Pal')).toBeInTheDocument()
  })

  it('renders navigation links', () => {
    renderWithRouter(
      <Layout>
        <div>Test content</div>
      </Layout>
    )
    
    expect(screen.getByText('Dashboard')).toBeInTheDocument()
  })

  it('renders children content', () => {
    renderWithRouter(
      <Layout>
        <div>Test content</div>
      </Layout>
    )
    
    expect(screen.getByText('Test content')).toBeInTheDocument()
  })
})