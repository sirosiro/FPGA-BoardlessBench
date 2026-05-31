import { Cpu } from 'lucide-react';
import { useDashboard } from './DashboardContext';

function RegisterMonitor() {
  const { registers } = useDashboard();

  return (
    <div className="register-pane" style={{ height: '100%', display: 'flex', flexDirection: 'column', overflow: 'hidden' }}>
      <div className="panel-header"><Cpu size={16} /> Register Monitor</div>
      <div className="register-viewport" style={{ flex: 1, overflowY: 'auto', padding: '1rem' }}>
        <table className="register-table">
          <thead>
            <tr><th>Name</th><th>Offset</th><th>Value (Hex)</th></tr>
          </thead>
          <tbody>
            {registers.map((reg, i) => (
              <tr key={i}>
                <td className="reg-cell-name" style={{ fontWeight: 600 }}>{reg.name}</td>
                <td className="reg-offset">{reg.offset}</td>
                <td className="reg-val">{reg.value}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}

export default RegisterMonitor;
