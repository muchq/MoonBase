import React, { useState, useEffect } from 'react'
import { useParams, Link } from 'react-router-dom'
import type { Build, ProjectHistoryResponse } from '../types/api'

const ProjectHistory: React.FC = () => {
  const { id } = useParams<{ id: string }>()
  const [builds, setBuilds] = useState<Build[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState<string | null>(null)

  useEffect(() => {
    const fetchBuilds = async () => {
      if (!id) return

      try {
        const response = await fetch(`/api/projects/${id}/history`)
        if (!response.ok) {
          throw new Error(`Failed to fetch builds: ${response.status}`)
        }
        const data: ProjectHistoryResponse = await response.json()
        setBuilds(data.builds)
      } catch (err) {
        setError(err instanceof Error ? err.message : 'Failed to load builds')
      } finally {
        setLoading(false)
      }
    }

    fetchBuilds()
  }, [id])

  const getStatusColor = (status: string) => {
    switch (status.toLowerCase()) {
      case 'completed':
        return '#22c55e' // green
      case 'failed':
        return '#ef4444' // red
      case 'running':
        return '#3b82f6' // blue
      case 'queued':
        return '#f59e0b' // yellow
      case 'cancelled':
        return '#6b7280' // gray
      default:
        return '#6b7280' // gray
    }
  }

  const formatDuration = (durationMs?: number) => {
    if (!durationMs) return 'N/A'

    if (durationMs < 1000) {
      return `${durationMs}ms`
    }

    const seconds = Math.floor(durationMs / 1000)
    const minutes = Math.floor(seconds / 60)
    const remainingSeconds = seconds % 60

    if (minutes > 0) {
      return `${minutes}m ${remainingSeconds}s`
    }
    return `${seconds}s`
  }

  const formatTimestamp = (timestamp: string) => {
    const date = new Date(timestamp)
    const now = new Date()
    const diffMs = now.getTime() - date.getTime()
    const diffMinutes = Math.floor(diffMs / (1000 * 60))
    const diffHours = Math.floor(diffMinutes / 60)
    const diffDays = Math.floor(diffHours / 24)

    if (diffDays > 0) {
      return `${diffDays} day${diffDays > 1 ? 's' : ''} ago`
    } else if (diffHours > 0) {
      return `${diffHours} hour${diffHours > 1 ? 's' : ''} ago`
    } else if (diffMinutes > 0) {
      return `${diffMinutes} minute${diffMinutes > 1 ? 's' : ''} ago`
    } else {
      return 'Just now'
    }
  }

  if (loading) {
    return (
      <div>
        <h1>Project History</h1>
        <p>Loading builds...</p>
      </div>
    )
  }

  if (error) {
    return (
      <div>
        <h1>Project History</h1>
        <p style={{ color: '#ef4444' }}>Error: {error}</p>
      </div>
    )
  }

  return (
    <div>
      <h1>Project History</h1>
      <div style={{ marginBottom: '1rem' }}>
        <Link to="/" style={{ color: '#3b82f6', textDecoration: 'none' }}>
          ← Back to Dashboard
        </Link>
      </div>

      <div style={{ marginTop: '2rem' }}>
        <h2>Recent Builds ({builds.length})</h2>

        {builds.length === 0 ? (
          <div style={{
            padding: '2rem',
            border: '1px solid #e5e7eb',
            borderRadius: '8px',
            backgroundColor: '#f9fafb',
            textAlign: 'center',
            marginTop: '1rem'
          }}>
            <p>No builds found for this project.</p>
          </div>
        ) : (
          <div style={{ marginTop: '1rem' }}>
            {builds.map((build) => (
              <div
                key={build.id}
                style={{
                  border: '1px solid #e5e7eb',
                  borderRadius: '8px',
                  padding: '1rem',
                  marginBottom: '1rem',
                  backgroundColor: '#ffffff',
                }}
              >
                <div style={{
                  display: 'flex',
                  justifyContent: 'space-between',
                  alignItems: 'flex-start',
                  marginBottom: '0.5rem'
                }}>
                  <div style={{ flex: 1 }}>
                    <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem' }}>
                      <div
                        style={{
                          width: '12px',
                          height: '12px',
                          borderRadius: '50%',
                          backgroundColor: getStatusColor(build.status),
                          flexShrink: 0,
                        }}
                      />
                      <h3 style={{ margin: 0, fontSize: '1rem', fontWeight: '600' }}>
                        {build.command}
                      </h3>
                    </div>
                    <p style={{ margin: '0.5rem 0 0 1.5rem', fontSize: '0.875rem', color: '#6b7280' }}>
                      {formatTimestamp(build.start_time)}
                    </p>
                  </div>

                  <div style={{ textAlign: 'right', fontSize: '0.875rem' }}>
                    <div style={{ fontWeight: '600', color: '#374151' }}>
                      {build.status}
                    </div>
                    {build.exit_code !== undefined && build.exit_code !== null && (
                      <div style={{ color: '#6b7280', marginTop: '0.25rem' }}>
                        Exit code: {build.exit_code}
                      </div>
                    )}
                  </div>
                </div>

                <div style={{
                  display: 'flex',
                  gap: '2rem',
                  fontSize: '0.75rem',
                  color: '#6b7280',
                  marginTop: '0.75rem',
                  paddingTop: '0.75rem',
                  borderTop: '1px solid #e5e7eb',
                }}>
                  <div>
                    <strong>Duration:</strong> {formatDuration(build.duration_ms)}
                  </div>
                  <div>
                    <strong>Mode:</strong> {build.execution_mode}
                  </div>
                  <div>
                    <strong>Environment:</strong> {build.environment}
                  </div>
                  {build.git_context?.branch && (
                    <div>
                      <strong>Branch:</strong> {build.git_context.branch}
                    </div>
                  )}
                </div>

                {build.logs_stored && (
                  <div style={{ marginTop: '0.75rem' }}>
                    <Link
                      to={`/builds/${build.id}`}
                      style={{
                        color: '#3b82f6',
                        textDecoration: 'none',
                        fontSize: '0.875rem',
                      }}
                    >
                      View Logs →
                    </Link>
                  </div>
                )}
              </div>
            ))}
          </div>
        )}
      </div>
    </div>
  )
}

export default ProjectHistory