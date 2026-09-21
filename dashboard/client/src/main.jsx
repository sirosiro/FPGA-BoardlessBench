import React, { StrictMode, useState, useEffect, useRef, useMemo, useCallback } from 'react'
import { createRoot } from 'react-dom/client'
import * as ReactDOM from 'react-dom'
import * as LucideIcons from 'lucide-react'
import { useDashboard } from './components/DashboardContext'
import './index.css'
import App from './App.jsx'

// DPPA: Expose unified global runtime bridge for zero-rebuild external panes (API v1)
window.React = React;
window.ReactDOM = ReactDOM;
window.FBB = {
  version: '1.0.0',
  apiVersion: 1,
  React,
  ReactDOM,
  hooks: { useState, useEffect, useRef, useMemo, useCallback },
  icons: LucideIcons,
  useDashboard
};

createRoot(document.getElementById('root')).render(
  <StrictMode>
    <App />
  </StrictMode>,
)
