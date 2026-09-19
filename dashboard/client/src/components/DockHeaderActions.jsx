/**
 * @file DockHeaderActions.jsx
 * @brief Header actions renderer for Dockview tab groups
 * 
 * Renders an action button on the right side of Dockview tab headers (VS Code style).
 * Allows users to pop out the currently active tab into an independent browser window
 * for multi-monitor viewing.
 */

import React from 'react';
import { ExternalLink } from 'lucide-react';

export default function DockHeaderActions(props) {
  const handlePopout = () => {
    // In dockview-react, activePanel is provided directly in props,
    // or via props.group.activePanel / props.containerApi.activePanel
    const activePanel = props.activePanel || props.group?.activePanel || props.containerApi?.activePanel;
    if (!activePanel) {
      console.warn('[DockHeaderActions] No active panel found for popout:', props);
      return;
    }

    const panelId = activePanel.id;
    const title = activePanel.title || panelId;
    const params = activePanel.params || {};
    const component = activePanel.view?.contentComponent || params._componentName || panelId.split('_')[0];

    // Safari compatibility: window.open MUST be called synchronously within the direct click event
    const win = window.open(
      '',
      `fbb_popout_${panelId}_${Date.now()}`,
      'width=960,height=680,menubar=no,toolbar=no,location=no,status=no,resizable=yes'
    );

    if (!win) {
      alert('Pop-up window was blocked by the browser. Please check Safari "Pop-up Windows" permissions for http://localhost:8080.');
      return;
    }

    // Dispatch global popout request with the opened window reference
    window.dispatchEvent(new CustomEvent('fbb:popout', {
      detail: {
        id: panelId,
        title,
        params,
        component,
        win
      }
    }));
  };

  return (
    <div
      style={{
        display: 'flex',
        alignItems: 'center',
        paddingRight: '6px',
        height: '100%'
      }}
    >
      <button
        onClick={handlePopout}
        title="Pop out to separate window (別ウィンドウで開く)"
        style={{
          background: 'transparent',
          border: 'none',
          color: '#8b949e',
          cursor: 'pointer',
          padding: '3px 5px',
          borderRadius: '4px',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          transition: 'color 0.15s, background-color 0.15s'
        }}
        onMouseEnter={(e) => {
          e.currentTarget.style.color = '#58a6ff';
          e.currentTarget.style.backgroundColor = '#21262d';
        }}
        onMouseLeave={(e) => {
          e.currentTarget.style.color = '#8b949e';
          e.currentTarget.style.backgroundColor = 'transparent';
        }}
      >
        <ExternalLink size={13} />
      </button>
    </div>
  );
}
