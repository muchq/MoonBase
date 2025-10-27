import { Routes, Route } from 'react-router-dom'
import Layout from './components/Layout'
import Dashboard from './pages/Dashboard'
import BuildViewer from './pages/BuildViewer'
import ProjectHistory from './pages/ProjectHistory'

function App() {
  return (
    <Layout>
      <Routes>
        <Route path="/" element={<Dashboard />} />
        <Route path="/builds/:id" element={<BuildViewer />} />
        <Route path="/projects/:id" element={<ProjectHistory />} />
      </Routes>
    </Layout>
  )
}

export default App