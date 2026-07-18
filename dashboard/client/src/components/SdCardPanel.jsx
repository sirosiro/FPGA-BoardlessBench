import React, { useState, useEffect } from 'react';
import { Database, FileText, CheckCircle, XCircle } from 'lucide-react';

function SdCardPanel() {
  const [status, setStatus] = useState({
    mounted: false,
    mountPoint: '/mnt/sd',
    usedSize: 0,
    totalSize: 512 * 1024 * 1024,
    sdDir: ''
  });
  const [files, setFiles] = useState([]);
  const [selectedFile, setSelectedFile] = useState(null);
  const [viewFormat, setViewFormat] = useState('text');
  const [fileContent, setFileContent] = useState('');
  const [loadingContent, setLoadingContent] = useState(false);

  // Poll status and file list
  useEffect(() => {
    const fetchData = async () => {
      try {
        const resStatus = await fetch('/api/sdcard/status');
        const dataStatus = await resStatus.json();
        setStatus(dataStatus);

        const resList = await fetch('/api/sdcard/list');
        const dataList = await resList.json();
        setFiles(dataList);
      } catch (e) {
        console.error('Failed to fetch SD status/list:', e);
      }
    };

    fetchData();
    const interval = setInterval(fetchData, 2000);
    return () => clearInterval(interval);
  }, []);

  // Fetch file content when selection or format changes
  useEffect(() => {
    if (!selectedFile) return;

    const fetchContent = async () => {
      setLoadingContent(true);
      try {
        const res = await fetch(`/api/sdcard/dump?file=${encodeURIComponent(selectedFile)}&format=${viewFormat}`);
        const content = await res.text();
        setFileContent(content);
      } catch (e) {
        setFileContent(`Error loading file: ${e.message}`);
      } finally {
        setLoadingContent(false);
      }
    };

    fetchContent();
  }, [selectedFile, viewFormat]);

  const formatSize = (bytes) => {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
  };

  const usedPercentage = Math.min(100, (status.usedSize / status.totalSize) * 100).toFixed(2);

  return (
    <div className="sd-card-panel" style={{ height: '100%', display: 'flex', flexDirection: 'column', overflow: 'hidden', background: '#0d1117', color: '#c9d1d9' }}>
      <div className="panel-header" style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', padding: '8px 12px', background: '#161b22', borderBottom: '1px solid #30363d', fontSize: '0.8rem', fontWeight: 600, color: '#8b949e' }}>
        <Database size={16} /> Virtual SD Card Explorer (MMC0)
      </div>

      {/* Meta/Status Header */}
      <div className="sd-metadata" style={{ padding: '0.75rem 1rem', borderBottom: '1px solid #30363d', display: 'flex', flexWrap: 'wrap', gap: '1.5rem', background: '#0d1117', fontSize: '0.75rem' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem' }}>
          <span>Status:</span>
          {status.mounted ? (
            <span style={{ color: '#3fb950', display: 'flex', alignItems: 'center', gap: '0.25rem', fontWeight: 600 }}>
              <CheckCircle size={14} /> Mounted
            </span>
          ) : (
            <span style={{ color: '#f85149', display: 'flex', alignItems: 'center', gap: '0.25rem', fontWeight: 600 }}>
              <XCircle size={14} /> Unmounted
            </span>
          )}
        </div>
        <div>
          <span>Mount Point:</span> <strong style={{ color: '#58a6ff' }}>{status.mountPoint}</strong>
        </div>
        <div style={{ flex: 1, minWidth: '150px' }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '0.25rem' }}>
            <span>Used Space:</span>
            <span>{formatSize(status.usedSize)} / {formatSize(status.totalSize)} ({usedPercentage}%)</span>
          </div>
          <div style={{ width: '100%', height: '6px', background: '#21262d', borderRadius: '3px', overflow: 'hidden' }}>
            <div style={{ width: `${usedPercentage}%`, height: '100%', background: '#58a6ff', borderRadius: '3px' }}></div>
          </div>
        </div>
      </div>

      {/* Main Split View */}
      <div style={{ flex: 1, display: 'flex', overflow: 'hidden' }}>
        {/* Left Side: File Explorer */}
        <div style={{ width: '35%', borderRight: '1px solid #30363d', display: 'flex', flexDirection: 'column', overflow: 'hidden' }}>
          <div style={{ padding: '6px 12px', background: '#161b22', fontSize: '0.7rem', fontWeight: 600, color: '#8b949e', borderBottom: '1px solid #30363d' }}>
            FILES IN STORAGE
          </div>
          <div style={{ flex: 1, overflowY: 'auto' }}>
            {files.length === 0 ? (
              <div style={{ padding: '1rem', textAlign: 'center', color: '#8b949e', fontSize: '0.75rem' }}>
                No files found or SD Card is empty.
              </div>
            ) : (
              <table style={{ width: '100%', borderCollapse: 'collapse', fontSize: '0.75rem' }}>
                <thead>
                  <tr style={{ background: '#161b22', borderBottom: '1px solid #30363d', textAlign: 'left', color: '#8b949e' }}>
                    <th style={{ padding: '6px 8px', fontWeight: 500 }}>Name</th>
                    <th style={{ padding: '6px 8px', fontWeight: 500, textAlign: 'right' }}>Size</th>
                  </tr>
                </thead>
                <tbody>
                  {files.map((file, idx) => (
                    <tr 
                      key={idx} 
                      onClick={() => setSelectedFile(file.name)}
                      style={{ 
                        borderBottom: '1px solid #21262d', 
                        cursor: 'pointer', 
                        background: selectedFile === file.name ? '#1f242c' : 'transparent',
                        color: selectedFile === file.name ? '#58a6ff' : '#c9d1d9'
                      }}
                      className="file-row"
                    >
                      <td style={{ padding: '8px', display: 'flex', alignItems: 'center', gap: '0.5rem', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                        <FileText size={14} style={{ color: '#8b949e' }} />
                        {file.name}
                      </td>
                      <td style={{ padding: '8px', textAlign: 'right', color: '#8b949e' }}>
                        {formatSize(file.size)}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            )}
          </div>
        </div>

        {/* Right Side: File Content Dumper */}
        <div style={{ flex: 1, display: 'flex', flexDirection: 'column', overflow: 'hidden' }}>
          <div style={{ padding: '4px 12px', background: '#161b22', borderBottom: '1px solid #30363d', display: 'flex', justifyContent: 'space-between', alignItems: 'center', height: '29px' }}>
            <span style={{ fontSize: '0.7rem', fontWeight: 600, color: '#8b949e' }}>
              {selectedFile ? `PREVIEW: ${selectedFile}` : 'FILE DUMP PREVIEW'}
            </span>
            {selectedFile && (
              <div style={{ display: 'flex', gap: '2px', background: '#0d1117', padding: '2px', borderRadius: '4px', border: '1px solid #30363d' }}>
                <button 
                  onClick={() => setViewFormat('text')}
                  style={{
                    background: viewFormat === 'text' ? '#21262d' : 'transparent',
                    border: 'none',
                    color: viewFormat === 'text' ? '#ffffff' : '#8b949e',
                    fontSize: '0.65rem',
                    padding: '2px 8px',
                    borderRadius: '3px',
                    cursor: 'pointer'
                  }}
                >
                  Text
                </button>
                <button 
                  onClick={() => setViewFormat('hex')}
                  style={{
                    background: viewFormat === 'hex' ? '#21262d' : 'transparent',
                    border: 'none',
                    color: viewFormat === 'hex' ? '#ffffff' : '#8b949e',
                    fontSize: '0.65rem',
                    padding: '2px 8px',
                    borderRadius: '3px',
                    cursor: 'pointer'
                  }}
                >
                  HEX
                </button>
              </div>
            )}
          </div>
          <div style={{ flex: 1, overflow: 'auto', background: '#0d1117', padding: '1rem', margin: 0 }}>
            {selectedFile ? (
              loadingContent ? (
                <div style={{ color: '#8b949e', fontSize: '0.75rem', textAlign: 'center', marginTop: '2rem' }}>
                  Loading file content...
                </div>
              ) : (
                <pre style={{ 
                  margin: 0, 
                  fontFamily: 'SFMono-Regular, Consolas, "Liberation Mono", Menlo, monospace', 
                  fontSize: '0.75rem', 
                  lineHeight: '1.4', 
                  color: '#c9d1d9',
                  whiteSpace: 'pre'
                }}>
                  {fileContent}
                </pre>
              )
            ) : (
              <div style={{ display: 'flex', height: '100%', alignItems: 'center', justifyContent: 'center', color: '#8b949e', fontSize: '0.75rem' }}>
                Select a file from the explorer list to dump its contents.
              </div>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}

export default SdCardPanel;
