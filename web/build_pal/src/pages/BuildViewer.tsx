import React, { useState, useEffect, useRef } from 'react'
import { useParams } from 'react-router-dom'
import type { Build, BuildSummary, LogLine, BuildStatus } from '../types/api'

const API_BASE_URL = 'http://localhost:8080/api'

const BuildViewer: React.FC = () => {
  const { id } = useParams<{ id: string }>()
  const [build, setBuild] = useState<Build | null>(null)
  const [buildSummary, setBuildSummary] = useState<BuildSummary | null>(null)
  const [logs, setLogs] = useState<LogLine[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState<string | null>(null)
  const [isPolling, setIsPolling] = useState(false)
  const logsEndRef = useRef<HTMLDivElement>(null)
  const pollIntervalRef = useRef<NodeJS.Timeout | null>(null)

  const scrollToBottom = () => {
    logsEndRef.current?.scrollIntoView({ behavior: 'smooth' })
  }

  const fetchBuildStatus = async () => {
    if (!id) return

    try {
      const response = await fetch(`${API_BASE_URL}/builds/${id}`)
      if (!response.ok) {
        throw new Error(`Failed to fetch build: ${response.statusText}`)
      }
      const data = await response.json()
      setBuild(data.build)
      
      // If build is complete, fetch summary
      if (data.build.status === 'completed' || data.build.status === 'failed') {
        fetchBuildSummary()
      }
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to fetch build')
    }
  }

  const fetchBuildSummary = async () => {
    if (!id) return

    try {
      const response = await fetch(`${API_BASE_URL}/builds/${id}/summary`)
      if (response.ok) {
        const summary = await response.json()
        setBuildSummary(summary)
      }
    } catch (err) {
      console.warn('Failed to fetch build summary:', err)
    }
  }

  const fetchLogs = async () => {
    if (!id) return

    try {
      const response = await fetch(`${API_BASE_URL}/builds/${id}/logs`)
      if (!response.ok) {
        throw new Error(`Failed to fetch logs: ${response.statusText}`)
      }
      const data = await response.json()
      setLogs(data.logs || [])
      
      // Auto-scroll to bottom when new logs arrive
      setTimeout(scrollToBottom, 100)
    } catch (err) {
      console.warn('Failed to fetch logs:', err)
    }
  }

  const startPolling = () => {
    if (pollIntervalRef.current) return
    
    setIsPolling(true)
    pollIntervalRef.current = setInterval(() => {
      fetchBuildStatus()
      fetchLogs()
    }, 2000) // Poll every 2 seconds
  }

  const stopPolling = () => {
    if (pollIntervalRef.current) {
      clearInterval(pollIntervalRef.current)
      pollIntervalRef.current = null
    }
    setIsPolling(false)
  }

  const cancelBuild = async () => {
    if (!id || !build) return

    try {
      const response = await fetch(`${API_BASE_URL}/builds/${id}`, {
        method: 'DELETE'
      })
      if (!response.ok) {
        throw new Error(`Failed to cancel build: ${response.statusText}`)
      }
      // Refresh build status
      fetchBuildStatus()
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to cancel build')
    }
  }

  useEffect(() => {
    const initializeData = async () => {
      setLoading(true)
      await fetchBuildStatus()
      await fetchLogs()
      setLoading(false)
    }

    initializeData()

    return () => {
      stopPolling()
    }
  }, [id])

  useEffect(() => {
    if (build && build.status) {
      // Start polling if build is running
      if (build.status === 'running' || build.status === 'queued') {
        startPolling()
      } else {
        stopPolling()
      }
    }
  }, [build?.status])

  const getStatusColor = (status: BuildStatus): string => {
    switch (status) {
      case 'completed': return '#22c55e'
      case 'failed': return '#ef4444'
      case 'running': return '#3b82f6'
      case 'queued': return '#f59e0b'
      case 'cancelled': return '#6b7280'
      default: return '#6b7280'
    }
  }

  const formatDuration = (durationMs?: number): string => {
    if (!durationMs) return 'N/A'
    const seconds = Math.floor(durationMs / 1000)
    const minutes = Math.floor(seconds / 60)
    const remainingSeconds = seconds % 60
    return minutes > 0 ? `${minutes}m ${remainingSeconds}s` : `${remainingSeconds}s`
  }

  const formatTimestamp = (timestamp: string): string => {
    return new Date(timestamp).toLocaleString()
  }

  if (loading) {
    return (
      <div style={{ padding: '2rem', textAlign: 'center' }}>
        <div>Loading build information...</div>
      </div>
    )
  }

  if (error) {
    return (
      <div style={{ padding: '2rem', color: '#ef4444' }}>
        <h2>Error</h2>
        <p>{error}</p>
      </div>
    )
  }

  if (!build) {
    return (
      <div style={{ padding: '2rem' }}>
        <h2>Build Not Found</h2>
        <p>Build with ID {id} was not found.</p>
      </div>
    )
  }

  return (
    <div style={{ padding: '2rem', maxWidth: '1200px', margin: '0 auto' }}>
      {/* Build Header */}
      <div style={{ 
        marginBottom: '2rem', 
        padding: '1.5rem', 
        backgroundColor: '#f8f9fa', 
        borderRadius: '8px',
        border: '1px solid #e9ecef'
      }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start', marginBottom: '1rem' }}>
          <div>
            <h1 style={{ margin: '0 0 0.5rem 0', fontSize: '1.5rem' }}>
              Build: {build.command}
            </h1>
            <div style={{ display: 'flex', alignItems: 'center', gap: '1rem', flexWrap: 'wrap' }}>
              <span style={{ 
                padding: '0.25rem 0.75rem', 
                borderRadius: '12px', 
                backgroundColor: getStatusColor(build.status),
                color: 'white',
                fontSize: '0.875rem',
                fontWeight: '500'
              }}>
                {build.status.toUpperCase()}
                {isPolling && build.status === 'running' && (
                  <span style={{ marginLeft: '0.5rem' }}>●</span>
                )}
              </span>
              <span style={{ color: '#6b7280', fontSize: '0.875rem' }}>
                ID: {build.id}
              </span>
            </div>
          </div>
          {(build.status === 'running' || build.status === 'queued') && (
            <button
              onClick={cancelBuild}
              style={{
                padding: '0.5rem 1rem',
                backgroundColor: '#ef4444',
                color: 'white',
                border: 'none',
                borderRadius: '4px',
                cursor: 'pointer',
                fontSize: '0.875rem'
              }}
            >
              Cancel Build
            </button>
          )}
        </div>

        {/* Build Details */}
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(200px, 1fr))', gap: '1rem', fontSize: '0.875rem' }}>
          <div>
            <strong>Working Directory:</strong><br />
            <span style={{ color: '#6b7280', fontFamily: 'monospace' }}>{build.working_directory}</span>
          </div>
          <div>
            <strong>Execution Mode:</strong><br />
            <span style={{ color: '#6b7280' }}>{build.execution_mode}</span>
          </div>
          <div>
            <strong>Environment:</strong><br />
            <span style={{ color: '#6b7280' }}>{build.environment}</span>
          </div>
          <div>
            <strong>Started:</strong><br />
            <span style={{ color: '#6b7280' }}>{formatTimestamp(build.start_time)}</span>
          </div>
          {build.end_time && (
            <div>
              <strong>Completed:</strong><br />
              <span style={{ color: '#6b7280' }}>{formatTimestamp(build.end_time)}</span>
            </div>
          )}
          <div>
            <strong>Duration:</strong><br />
            <span style={{ color: '#6b7280' }}>{formatDuration(build.duration_ms)}</span>
          </div>
          {build.exit_code !== undefined && (
            <div>
              <strong>Exit Code:</strong><br />
              <span style={{ color: build.exit_code === 0 ? '#22c55e' : '#ef4444' }}>{build.exit_code}</span>
            </div>
          )}
        </div>

        {/* Git Context */}
        {build.git_context && (
          <div style={{ marginTop: '1rem', padding: '1rem', backgroundColor: 'white', borderRadius: '4px', border: '1px solid #e9ecef' }}>
            <h3 style={{ margin: '0 0 0.5rem 0', fontSize: '1rem' }}>Git Context</h3>
            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(200px, 1fr))', gap: '0.5rem', fontSize: '0.875rem' }}>
              {build.git_context.branch && (
                <div>
                  <strong>Branch:</strong> <span style={{ fontFamily: 'monospace' }}>{build.git_context.branch}</span>
                </div>
              )}
              {build.git_context.commit_hash && (
                <div>
                  <strong>Commit:</strong> <span style={{ fontFamily: 'monospace' }}>{build.git_context.commit_hash.substring(0, 8)}</span>
                </div>
              )}
              {build.git_context.author && (
                <div>
                  <strong>Author:</strong> {build.git_context.author}
                </div>
              )}
              <div>
                <strong>Uncommitted Changes:</strong> {build.git_context.has_uncommitted_changes ? 'Yes' : 'No'}
              </div>
            </div>
            {build.git_context.commit_message && (
              <div style={{ marginTop: '0.5rem' }}>
                <strong>Commit Message:</strong><br />
                <span style={{ color: '#6b7280', fontStyle: 'italic' }}>{build.git_context.commit_message}</span>
              </div>
            )}
          </div>
        )}
      </div>

      {/* Build Summary */}
      {buildSummary && (
        <div style={{ 
          marginBottom: '2rem', 
          padding: '1.5rem', 
          backgroundColor: buildSummary.error_count > 0 ? '#fef2f2' : '#f0fdf4', 
          borderRadius: '8px',
          border: `1px solid ${buildSummary.error_count > 0 ? '#fecaca' : '#bbf7d0'}`
        }}>
          <h2 style={{ margin: '0 0 1rem 0', fontSize: '1.25rem' }}>Build Summary</h2>
          <div style={{ display: 'flex', gap: '2rem', flexWrap: 'wrap', marginBottom: '1rem' }}>
            <div>
              <strong>Errors:</strong> <span style={{ color: buildSummary.error_count > 0 ? '#ef4444' : '#22c55e' }}>{buildSummary.error_count}</span>
            </div>
            <div>
              <strong>Warnings:</strong> <span style={{ color: buildSummary.warning_count > 0 ? '#f59e0b' : '#6b7280' }}>{buildSummary.warning_count}</span>
            </div>
            {buildSummary.test_results && (
              <>
                <div>
                  <strong>Tests:</strong> {buildSummary.test_results.total}
                </div>
                <div>
                  <strong>Passed:</strong> <span style={{ color: '#22c55e' }}>{buildSummary.test_results.passed}</span>
                </div>
                <div>
                  <strong>Failed:</strong> <span style={{ color: '#ef4444' }}>{buildSummary.test_results.failed}</span>
                </div>
                <div>
                  <strong>Skipped:</strong> <span style={{ color: '#6b7280' }}>{buildSummary.test_results.skipped}</span>
                </div>
              </>
            )}
          </div>

          {/* Display parsed errors */}
          {buildSummary.parsed_errors.length > 0 && (
            <div>
              <h3 style={{ margin: '0 0 0.5rem 0', fontSize: '1rem' }}>Errors & Warnings</h3>
              <div style={{ maxHeight: '200px', overflowY: 'auto' }}>
                {buildSummary.parsed_errors.map((error, index) => (
                  <div key={index} style={{ 
                    padding: '0.5rem', 
                    marginBottom: '0.5rem', 
                    backgroundColor: 'white', 
                    borderRadius: '4px',
                    borderLeft: `4px solid ${error.severity === 'error' ? '#ef4444' : '#f59e0b'}`
                  }}>
                    <div style={{ fontSize: '0.875rem' }}>
                      <span style={{ 
                        color: error.severity === 'error' ? '#ef4444' : '#f59e0b',
                        fontWeight: '500',
                        textTransform: 'uppercase'
                      }}>
                        {error.severity}
                      </span>
                      {error.file && (
                        <span style={{ color: '#6b7280', marginLeft: '0.5rem' }}>
                          {error.file}
                          {error.line && `:${error.line}`}
                          {error.column && `:${error.column}`}
                        </span>
                      )}
                    </div>
                    <div style={{ marginTop: '0.25rem', color: '#374151' }}>{error.message}</div>
                  </div>
                ))}
              </div>
            </div>
          )}
        </div>
      )}

      {/* Build Logs */}
      <div style={{ marginBottom: '2rem' }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '1rem' }}>
          <h2 style={{ margin: 0, fontSize: '1.25rem' }}>Build Logs</h2>
          <div style={{ display: 'flex', gap: '0.5rem', alignItems: 'center' }}>
            {isPolling && (
              <span style={{ color: '#3b82f6', fontSize: '0.875rem' }}>
                ● Live
              </span>
            )}
            <span style={{ color: '#6b7280', fontSize: '0.875rem' }}>
              {logs.length} lines
            </span>
          </div>
        </div>
        
        <div style={{ 
          backgroundColor: '#1e1e1e', 
          color: '#fff', 
          padding: '1rem', 
          borderRadius: '8px',
          fontFamily: 'monospace',
          fontSize: '0.875rem',
          lineHeight: '1.4',
          maxHeight: '600px',
          overflowY: 'auto',
          border: '1px solid #374151'
        }}>
          {logs.length === 0 ? (
            <div style={{ color: '#6b7280', textAlign: 'center', padding: '2rem' }}>
              {build.status === 'queued' ? 'Build is queued...' : 
               build.status === 'running' ? 'Waiting for logs...' : 
               'No logs available'}
            </div>
          ) : (
            logs.map((log, index) => (
              <div key={index} style={{ marginBottom: '0.125rem' }}>
                <span style={{ color: '#6b7280', marginRight: '1rem', userSelect: 'none' }}>
                  {log.line_number.toString().padStart(4, ' ')}
                </span>
                {log.timestamp && (
                  <span style={{ color: '#9ca3af', marginRight: '1rem' }}>
                    {new Date(log.timestamp).toLocaleTimeString()}
                  </span>
                )}
                <span>{log.content}</span>
              </div>
            ))
          )}
          <div ref={logsEndRef} />
        </div>
      </div>
    </div>
  )
}

export default BuildViewer