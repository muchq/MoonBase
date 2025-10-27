import React, { useState, useEffect } from 'react'
import { Link } from 'react-router-dom'
import type { ProjectSummary, ListProjectsResponse } from '../types/api'

const Dashboard: React.FC = () => {
  const [projects, setProjects] = useState<ProjectSummary[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState<string | null>(null)

  useEffect(() => {
    const fetchProjects = async () => {
      try {
        const response = await fetch('/api/projects')
        if (!response.ok) {
          throw new Error(`Failed to fetch projects: ${response.status}`)
        }
        const data: ListProjectsResponse = await response.json()
        setProjects(data.projects)
      } catch (err) {
        setError(err instanceof Error ? err.message : 'Failed to load projects')
      } finally {
        setLoading(false)
      }
    }

    fetchProjects()
  }, [])

  const getStatusColor = (status?: string) => {
    switch (status) {
      case 'completed':
        return '#22c55e' // green
      case 'failed':
        return '#ef4444' // red
      case 'running':
        return '#3b82f6' // blue
      case 'cancelled':
        return '#6b7280' // gray
      default:
        return '#6b7280' // gray
    }
  }

  const getStatusText = (status?: string) => {
    if (!status) return 'No builds'
    return status.charAt(0).toUpperCase() + status.slice(1)
  }

  const formatLastBuildTime = (time?: string) => {
    if (!time) return 'Never'
    const date = new Date(time)
    const now = new Date()
    const diffMs = now.getTime() - date.getTime()
    const diffHours = Math.floor(diffMs / (1000 * 60 * 60))
    const diffDays = Math.floor(diffHours / 24)

    if (diffDays > 0) {
      return `${diffDays} day${diffDays > 1 ? 's' : ''} ago`
    } else if (diffHours > 0) {
      return `${diffHours} hour${diffHours > 1 ? 's' : ''} ago`
    } else {
      return 'Recently'
    }
  }

  if (loading) {
    return (
      <div>
        <h1>Build Pal Dashboard</h1>
        <p>Loading projects...</p>
      </div>
    )
  }

  if (error) {
    return (
      <div>
        <h1>Build Pal Dashboard</h1>
        <p style={{ color: '#ef4444' }}>Error: {error}</p>
        <p>Make sure the Build Pal server is running.</p>
      </div>
    )
  }

  return (
    <div>
      <h1>Build Pal Dashboard</h1>
      <p>Welcome to Build Pal - your unified build monitoring tool.</p>
      
      <div style={{ marginTop: '2rem' }}>
        <h2>Projects</h2>
        
        {projects.length === 0 ? (
          <div style={{ 
            padding: '2rem', 
            border: '1px solid #e5e7eb', 
            borderRadius: '8px',
            backgroundColor: '#f9fafb',
            textAlign: 'center'
          }}>
            <p>No projects configured yet.</p>
            <p>Run <code style={{ 
              backgroundColor: '#e5e7eb', 
              padding: '0.25rem 0.5rem', 
              borderRadius: '4px' 
            }}>build_pal</code> in a project directory to get started.</p>
          </div>
        ) : (
          <div style={{
            display: 'grid',
            gridTemplateColumns: 'repeat(auto-fill, minmax(300px, 1fr))',
            gap: '1rem',
            marginTop: '1rem'
          }}>
            {projects.map((projectSummary) => (
              <Link
                key={projectSummary.project.id}
                to={`/projects/${projectSummary.project.id}`}
                style={{
                  textDecoration: 'none',
                  color: 'inherit'
                }}
              >
                <div style={{
                  border: '1px solid #e5e7eb',
                  borderRadius: '8px',
                  padding: '1rem',
                  backgroundColor: '#ffffff',
                  cursor: 'pointer',
                  transition: 'box-shadow 0.2s',
                  ':hover': {
                    boxShadow: '0 4px 6px -1px rgba(0, 0, 0, 0.1)'
                  }
                }}
                onMouseEnter={(e) => {
                  e.currentTarget.style.boxShadow = '0 4px 6px -1px rgba(0, 0, 0, 0.1)'
                }}
                onMouseLeave={(e) => {
                  e.currentTarget.style.boxShadow = 'none'
                }}
                >
                  <div style={{
                    display: 'flex',
                    justifyContent: 'space-between',
                    alignItems: 'flex-start',
                    marginBottom: '0.5rem'
                  }}>
                    <h3 style={{ 
                      margin: 0, 
                      fontSize: '1.125rem',
                      fontWeight: '600'
                    }}>
                      {projectSummary.project.name}
                    </h3>
                    <div style={{
                      width: '12px',
                      height: '12px',
                      borderRadius: '50%',
                      backgroundColor: getStatusColor(projectSummary.last_build_status),
                      flexShrink: 0,
                      marginLeft: '0.5rem'
                    }} />
                  </div>
                  
                  <p style={{ 
                    margin: '0 0 0.5rem 0', 
                    fontSize: '0.875rem',
                    color: '#6b7280'
                  }}>
                    {projectSummary.project.tool_type.toUpperCase()}
                  </p>
                  
                  <p style={{ 
                    margin: '0 0 0.5rem 0', 
                    fontSize: '0.875rem',
                    color: '#374151',
                    wordBreak: 'break-all'
                  }}>
                    {projectSummary.project.path}
                  </p>
                  
                  <div style={{
                    display: 'flex',
                    justifyContent: 'space-between',
                    alignItems: 'center',
                    fontSize: '0.75rem',
                    color: '#6b7280',
                    marginTop: '1rem'
                  }}>
                    <span>
                      Status: {getStatusText(projectSummary.last_build_status)}
                    </span>
                    <span>
                      {formatLastBuildTime(projectSummary.last_build_time)}
                    </span>
                  </div>
                  
                  <div style={{
                    fontSize: '0.75rem',
                    color: '#6b7280',
                    marginTop: '0.25rem'
                  }}>
                    {projectSummary.total_builds} build{projectSummary.total_builds !== 1 ? 's' : ''}
                  </div>
                </div>
              </Link>
            ))}
          </div>
        )}
      </div>
    </div>
  )
}

export default Dashboard