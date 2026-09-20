/**
 * @file paneRegistry.js
 * @brief DPPA (Dashboard Pane Plugin Architecture) - Core Pane Registry
 * 
 * Centralizes all dashboard pane definitions, metadata, icons, and components.
 * Eliminates tight coupling in App.jsx by providing a declarative plugin registry
 * inspired by F-BB's Peripheral Plugin Architecture (PPA: ADR #005).
 */

import React from 'react';
import {
  Cpu,
  Activity,
  Terminal,
  Layers,
  Tv,
  HardDrive,
  FileCode,
  Monitor,
  ShieldAlert,
  Car,
  Zap,
  Gauge,
  MapPin,
  Navigation
} from 'lucide-react';

import RegisterMonitor from '../components/RegisterMonitor';
import GpioPanel from '../components/GpioPanel';
import UartTerminal from '../components/UartTerminal';
import RegisterTracer from '../components/RegisterTracer';
import HdmiOutput from '../components/HdmiOutput';
import SdCardPanel from '../components/SdCardPanel';
import DtsVisualizer from '../components/DTSVisualizer';
import GenericPeripheralPane from '../components/GenericPeripheralPane';
import TransactionLoggerPane from '../components/TransactionLoggerPane';
import CanAnalyzerPane from '../components/CanAnalyzerPane';
import ChaosPanel from '../components/ChaosPanel';
import Ros2ControlStatusPane from '../components/Ros2ControlStatusPane';
import Ros2JointWaveformPane from '../components/Ros2JointWaveformPane';
import Ros2TeleopConsolePane from '../components/Ros2TeleopConsolePane';
import Ros2PoseMap2DPane from '../components/Ros2PoseMap2DPane';

export const PANE_CATEGORIES = {
  OBSERVABILITY: 'Observability & Control',
  PERIPHERALS: 'Peripherals & I/O',
  SERIAL: 'Serial Terminals',
  EXTENSIONS: 'Add-ons & Robotics'
};

/**
 * Built-in Core Panes
 */
export const BUILTIN_PANES = {
  registerMonitor: {
    id: 'registerMonitor',
    title: 'Registers',
    category: PANE_CATEGORIES.OBSERVABILITY,
    icon: Cpu,
    component: RegisterMonitor,
    defaultParams: {}
  },
  gpioPanel: {
    id: 'gpioPanel',
    title: 'GPIO / Pin Array',
    category: PANE_CATEGORIES.OBSERVABILITY,
    icon: Activity,
    component: GpioPanel,
    defaultParams: {}
  },
  canAnalyzer: {
    id: 'canAnalyzer',
    title: 'CAN Bus Analyzer',
    category: PANE_CATEGORIES.OBSERVABILITY,
    icon: Car,
    component: CanAnalyzerPane,
    defaultParams: {}
  },
  registerTracer: {
    id: 'registerTracer',
    title: 'Tracer',
    category: PANE_CATEGORIES.OBSERVABILITY,
    icon: Layers,
    component: RegisterTracer,
    defaultParams: {}
  },
  transactionLogger: {
    id: 'transactionLogger',
    title: 'Transaction Logger',
    category: PANE_CATEGORIES.OBSERVABILITY,
    icon: ShieldAlert,
    component: TransactionLoggerPane,
    defaultParams: {}
  },
  chaosEngine: {
    id: 'chaosEngine',
    title: 'Chaos & Fault Injection',
    category: PANE_CATEGORIES.OBSERVABILITY,
    icon: Zap,
    component: ChaosPanel,
    defaultParams: {}
  },
  dtsVisualizer: {
    id: 'dtsVisualizer',
    title: 'DTS Visualizer & AI',
    category: PANE_CATEGORIES.OBSERVABILITY,
    icon: FileCode,
    component: DtsVisualizer,
    defaultParams: {}
  },
  uartTerminal: {
    id: 'uartTerminal',
    title: 'UART Console',
    category: PANE_CATEGORIES.SERIAL,
    icon: Terminal,
    component: UartTerminal,
    multiInstance: true,
    defaultParams: { deviceName: 'default' }
  },
  genericPeripheralPane: {
    id: 'genericPeripheralPane',
    title: 'Virtual Peripheral View',
    category: PANE_CATEGORIES.PERIPHERALS,
    icon: Monitor,
    component: GenericPeripheralPane,
    multiInstance: true,
    defaultParams: { type: 'standby' }
  },
  sdCard: {
    id: 'sdCard',
    title: 'Virtual SD Card',
    category: PANE_CATEGORIES.PERIPHERALS,
    icon: HardDrive,
    component: SdCardPanel,
    defaultParams: {}
  },
  hdmiOutput: {
    id: 'hdmiOutput',
    title: 'HDMI Output Preview',
    category: PANE_CATEGORIES.PERIPHERALS,
    icon: Tv,
    component: HdmiOutput,
    defaultParams: {}
  },
  ros2ControlStatus: {
    id: 'ros2ControlStatus',
    title: 'ros2_control Status & Jitter',
    category: PANE_CATEGORIES.EXTENSIONS,
    icon: Gauge,
    component: Ros2ControlStatusPane,
    defaultParams: {}
  },
  ros2JointWaveform: {
    id: 'ros2JointWaveform',
    title: 'ROS 2 Joint Tracking Waveform',
    category: PANE_CATEGORIES.EXTENSIONS,
    icon: Activity,
    component: Ros2JointWaveformPane,
    defaultParams: {}
  },
  ros2TeleopConsole: {
    id: 'ros2TeleopConsole',
    title: 'ROS 2 Teleop & E-STOP Console',
    category: PANE_CATEGORIES.EXTENSIONS,
    icon: Navigation,
    component: Ros2TeleopConsolePane,
    defaultParams: {}
  },
  ros2PoseMap2D: {
    id: 'ros2PoseMap2D',
    title: 'AMR 2D Pose & Map',
    category: PANE_CATEGORIES.EXTENSIONS,
    icon: MapPin,
    component: Ros2PoseMap2DPane,
    defaultParams: {}
  }
};

/**
 * Storage for dynamic custom/add-on panes (e.g. ROS 2 AMR Nav2 map)
 */
const customPanes = new Map();

/**
 * Register a custom pane extension
 * @param {Object} descriptor - Pane definition
 */
export function registerCustomPane(descriptor) {
  if (!descriptor || !descriptor.id || !descriptor.component) {
    console.error('[DPPA] Invalid pane descriptor:', descriptor);
    return;
  }
  customPanes.set(descriptor.id, {
    category: PANE_CATEGORIES.EXTENSIONS,
    icon: Monitor,
    defaultParams: {},
    ...descriptor
  });
}

/**
 * Resolve pane definition by component ID (with legacy alias mapping)
 * @param {string} id
 * @returns {Object|null}
 */
export function getPaneDefinition(id) {
  if (customPanes.has(id)) {
    return customPanes.get(id);
  }
  if (BUILTIN_PANES[id]) {
    return BUILTIN_PANES[id];
  }

  // Aliases for robustness
  if (id === 'tracerPanel' || id === 'tracer' || id === 'registerTracer') {
    return BUILTIN_PANES.registerTracer;
  }
  if (id === 'chaosPanel' || id === 'chaosEngine') {
    return BUILTIN_PANES.chaosEngine;
  }
  if (id === 'canPanel' || id === 'canAnalyzer') {
    return BUILTIN_PANES.canAnalyzer;
  }
  if (id === 'dtsPanel' || id === 'dtsVisualizer') {
    return BUILTIN_PANES.dtsVisualizer;
  }

  // Legacy aliases fallback to genericPeripheralPane
  if (id === 'oledDisplay' || id === 'seg7Display' || id === 'spiAdcPanel') {
    return {
      ...BUILTIN_PANES.genericPeripheralPane,
      id
    };
  }

  return null;
}

/**
 * Generate Dockview `components` map dynamically
 * @returns {Object} { [id]: (props) => ReactElement }
 */
export function getDockviewComponentsMap() {
  const map = {};

  // Register built-in panes
  Object.values(BUILTIN_PANES).forEach(pane => {
    const Component = pane.component;
    map[pane.id] = (props) => <Component {...props} />;
  });

  // Register custom panes
  customPanes.forEach((pane, id) => {
    const Component = pane.component;
    map[id] = (props) => <Component {...props} />;
  });

  // Legacy aliases mapping
  map.oledDisplay = (props) => <GenericPeripheralPane {...props} />;
  map.seg7Display = (props) => <GenericPeripheralPane {...props} />;
  map.spiAdcPanel = (props) => <GenericPeripheralPane {...props} />;

  return map;
}

/**
 * Extract available panes grouped by category for the "+ Add Pane" dropdown menu
 * Dynamically resolves DTS peripherals and UARTs from the active board manifest
 * @param {Object} manifest
 * @returns {Array} Categories array with items
 */
export function getGroupedPanesForMenu(manifest) {
  // Observability & Control items
  const observabilityItems = [
    BUILTIN_PANES.registerMonitor,
    BUILTIN_PANES.gpioPanel,
    BUILTIN_PANES.canAnalyzer,
    BUILTIN_PANES.registerTracer,
    BUILTIN_PANES.transactionLogger,
    BUILTIN_PANES.chaosEngine,
    BUILTIN_PANES.dtsVisualizer
  ].map(p => ({
    id: p.id,
    component: p.id,
    title: p.title,
    icon: p.icon,
    params: p.defaultParams
  }));

  // Peripherals & I/O items (extracted from manifest)
  const i2cSlaves = manifest?.devices?.flatMap(d => d.i2c_slaves || []) || [];
  const spiSlaves = manifest?.devices?.flatMap(d => d.spi_slaves || []) || [];
  const directDevices = manifest?.devices?.filter(d => d.ui_widget || d.compatible?.includes('hub75')) || [];
  const allSlaves = [...i2cSlaves, ...spiSlaves, ...directDevices];

  const hasOled = i2cSlaves.some(s => s.compatible?.includes('ssd1306'));
  const hasSeg7 = i2cSlaves.some(s => s.compatible?.includes('ht16k33'));
  const hasSpiAdc = spiSlaves.some(s => s.compatible?.includes('mcp3208'));
  const hasSdCard = Boolean(manifest?.sd_card_path) || manifest?.devices?.some(d => d.name?.includes('sd') || d.compatible?.includes('sd'));
  const hasHdmi = Boolean(manifest?.hdmi_output_path) || manifest?.devices?.some(d => d.name?.includes('hdmi') || d.compatible?.includes('hdmi'));

  const peripheralItems = [];

  if (hasOled) {
    const oledTitle = i2cSlaves.find(s => s.compatible?.includes('ssd1306'))?.ui_widget?.title || 'SSD1306 OLED Display (128x64)';
    peripheralItems.push({
      id: 'oledDisplay',
      component: 'genericPeripheralPane',
      title: oledTitle,
      icon: Monitor,
      params: { type: 'oled' }
    });
  }

  if (hasSeg7) {
    const seg7Title = i2cSlaves.find(s => s.compatible?.includes('ht16k33'))?.ui_widget?.title || 'Adafruit 4-Digit 7-Segment LED (Red)';
    peripheralItems.push({
      id: 'seg7Display',
      component: 'genericPeripheralPane',
      title: seg7Title,
      icon: Monitor,
      params: { type: 'seg7', pluginId: 'adafruit_ht16k33' }
    });
  }

  if (hasSpiAdc) {
    peripheralItems.push({
      id: 'spiAdcPanel',
      component: 'genericPeripheralPane',
      title: 'SPI ADC (12-bit)',
      icon: Activity,
      params: { type: 'spiAdc' }
    });
  }

  // Dynamic PPA custom peripheral items
  allSlaves.forEach((s, idx) => {
    const isLegacyOled = s.compatible?.includes('ssd1306');
    const isLegacySeg7 = s.compatible?.includes('ht16k33');
    const isLegacySpiAdc = s.compatible?.includes('mcp3208');
    if (!isLegacyOled && !isLegacySeg7 && !isLegacySpiAdc) {
      const pTitle = s.ui_widget?.title || s.name || 'Generic Peripheral';
      const pId = `generic_peripheral_${s.name || idx}_${idx}`;
      peripheralItems.push({
        id: pId,
        component: 'genericPeripheralPane',
        title: pTitle,
        icon: Monitor,
        params: { pluginId: s.compatible, manifest: s }
      });
    }
  });

  if (hasSdCard) {
    peripheralItems.push({
      id: 'sdCard',
      component: 'sdCard',
      title: 'Virtual SD Card',
      icon: HardDrive,
      params: {}
    });
  }

  if (hasHdmi) {
    peripheralItems.push({
      id: 'hdmiOutput',
      component: 'hdmiOutput',
      title: 'HDMI Output Preview',
      icon: Tv,
      params: {}
    });
  }

  if (peripheralItems.length === 0) {
    peripheralItems.push({
      id: 'standbyPeripheral',
      component: 'genericPeripheralPane',
      title: 'Virtual Peripheral View',
      icon: Monitor,
      params: { type: 'standby' }
    });
  }

  // UART Consoles
  const rawUarts = manifest?.uarts || [];
  const uartItems = rawUarts.length > 0
    ? rawUarts.map(u => ({
        id: `uartTerminal_${u.name}`,
        component: 'uartTerminal',
        title: `UART: ${u.name}`,
        params: { deviceName: u.name },
        icon: Terminal
      }))
    : [{ id: 'uartTerminal_default', component: 'uartTerminal', title: 'UART Console', params: { deviceName: 'default' }, icon: Terminal }];

  // Extension Add-on items
  const extensionItems = [
    BUILTIN_PANES.ros2ControlStatus,
    BUILTIN_PANES.ros2JointWaveform,
    BUILTIN_PANES.ros2TeleopConsole,
    BUILTIN_PANES.ros2PoseMap2D
  ].map(p => ({
    id: p.id,
    component: p.id,
    title: p.title,
    icon: p.icon,
    params: p.defaultParams || {}
  }));

  customPanes.forEach(p => {
    extensionItems.push({
      id: p.id,
      component: p.id,
      title: p.title,
      icon: p.icon,
      params: p.defaultParams || {}
    });
  });

  const categories = [
    {
      category: PANE_CATEGORIES.OBSERVABILITY,
      items: observabilityItems
    },
    {
      category: PANE_CATEGORIES.PERIPHERALS,
      items: peripheralItems
    },
    {
      category: PANE_CATEGORIES.SERIAL,
      items: uartItems
    }
  ];

  if (extensionItems.length > 0) {
    categories.push({
      category: PANE_CATEGORIES.EXTENSIONS,
      items: extensionItems
    });
  }

  return categories;
}
