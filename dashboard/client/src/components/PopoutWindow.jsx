/**
 * @file PopoutWindow.jsx
 * @brief Multi-Screen Pop-out Child Window via React Portal
 * 
 * Creates an independent OS browser window using window.open() and projects
 * React components into it using createPortal(). Inherits all parent stylesheets,
 * dark theme styling, and provides an automatic re-docking handler when closed.
 */

import { useEffect, useState, useRef } from 'react';
import { createPortal } from 'react-dom';

export default function PopoutWindow({ title, onClose, win: winProp, children }) {
  const [container, setContainer] = useState(null);
  const winRef = useRef(null);
  const onCloseRef = useRef(onClose);

  useEffect(() => {
    onCloseRef.current = onClose;
  }, [onClose]);

  useEffect(() => {
    // Use window opened synchronously in click handler (Safari compliant), or fallback to opening here
    let win = winProp;
    if (!win || win.closed) {
      win = window.open(
        '',
        `fbb_popout_${Date.now()}`,
        'width=960,height=680,menubar=no,toolbar=no,location=no,status=no,resizable=yes'
      );
    }

    if (!win) {
      alert('Pop-up window was blocked by the browser. Please check Safari "Pop-up Windows" permissions for http://localhost:8080.');
      onClose();
      return;
    }

    winRef.current = win;
    win.document.title = `${title} - F-BB Multi-Screen Cockpit`;

    // 1. Clone DOM stylesheet links and style tags directly (clean & fast across Safari/WebKit)
    try {
      Array.from(document.querySelectorAll('link[rel="stylesheet"], style')).forEach(node => {
        win.document.head.appendChild(node.cloneNode(true));
      });
    } catch (e) {
      console.warn('[PopoutWindow] Notice cloning styles:', e);
    }

    // 2. Clone inline stylesheet cssRules as fallback
    Array.from(document.styleSheets).forEach(styleSheet => {
      try {
        if (styleSheet.cssRules && styleSheet.cssRules.length > 0) {
          const newStyle = win.document.createElement('style');
          Array.from(styleSheet.cssRules).forEach(rule => {
            newStyle.appendChild(win.document.createTextNode(rule.cssText));
          });
          win.document.head.appendChild(newStyle);
        }
      } catch {
        // Cross-origin access restriction - safe to ignore
      }
    });

    // Dark theme styling on popout window body
    win.document.body.style.backgroundColor = '#0d1117';
    win.document.body.style.color = '#c9d1d9';
    win.document.body.style.margin = '0';
    win.document.body.style.padding = '0';
    win.document.body.style.overflow = 'hidden';
    win.document.body.style.height = '100vh';
    win.document.body.style.width = '100vw';
    win.document.body.style.fontFamily = '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif';

    // Create wrapper root with dockview dark theme class
    const rootDiv = win.document.createElement('div');
    rootDiv.id = 'fbb-popout-root';
    rootDiv.className = 'dockview-theme-dark';
    rootDiv.style.display = 'flex';
    rootDiv.style.flexDirection = 'column';
    rootDiv.style.height = '100%';
    rootDiv.style.width = '100%';
    win.document.body.appendChild(rootDiv);

    // Mount container inside wrapper
    const contentDiv = win.document.createElement('div');
    contentDiv.id = 'fbb-popout-content';
    contentDiv.className = 'dockview-theme-dark';
    contentDiv.style.flex = '1';
    contentDiv.style.overflow = 'hidden';
    contentDiv.style.display = 'flex';
    contentDiv.style.flexDirection = 'column';
    contentDiv.style.position = 'relative';

    // Top control bar inside popout window
    const topBar = win.document.createElement('div');
    topBar.style.height = '36px';
    topBar.style.background = '#161b22';
    topBar.style.borderBottom = '1px solid #30363d';
    topBar.style.display = 'flex';
    topBar.style.alignItems = 'center';
    topBar.style.justifyContent = 'space-between';
    topBar.style.padding = '0 12px';
    topBar.style.fontSize = '0.8rem';
    topBar.style.userSelect = 'none';

    const titleSpan = win.document.createElement('span');
    titleSpan.textContent = `F-BB Multi-Screen: ${title}`;
    titleSpan.style.fontWeight = '600';
    titleSpan.style.color = '#58a6ff';
    titleSpan.style.display = 'flex';
    titleSpan.style.alignItems = 'center';
    titleSpan.style.gap = '8px';

    const redockBtn = win.document.createElement('button');
    redockBtn.innerHTML = '&#x2199; Re-dock to Main Window';
    redockBtn.style.background = '#21262d';
    redockBtn.style.color = '#c9d1d9';
    redockBtn.style.border = '1px solid #30363d';
    redockBtn.style.borderRadius = '4px';
    redockBtn.style.padding = '4px 10px';
    redockBtn.style.fontSize = '0.75rem';
    redockBtn.style.cursor = 'pointer';
    redockBtn.style.fontWeight = '600';
    redockBtn.style.transition = 'all 0.15s';
    redockBtn.onmouseenter = () => {
      redockBtn.style.background = '#30363d';
      redockBtn.style.color = '#58a6ff';
    };
    redockBtn.onmouseleave = () => {
      redockBtn.style.background = '#21262d';
      redockBtn.style.color = '#c9d1d9';
    };
    redockBtn.onclick = () => {
      win.close();
    };

    topBar.appendChild(titleSpan);
    topBar.appendChild(redockBtn);

    rootDiv.appendChild(topBar);
    rootDiv.appendChild(contentDiv);

    queueMicrotask(() => {
      setContainer(contentDiv);
    });

    // Auto re-dock when child window is closed
    let isClosed = false;
    const handleClose = () => {
      if (isClosed) return;
      isClosed = true;
      onCloseRef.current?.();
    };

    win.addEventListener('beforeunload', handleClose);
    win.addEventListener('pagehide', handleClose);

    // Heartbeat check for window close (detects Safari traffic light or Cmd+W reliably)
    const checkClosedInterval = setInterval(() => {
      if (win.closed) {
        clearInterval(checkClosedInterval);
        handleClose();
      }
    }, 500);

    return () => {
      clearInterval(checkClosedInterval);
      win.removeEventListener('beforeunload', handleClose);
      win.removeEventListener('pagehide', handleClose);
      if (!win.closed) {
        win.close();
      }
    };
  }, [winProp, title, onClose]);

  if (!container) return null;

  return createPortal(children, container);
}
