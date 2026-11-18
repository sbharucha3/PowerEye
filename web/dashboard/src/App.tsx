import { createContext, useContext, useEffect, useState, type ReactNode } from 'react'
import { useTelemetry } from '@/hooks/useTelemetry'
import type { HubSnapshot } from '@/lib/telemetry'

type Page = 'home' | 'add' | 'history'
const TelemetryContext = createContext<HubSnapshot | null>(null)

function useSnapshot() {
  const snapshot = useContext(TelemetryContext)
  if (!snapshot) throw new Error('Telemetry context is missing')
  return snapshot
}
type IconName =
  | 'activity'
  | 'alert'
  | 'arrow'
  | 'battery'
  | 'bolt'
  | 'camera'
  | 'check'
  | 'chevron'
  | 'clock'
  | 'download'
  | 'edit'
  | 'fit'
  | 'history'
  | 'home'
  | 'hub'
  | 'info'
  | 'minus'
  | 'plug'
  | 'plus'
  | 'search'
  | 'signal'
  | 'upload'
  | 'x'

function Icon({ name, size = 18, className = '' }: { name: IconName; size?: number; className?: string }) {
  const paths: Record<IconName, ReactNode> = {
    activity: <><path d="M3 12h4l2-6 4 12 2-6h6" /></>,
    alert: <><path d="M12 3 2.8 19h18.4L12 3Z" /><path d="M12 9v4.5M12 17h.01" /></>,
    arrow: <><path d="M5 12h14M14 7l5 5-5 5" /></>,
    battery: <><rect x="4" y="7" width="15" height="10" rx="2" /><path d="M19 10h2v4h-2M8 10v4M11 10v4" /></>,
    bolt: <><path d="m13 2-8 12h7l-1 8 8-12h-7l1-8Z" /></>,
    camera: <><rect x="3" y="6" width="18" height="14" rx="2" /><path d="m8 6 1.5-2h5L16 6" /><circle cx="12" cy="13" r="3.5" /></>,
    check: <><path d="m5 12 4 4L19 6" /></>,
    chevron: <><path d="m9 18 6-6-6-6" /></>,
    clock: <><circle cx="12" cy="12" r="9" /><path d="M12 7v5l3 2" /></>,
    download: <><path d="M12 3v12m0 0 4-4m-4 4-4-4M5 20h14" /></>,
    edit: <><path d="m14 5 5 5M4 20l3.5-.8L19 7.7a2 2 0 0 0-2.7-2.7L4.8 16.5 4 20Z" /></>,
    fit: <><path d="M8 3H3v5M16 3h5v5M8 21H3v-5M16 21h5v-5" /></>,
    history: <><path d="M3 12a9 9 0 1 0 3-6.7L3 8" /><path d="M3 3v5h5M12 7v5l3 2" /></>,
    home: <><path d="m3 11 9-8 9 8" /><path d="M5 10v10h14V10M9 20v-6h6v6" /></>,
    hub: <><circle cx="12" cy="12" r="3" /><circle cx="5" cy="6" r="2" /><circle cx="19" cy="6" r="2" /><circle cx="12" cy="20" r="2" /><path d="m10 10-3.5-3M14 10l3.5-3M12 15v3" /></>,
    info: <><circle cx="12" cy="12" r="9" /><path d="M12 11v6M12 7h.01" /></>,
    minus: <><path d="M5 12h14" /></>,
    plug: <><path d="M8 3v5M16 3v5M6 8h12v2a6 6 0 0 1-12 0V8ZM12 16v5" /></>,
    plus: <><path d="M12 5v14M5 12h14" /></>,
    search: <><circle cx="11" cy="11" r="7" /><path d="m16 16 5 5" /></>,
    signal: <><path d="M5 15a10 10 0 0 1 14 0M8 18a6 6 0 0 1 8 0M12 21h.01M2 12a14 14 0 0 1 20 0" /></>,
    upload: <><path d="M12 16V4m0 0L8 8m4-4 4 4M4 15v5h16v-5" /></>,
    x: <><path d="m6 6 12 12M18 6 6 18" /></>,
  }
  return <svg className={`icon ${className}`.trim()} width={size} height={size} viewBox="0 0 24 24" aria-hidden="true">{paths[name]}</svg>
}

function StatusBadge({ status, children }: { status: 'ok' | 'warning' | 'critical' | 'nodata'; children: ReactNode }) {
  return <span className={`status-badge ${status}`}><Icon name={status === 'ok' ? 'check' : status === 'nodata' ? 'minus' : 'alert'} size={12} />{children}</span>
}

function Button({ children, kind = 'default', icon, onClick }: { children: ReactNode; kind?: 'default' | 'primary' | 'quiet'; icon?: IconName; onClick?: () => void }) {
  return <button className={`button ${kind}`} onClick={onClick}>{icon && <Icon name={icon} size={17} />}{children}</button>
}

function Nav({ page, setPage }: { page: Page; setPage: (page: Page) => void }) {
  return (
    <>
      <aside className="sidebar">
        <div className="brand"><span className="brand-mark"><Icon name="bolt" size={20} /></span><span>PowerEye</span></div>
        <nav className="side-nav">
          <button className={page === 'home' || page === 'add' ? 'active' : ''} onClick={() => setPage('home')}><Icon name="home" />Home</button>
          <button className={page === 'history' ? 'active' : ''} onClick={() => setPage('history')}><Icon name="history" />History</button>
        </nav>
        <div className="site-meta"><div className="eyebrow">SITE SYSTEM</div><div><span className="status-dot ok-dot" />12 of 12 linked</div><div><span className="status-dot" />Hub PE-401</div></div>
        <div className="operator"><span className="avatar">MS</span><span><b>Marcus Shaw</b><small>Generator operator</small></span></div>
      </aside>
      <nav className="bottom-nav">
        <button className={page === 'home' || page === 'add' ? 'active' : ''} onClick={() => setPage('home')}><Icon name="home" /><span>Home</span></button>
        <button className={page === 'history' ? 'active' : ''} onClick={() => setPage('history')}><Icon name="history" /><span>History</span></button>
      </nav>
    </>
  )
}

function Topbar({ page, setPage }: { page: Page; setPage: (page: Page) => void }) {
  const snapshot = useSnapshot()
  const ageSeconds = Math.max(0, Math.floor((Date.now() - snapshot.receivedAt) / 1000))
  const stale = ageSeconds > 5
  return (
    <header className="topbar">
      <div className="site-title"><span className="mobile-brand"><Icon name="bolt" size={16} /></span><div><small>ACTIVE SITE · {snapshot.hub.id}</small><strong>{snapshot.hub.name}</strong></div><Icon name="chevron" size={15} /></div>
      <div className="top-actions"><span className="connection"><span className="pulse" />{snapshot.hub.source === 'simulated' ? 'Simulated demo' : 'Hub connected'} · {stale ? 'Stale' : `${ageSeconds}s ago`}</span><Button kind="primary" icon="plus" onClick={() => setPage('add')}>{page === 'add' ? 'Adding device' : 'Add Device'}</Button></div>
    </header>
  )
}

type MeterStatus = 'ok' | 'warning' | 'critical' | 'nodata'
type MeterNode = { id: string; name: string; sub: string; value: string; type: 'source' | 'load'; status: MeterStatus; x: number; y: number }

const nodes: MeterNode[] = [
  { id: 'gen', name: 'Generator 1', sub: 'Diesel', value: '68.4 kW', type: 'source', status: 'ok', x: 28, y: 5 },
  { id: 'battery', name: 'Battery Bank', sub: '', value: '12.8 kW', type: 'source', status: 'ok', x: 68, y: 5 },
  { id: 'main', name: 'Main Distro', sub: '', value: '81.2 kW', type: 'source', status: 'ok', x: 48, y: 28 },
  { id: 'a', name: 'Distro A', sub: '', value: '49.7 kW', type: 'source', status: 'warning', x: 25, y: 51 },
  { id: 'b', name: 'Distro B', sub: '', value: '31.5 kW', type: 'source', status: 'ok', x: 72, y: 51 },
  { id: 'key', name: 'Key Light HMI', sub: '', value: '42.1 A', type: 'load', status: 'warning', x: 5, y: 79 },
  { id: 'fill', name: 'Fill Light', sub: '', value: '18.6 A', type: 'load', status: 'ok', x: 20, y: 79 },
  { id: 'camera', name: 'Camera Cart', sub: '', value: '6.8 A', type: 'load', status: 'ok', x: 35, y: 79 },
  { id: 'video', name: 'Video Village', sub: '', value: '—', type: 'load', status: 'nodata', x: 50, y: 79 },
  { id: 'sound', name: 'Sound Cart', sub: '', value: '4.2 A', type: 'load', status: 'ok', x: 65, y: 79 },
  { id: 'craft', name: 'Craft Services', sub: '', value: '28.9 A', type: 'load', status: 'critical', x: 80, y: 79 },
  { id: 'trailer', name: 'Trailer 1', sub: '', value: '16.2 A', type: 'load', status: 'ok', x: 95, y: 79 },
]

const demoNode: MeterNode = { id: 'cameraaux', name: 'Camera Cart Aux', sub: '', value: '7.4 A', type: 'load', status: 'ok', x: 88, y: 79 }

const meterDetails: Record<string, { role: string; parent: string; voltage: string; current: string; power: string; factor: string; frequency: string; signal: string; signalLabel: string; statusLabel: string; updated: string; lastSeen: string }> = {
  gen: { role: 'Power source', parent: 'Top-level', voltage: '239.8 V', current: '285.3 A', power: '68.4 kW', factor: '0.97', frequency: '60.0 Hz', signal: '−54 dBm', signalLabel: 'Excellent', statusLabel: 'Operating normally', updated: 'Updated 3 sec ago', lastSeen: 'Just now' },
  battery: { role: 'Power source', parent: 'Top-level', voltage: '240.2 V', current: '53.6 A', power: '12.8 kW', factor: '0.99', frequency: '60.0 Hz', signal: '−61 dBm', signalLabel: 'Good', statusLabel: 'Operating normally', updated: 'Updated 5 sec ago', lastSeen: 'Just now' },
  main: { role: 'Power source', parent: 'Generator 1 + Battery Bank', voltage: '239.4 V', current: '339.2 A', power: '81.2 kW', factor: '0.98', frequency: '60.0 Hz', signal: '−58 dBm', signalLabel: 'Excellent', statusLabel: 'Operating normally', updated: 'Updated 4 sec ago', lastSeen: 'Just now' },
  a: { role: 'Power source', parent: 'Main Distro', voltage: '238.6 V', current: '208.7 A', power: '49.7 kW', factor: '0.94', frequency: '59.9 Hz', signal: '−67 dBm', signalLabel: 'Good', statusLabel: 'High load', updated: 'Updated 8 sec ago', lastSeen: 'Just now' },
  b: { role: 'Power source', parent: 'Main Distro', voltage: '240.1 V', current: '131.2 A', power: '31.5 kW', factor: '0.98', frequency: '60.0 Hz', signal: '−63 dBm', signalLabel: 'Good', statusLabel: 'Operating normally', updated: 'Updated 6 sec ago', lastSeen: 'Just now' },
  key: { role: 'Load', parent: 'Distro A', voltage: '238.1 V', current: '42.1 A', power: '9.6 kW', factor: '0.96', frequency: '59.9 Hz', signal: '−72 dBm', signalLabel: 'Fair', statusLabel: 'Approaching limit', updated: 'Updated 9 sec ago', lastSeen: 'Just now' },
  fill: { role: 'Load', parent: 'Distro A', voltage: '238.8 V', current: '18.6 A', power: '4.2 kW', factor: '0.95', frequency: '59.9 Hz', signal: '−65 dBm', signalLabel: 'Good', statusLabel: 'Operating normally', updated: 'Updated 5 sec ago', lastSeen: 'Just now' },
  camera: { role: 'Load', parent: 'Distro A', voltage: '239.0 V', current: '6.8 A', power: '1.5 kW', factor: '0.93', frequency: '59.9 Hz', signal: '−59 dBm', signalLabel: 'Excellent', statusLabel: 'Operating normally', updated: 'Updated 4 sec ago', lastSeen: 'Just now' },
  video: { role: 'Load', parent: 'Distro A', voltage: '—', current: '—', power: '—', factor: '—', frequency: '—', signal: '—', signalLabel: 'Offline', statusLabel: 'No data', updated: 'Last update 18 min ago', lastSeen: '18 minutes ago' },
  sound: { role: 'Load', parent: 'Distro B', voltage: '240.0 V', current: '4.2 A', power: '0.9 kW', factor: '0.91', frequency: '60.0 Hz', signal: '−64 dBm', signalLabel: 'Good', statusLabel: 'Operating normally', updated: 'Updated 7 sec ago', lastSeen: 'Just now' },
  craft: { role: 'Load', parent: 'Distro B', voltage: '236.9 V', current: '28.9 A', power: '6.7 kW', factor: '0.98', frequency: '59.8 Hz', signal: '−69 dBm', signalLabel: 'Good', statusLabel: 'Overcurrent', updated: 'Updated 2 sec ago', lastSeen: 'Just now' },
  trailer: { role: 'Load', parent: 'Distro B', voltage: '239.7 V', current: '16.2 A', power: '3.7 kW', factor: '0.95', frequency: '60.0 Hz', signal: '−75 dBm', signalLabel: 'Fair', statusLabel: 'Operating normally', updated: 'Updated 11 sec ago', lastSeen: 'Just now' },
  cameraaux: { role: 'Load', parent: 'Distro B', voltage: '240.1 V', current: '7.4 A', power: '1.7 kW', factor: '0.96', frequency: '60.0 Hz', signal: '−57 dBm', signalLabel: 'Excellent', statusLabel: 'Operating normally', updated: 'Added just now', lastSeen: 'Just now' },
}

function TopologyGraph({ mini = false, selectedId = 'a', onSelect, demoAdded = false, simplified = false, activeBranch = null }: { mini?: boolean; selectedId?: string; onSelect?: (id: string) => void; demoAdded?: boolean; simplified?: boolean; activeBranch?: string | null }) {
  if (mini) {
    return <div className="mini-graph">
      <div className="mini-node source"><Icon name="bolt" size={14} /><span>Main Distro</span></div>
      <div className="mini-line" />
      <div className="mini-row"><div className="mini-node"><Icon name="plug" size={14} /><span>Camera Cart</span></div><div className="mini-node highlighted"><Icon name="plus" size={14} /><span>New meter</span></div></div>
    </div>
  }
  const allNodes = demoAdded
    ? [...nodes, demoNode].map(node => node.type === 'load' ? { ...node, x: [4, 17, 30, 43, 56, 69, 82, 95][['key', 'fill', 'camera', 'video', 'sound', 'craft', 'trailer', 'cameraaux'].indexOf(node.id)] } : node)
    : nodes
  const branchNodes: Record<string, string[]> = {
    gen: ['gen', 'battery', 'main'],
    battery: ['gen', 'battery', 'main'],
    main: ['gen', 'battery', 'main', 'a', 'b'],
    a: ['gen', 'battery', 'main', 'a', 'key', 'fill', 'camera', 'video'],
    b: ['gen', 'battery', 'main', 'b', 'sound', 'craft', 'trailer', ...(demoAdded ? ['cameraaux'] : [])],
  }
  const visibleIds = simplified ? new Set(activeBranch ? branchNodes[activeBranch] : ['gen', 'battery']) : null
  const visibleNodes = visibleIds ? allNodes.filter(node => visibleIds.has(node.id)) : allNodes
  const nodeX = (id: string) => allNodes.find(node => node.id === id)?.x ?? 50
  const edges = [
    { from: 'gen', to: 'main', line: [28, 17, 48, 28] },
    { from: 'battery', to: 'main', line: [68, 17, 48, 28] },
    { from: 'main', to: 'a', line: [48, 40, 25, 51] },
    { from: 'main', to: 'b', line: [48, 40, 72, 51] },
    ...['key', 'fill', 'camera', 'video'].map(id => ({ from: 'a', to: id, line: [25, 63, nodeX(id), 79] })),
    ...['sound', 'craft', 'trailer', ...(demoAdded ? ['cameraaux'] : [])].map(id => ({ from: 'b', to: id, line: [72, 63, nodeX(id), 79] })),
  ]
  const visibleEdges = visibleIds ? edges.filter(edge => visibleIds.has(edge.from) && visibleIds.has(edge.to)) : edges
  return (
    <div className={`graph-canvas ${simplified ? 'simplified' : ''}`}>
      <div className="graph-label source-label" /><div className="graph-label load-label" />
      <svg className="connectors" viewBox="0 0 100 100" preserveAspectRatio="none">{visibleEdges.map(({ line: l }, i) => <path key={i} d={`M ${l[0]} ${l[1]} L ${l[0]} ${(l[1] + l[3]) / 2} L ${l[2]} ${(l[1] + l[3]) / 2} L ${l[2]} ${l[3]}`} />)}</svg>
      {visibleNodes.map(node => {
        const isRootSource = node.id === 'gen' || node.id === 'battery'
        return <button type="button" aria-label={`View ${node.name} details`} onClick={() => onSelect?.(node.id)} key={node.id} className={`node-wrap ${node.id === selectedId ? 'selected' : ''}`} style={{ left: `${node.x}%`, top: `${node.y}%` }}>
          <span className={`node-tooltip ${node.status}`}><Icon name={node.status === 'ok' ? 'check' : node.status === 'nodata' ? 'minus' : 'alert'} size={11} />{meterDetails[node.id].statusLabel}</span>
          <div className={`meter-node ${isRootSource ? 'source' : 'load'} ${node.status}`}><Icon name={isRootSource ? (node.id === 'battery' ? 'battery' : 'bolt') : 'plug'} size={isRootSource ? 21 : 17} /><span className="node-state">{node.status === 'ok' ? <Icon name="check" size={10} /> : node.status === 'nodata' ? <Icon name="minus" size={10} /> : <Icon name="alert" size={10} />}</span></div>
          <div className="node-copy"><b>{node.name}</b>{node.sub && <small>{node.sub}</small>}</div>
        </button>
      })}
    </div>
  )
}

function SelectedPanel({ setPage, selectedId, onDelete }: { setPage: (page: Page) => void; selectedId: string; onDelete: () => void }) {
  const snapshot = useSnapshot()
  const node = selectedId === demoNode.id ? demoNode : nodes.find(item => item.id === selectedId) ?? nodes[3]
  const details = meterDetails[node.id]
  const live = snapshot.latest[node.id]
  const ageSeconds = live?.measuredAt ? Math.max(0, Math.floor((Date.now() - live.measuredAt) / 1000)) : null
  const stale = ageSeconds !== null && ageSeconds > 5
  const value = (number: number | null | undefined, digits: number) =>
    number === null || number === undefined ? '—' : number.toFixed(digits)
  const metrics = [
    ['Voltage', live?.voltage == null ? details.voltage : `${value(live.voltage, 1)} V`],
    ['Current', live?.current == null ? '—' : `${value(live.current, 1)} A`],
    ['Power', live?.power == null ? '—' : live.power >= 1000 ? `${value(live.power / 1000, 1)} kW` : `${value(live.power, 0)} W`],
    ['Power factor', live?.powerFactor == null ? '—' : value(live.powerFactor, 2)],
    ['Frequency', live?.frequency == null ? '—' : `${value(live.frequency, 1)} Hz`],
    ['Signal', live?.signalDbm == null ? '—' : `${value(live.signalDbm, 0)} dBm`],
  ]
  return <aside className="details-panel" id="meter-details">
    <div className="panel-head"><div><div className="eyebrow">SELECTED METER</div><strong>{node.name}</strong></div><span className="selected-indicator"><Icon name="activity" size={16} /></span></div>
    <div className="detail-status"><StatusBadge status={stale ? 'nodata' : node.status}>{stale ? 'Stale / offline' : details.statusLabel}</StatusBadge><span>{ageSeconds === null ? 'Simulated · no reading' : stale ? `Stale · ${ageSeconds}s old` : `Simulated · updated ${ageSeconds}s ago`}</span></div>
    <div className="detail-meta"><span>ROLE<b>{details.role}</b></span><span>PARENT<b>{details.parent}</b></span></div>
    <div className="metric-grid">{metrics.map(([name, value]) => <div key={name}><span>{name}</span><b>{value}</b>{name === 'Signal' && <small className={node.status === 'nodata' ? 'offline-signal' : ''}><Icon name="signal" size={13} /> {details.signalLabel}</small>}</div>)}</div>
    <div className="trend-head"><span>Load trend</span><small>Last 30 min</small></div>
    <svg className="sparkline" viewBox="0 0 270 64" preserveAspectRatio="none"><path className="spark-fill" d="M0 55 L0 44 20 46 42 38 64 40 85 32 108 35 130 27 152 31 174 22 196 25 218 16 240 20 270 10 L270 64Z" /><path d="M0 44 20 46 42 38 64 40 85 32 108 35 130 27 152 31 174 22 196 25 218 16 240 20 270 10" /></svg>
    <div className="panel-foot"><div className="panel-actions"><button onClick={() => setPage('history')}>View history <Icon name="arrow" size={15} /></button>{node.id === demoNode.id && <button className="delete-device" onClick={onDelete}><Icon name="x" size={14} />Delete demo device</button>}</div></div>
  </aside>
}

function DataNote() {
  return <div className="data-note"><Icon name="info" size={13} />Not for billing or compliance.</div>
}

function HomePage({ setPage, selectedId, setSelectedId, demoAdded, onDeleteDemo }: { setPage: (page: Page) => void; selectedId: string; setSelectedId: (id: string) => void; demoAdded: boolean; onDeleteDemo: () => void }) {
  const snapshot = useSnapshot()
  const [networkView, setNetworkView] = useState<'full' | 'simple'>('full')
  const [activeBranch, setActiveBranch] = useState<string | null>(null)
  const ageSeconds = Math.max(0, Math.floor((Date.now() - snapshot.receivedAt) / 1000))
  const stale = ageSeconds > 5
  const onlineCount = Object.values(snapshot.latest).filter((measurement) =>
    measurement.receivedAt !== null && Date.now() - measurement.measuredAt <= 5000
  ).length
  const activeAlerts = snapshot.alerts.filter((alert) => !alert.acknowledged).length
  const mainPower = snapshot.latest.main?.power
  const totalPower = mainPower == null ? 0 : mainPower / 1000
  const parentIds = new Set(['gen', 'battery', 'main', 'a', 'b'])
  const selectMeter = (id: string) => {
    setSelectedId(id)
    if (networkView === 'simple' && parentIds.has(id)) setActiveBranch(id)
    if (window.matchMedia('(max-width: 900px)').matches) {
      window.setTimeout(() => document.getElementById('meter-details')?.scrollIntoView({ behavior: 'smooth', block: 'start' }), 0)
    }
  }
  const changeNetworkView = (view: 'full' | 'simple') => {
    setNetworkView(view)
    if (view === 'simple') {
      setActiveBranch(null)
      setSelectedId('gen')
    }
  }
  return <div className="page home-page">
    <div className="page-heading"><div><div className="eyebrow">SIMULATED DATA · DEMO ACCESS</div><div className="title">Power Map</div></div><span className="updated"><Icon name="clock" size={44} className="edited-update-icon" />{stale ? 'Stale / offline' : `Live · updated ${ageSeconds}s ago`}</span></div>
    <section className="site-summary edited-site-summary">
      <div className="summary-message"><div /></div>
      <div className="summary-stats edited-summary-stats"><div><span>SENSORS ONLINE</span><b>{onlineCount} <small>/ {snapshot.devices.length}</small></b></div><div><span>ACTIVE ALERTS</span><b className="amber-text">{activeAlerts}</b></div><div><span>SIMULATED LOAD</span><b>{totalPower.toFixed(1)} <small>kW</small></b></div></div>
    </section>
    <div className="home-grid">
      <section className="topology-card">
        <div className="card-toolbar"><div className="network-heading"><strong>Live network</strong><span className="view-hint">{networkView === 'simple' ? (activeBranch ? 'Showing selected branch' : 'Select a source to reveal its children') : ''}</span></div><div className="network-tools"><div className="legend"><span><i className="legend-shape source" />Source</span><span><i className="legend-shape load" />Load</span><span><i className="legend-dot nodata-dot" />No data</span></div><div className="view-toggle" aria-label="Network view"><button className={networkView === 'full' ? 'active' : ''} aria-pressed={networkView === 'full'} onClick={() => changeNetworkView('full')}>Full</button><button className={networkView === 'simple' ? 'active' : ''} aria-pressed={networkView === 'simple'} onClick={() => changeNetworkView('simple')}>Simple</button></div></div></div>
        <TopologyGraph selectedId={selectedId} onSelect={selectMeter} demoAdded={demoAdded} simplified={networkView === 'simple'} activeBranch={activeBranch} />
      </section>
      <SelectedPanel setPage={setPage} selectedId={selectedId} onDelete={onDeleteDemo} />
    </div>
    <DataNote />
  </div>
}

function MethodCard({ icon, title, text, active }: { icon: IconName; title: string; text: string; active?: boolean }) {
  return <div className={`method-card ${active ? 'active' : ''}`}><span className="method-icon"><Icon name={icon} size={20} /></span><strong>{title}</strong><small>{text}</small>{active && <span className="active-check"><Icon name="check" size={11} /></span>}</div>
}

function Step({ number, title, children, complete }: { number: number; title: string; children: ReactNode; complete?: boolean }) {
  return <section className="step-section"><div className={`step-number ${complete ? 'complete' : ''}`}>{complete ? <Icon name="check" size={15} /> : number}</div><div className="step-body"><div className="step-title"><span>STEP {number}</span><strong>{title}</strong></div>{children}</div></section>
}

function Field({ label, value, detected = false }: { label: string; value: string; detected?: boolean }) {
  return <div className="field"><label>{label}<small>{detected ? 'Detected' : 'Entered by you'}</small></label><div className="field-box">{value}<Icon name="edit" size={14} /></div></div>
}

function DevicePreview() {
  return <div className="preview-panel inline-preview"><div><div className="eyebrow">LIVE PREVIEW</div><strong /><p /><TopologyGraph mini /></div></div>
}

function AddDevicePage({ setPage, onAddDemo, demoAdded }: { setPage: (page: Page) => void; onAddDemo: () => void; demoAdded: boolean }) {
  return <div className="page add-page">
    <div className="page-heading add-heading"><div><div className="eyebrow">DEVICE SETUP</div><div className="title">Add a new meter</div><p>Link a PowerEye sensor and place it in the site topology.</p></div><Button kind="quiet" icon="x" onClick={() => setPage('home')}>Close</Button></div>
    <div className="progress-track">{['Identify', 'Details', 'Role', 'Review'].map((label, i) => <div className={i < 3 ? 'done' : 'current'} key={label}><span>{i < 3 ? <Icon name="check" size={12} /> : i + 1}</span><b>{label}</b></div>)}</div>
    <div className="add-layout">
      <main className="stepper">
        <Step number={1} title="Identify the sensor" complete>
          <div className="method-grid"><MethodCard icon="camera" title="Scan QR / barcode" text="Use device camera" active /><MethodCard icon="edit" title="Type device ID" text="Enter manually" /><MethodCard icon="upload" title="Photograph label" text="Detect label text" /></div>
          <div className="scan-confirm"><div className="scan-view"><span className="scan-corner tl" /><span className="scan-corner tr" /><span className="scan-corner bl" /><span className="scan-corner br" /><Icon name="camera" size={22} /><small>Label captured</small></div><div className="detected-card"><div><span className="eyebrow">DEVICE DETECTED</span><StatusBadge status="ok">Ready to link</StatusBadge></div><strong>PE-SENSOR-9X42-1187</strong><span>PowerEye Flex Meter · Model PF-200</span><button><Icon name="edit" size={14} />Edit</button></div></div>
        </Step>
        <Step number={2} title="Add details" complete>
          <div className="field-grid"><Field label="Device name" value="Camera Cart Aux" /><Field label="Voltage (V)" value="240" detected /><Field label="Frequency (Hz)" value="60" detected /></div>
          <div className="alternate-fill"><Icon name="camera" size={15} />Scan or photograph a panel label to fill these details automatically</div>
        </Step>
        <Step number={3} title="What is this meter measuring?" complete>
          <div className="role-options"><div><span><Icon name="bolt" size={22} /></span><div><strong>Power source</strong><small>Generator, battery, or grid feed</small></div></div><div className="selected"><span><Icon name="plug" size={22} /></span><div><strong>Load</strong><small>Equipment or downstream circuit</small></div><Icon name="check" size={17} /></div></div>
          <div className="parent-picker"><label>Which meter does this load connect to?</label><div className="select-box"><span><Icon name="search" size={16} />Distro B</span><Icon name="chevron" size={16} /></div><div className="select-menu"><div><span className="parent-node"><Icon name="bolt" size={14} /></span><span><b>Distro B</b><small>31.5 kW · OK</small></span><Icon name="check" size={15} /></div><div><span className="parent-node empty"><Icon name="minus" size={14} /></span><span><b>None, top-level</b><small>Not connected to another meter</small></span></div></div></div>
          <DevicePreview />
        </Step>
        <Step number={4} title="Review and add">
          <div className="review-card"><div className="review-device"><span><Icon name="plug" size={20} /></span><div><strong>Camera Cart Aux</strong><small>PE-SENSOR-9X42-1187</small></div><StatusBadge status="ok">Ready</StatusBadge></div><div className="review-grid"><span>MEASURES<b>Load</b></span><span>CONNECTED TO<b>Distro B</b></span><span>RATED VOLTAGE<b>240 V</b></span><span>FREQUENCY<b>60 Hz</b></span></div></div>
          <div className="review-actions"><Button onClick={() => setPage('home')}>Cancel</Button><Button kind="primary" icon={demoAdded ? 'check' : 'plus'} onClick={onAddDemo}>{demoAdded ? 'View on site' : 'Add to site'}</Button></div>
        </Step>
      </main>
    </div>
  </div>
}

const chartLines = [
  { name: 'Generator 1', cls: 'line-a', points: '0,132 45,128 85,136 125,114 170,118 214,95 255,102 300,82 345,88 390,62 435,70 480,55 525,66 570,44 620,52 670,34 720,45 770,30 820,38' },
  { name: 'Distro A', cls: 'line-b', points: '0,154 45,148 85,152 125,142 170,145 214,132 255,139 300,119 345,126 390,111 435,118 480,97 525,105 570,88 620,92 670,78 720,90 770,69 820,76' },
  { name: 'Craft Services', cls: 'line-c', points: '0,184 45,181 85,176 125,180 170,170 214,176 255,164 300,170 345,158 390,164 435,151 480,156 525,143 570,147 620,128 670,135 720,115 770,123 820,109' },
]

function HistoryChart() {
  const snapshot = useSnapshot()
  const ids = ['gen', 'a', 'craft']
  const classes = ['line-a', 'line-b', 'line-c']
  const names = ['Generator 1', 'Distro A', 'Craft Services']
  const lines = ids.map((id, index) => {
    const points = snapshot.history
      .filter((measurement) => measurement.deviceId === id)
      .slice(-120)
      .map((measurement, pointIndex, measurements) => {
        const x = measurements.length <= 1 ? 820 : (pointIndex / (measurements.length - 1)) * 820
        const kilowatts = (measurement.power ?? 0) / (measurement.power !== null && id === 'gen' ? 1000 : 1000)
        const y = 200 - Math.min(1, kilowatts / 100) * 200
        return `${x.toFixed(1)},${y.toFixed(1)}`
      })
      .join(' ')
    return { name: names[index], cls: classes[index], points }
  })
  return <div className="chart-wrap">
    <div className="chart-y"><span>100 kW</span><span>75</span><span>50</span><span>25</span><span>0</span></div>
    <svg className="history-chart" viewBox="0 0 820 220" preserveAspectRatio="none">
      {[20, 65, 110, 155, 200].map(y => <line key={y} x1="0" y1={y} x2="820" y2={y} className="grid-line" />)}
      <rect x="236" y="0" width="44" height="200" className="no-data-band" /><text x="243" y="17">NO DATA</text>
      <line x1="390" y1="0" x2="390" y2="200" className="event-line warning" /><circle cx="390" cy="15" r="7" className="event-marker warning" /><path d="M390 11v5M390 19h.01" />
      <line x1="670" y1="0" x2="670" y2="200" className="event-line" /><circle cx="670" cy="15" r="7" className="event-marker" /><path d="M666 15h8" />
      {lines.map(line => line.points && <polyline key={line.name} points={line.points} className={line.cls} />)}
    </svg>
    <div className="chart-x"><span>06:00</span><span>10:00</span><span>14:00</span><span>18:00</span><span>Now</span></div>
  </div>
}

function HistoryPage() {
  const snapshot = useSnapshot()
  const exportCsv = () => {
    const rows = [
      ['measured_at', 'received_at', 'hub_id', 'device_id', 'voltage_v', 'current_a', 'power_w', 'power_factor', 'frequency_hz', 'signal_dbm', 'source'],
      ...snapshot.history.map((measurement) => [
        new Date(measurement.measuredAt).toISOString(),
        measurement.receivedAt ? new Date(measurement.receivedAt).toISOString() : '',
        measurement.hubId,
        measurement.deviceId,
        measurement.voltage ?? '',
        measurement.current ?? '',
        measurement.power ?? '',
        measurement.powerFactor ?? '',
        measurement.frequency ?? '',
        measurement.signalDbm ?? '',
        measurement.source,
      ]),
    ]
    const csv = rows.map((row) => row.map((value) => `"${String(value).replaceAll('"', '""')}"`).join(',')).join('\n')
    const url = URL.createObjectURL(new Blob([csv], { type: 'text/csv;charset=utf-8' }))
    const anchor = document.createElement('a')
    anchor.href = url
    anchor.download = `powereye-${snapshot.hub.id}-history.csv`
    anchor.click()
    URL.revokeObjectURL(url)
  }
  const deviceName = (deviceId: string) =>
    snapshot.devices.find((device) => device.id === deviceId)?.name ?? deviceId
  const events = snapshot.alerts.map((alert) => ({
    time: new Date(alert.createdAt).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' }),
    device: deviceName(alert.deviceId),
    message: alert.message,
    severity: alert.severity,
  }))
  return <div className="page history-page">
    <div className="page-heading"><div><div className="eyebrow">SITE ANALYSIS · SIMULATED DATA</div><div className="title">History</div><p>Compare demo meter measurements and review demo alerts.</p></div><Button icon="download" onClick={exportCsv}>Export CSV</Button></div>
    <section className="filter-card">
      <div className="filter-group devices"><label>DEVICES</label><div className="chips"><span><i className="series-dot a" />Generator 1<Icon name="x" size={12} /></span><span><i className="series-dot b" />Distro A<Icon name="x" size={12} /></span><span><i className="series-dot c" />Craft Services<Icon name="x" size={12} /></span><button><Icon name="plus" size={14} />Add meter</button></div></div>
      <div className="filter-group"><label>METRIC</label><div className="segmented"><button>Voltage</button><button>Current</button><button className="active">Power</button><button>Power factor</button><button>Frequency</button><button>Energy</button></div></div>
      <div className="filter-group range"><label>TIME RANGE</label><div className="segmented"><button>1 h</button><button>6 h</button><button className="active">24 h</button><button>7 d</button><button>Custom</button></div></div>
    </section>
    <section className="chart-card">
      <div className="chart-head"><div><strong>Power over time</strong><span>Tuesday, October 22 · 06:00–19:14</span></div><div className="chart-legend">{chartLines.map(line => <span key={line.name}><i className={`series-dot ${line.cls.slice(-1)}`} />{line.name}</span>)}</div></div>
      <HistoryChart />
    </section>
    <div className="stat-cards"><div><span>PEAK LOAD</span><strong>94.7 <small>kW</small></strong><small><Icon name="activity" size={13} />18:32 · Generator 1</small></div><div><span>AVERAGE LOAD</span><strong>67.3 <small>kW</small></strong><small>Across selected meters</small></div><div><span>VOLTAGE RANGE</span><strong>232.1–241.8 <small>V</small></strong><small><Icon name="check" size={13} />Within expected range</small></div><div><span>TOTAL ENERGY</span><strong>1,284 <small>kWh</small></strong><small>Selected period</small></div></div>
    <section className="events-card">
      <div className="events-head"><div><strong>Events & alerts</strong><span>{events.length} simulated alerts</span></div><button>View all <Icon name="arrow" size={14} /></button></div>
      <div className="event-table"><div className="table-head"><span>TIME</span><span>DEVICE</span><span>EVENT TYPE</span><span>SEVERITY</span><span /></div>{events.map((event, index) => <div className="table-row" key={`${event.time}-${event.device}-${index}`}><span>{event.time}</span><strong>{event.device}</strong><span>{event.message}</span><span><StatusBadge status={event.severity === 'critical' ? 'critical' : event.severity === 'warning' ? 'warning' : 'ok'}>{event.severity}</StatusBadge></span><Icon name="chevron" size={15} /></div>)}</div>
    </section>
    <DataNote />
  </div>
}

export default function App() {
  const [page, setPage] = useState<Page>('home')
  const [selectedId, setSelectedId] = useState('a')
  const [demoAdded, setDemoAdded] = useState(false)
  const hubMatch = window.location.pathname.match(/^\/hub\/([^/]+)\/?/)
  const defaultHubId = import.meta.env.VITE_DEMO_HUB_ID?.trim() || 'demo-hub'
  const hubId = hubMatch?.[1] ? decodeURIComponent(hubMatch[1]) : defaultHubId
  const snapshot = useTelemetry(hubId)

  useEffect(() => {
    if (!window.location.pathname.startsWith('/hub/')) {
      window.history.replaceState({}, '', `/hub/${encodeURIComponent(hubId)}`)
    }
  }, [hubId])

  const addDemo = () => {
    setDemoAdded(true)
    setSelectedId(demoNode.id)
    setPage('home')
  }
  const deleteDemo = () => {
    setDemoAdded(false)
    setSelectedId('b')
  }
  return <TelemetryContext.Provider value={snapshot}>
    <div className="app-shell">
      <Nav page={page} setPage={setPage} />
      <div className="workspace">
        <Topbar page={page} setPage={setPage} />
        {page === 'home' && <HomePage setPage={setPage} selectedId={selectedId} setSelectedId={setSelectedId} demoAdded={demoAdded} onDeleteDemo={deleteDemo} />}
        {page === 'add' && <AddDevicePage setPage={setPage} onAddDemo={addDemo} demoAdded={demoAdded} />}
        {page === 'history' && <HistoryPage />}
      </div>
    </div>
  </TelemetryContext.Provider>
}
