import './compost/components/compost-knob.js';
import './compost/components/compost-scope.js';
import './compost/components/compost-select.js';

const encoder = new TextEncoder();
const decoder = new TextDecoder();
const send = (text) => window.parent.postMessage(encoder.encode(text).buffer, '*');
const parameters = new Map();
const parametersByID = new Map();
const values = new Map();
const svgNamespace = 'http://www.w3.org/2000/svg';

const pageNames = {
  osc1: 'Oscillator 1', osc2: 'Oscillator 2', filter: 'Filter',
  adsr1: 'Envelope', lfo: 'LFO', keyboard: 'Performance'
};
const pageParameters = {
  osc1: ['osc1Shape', 'osc1Tune', 'osc1Level', 'lfoRate', 'lfoToOsc1Shape', 'lfoToOsc1Tune'],
  osc2: ['osc2Shape', 'osc2Tune', 'osc2Level', 'lfoRate', 'lfoToOsc2Shape', 'lfoToOsc2Tune'],
  filter: ['cutoff', 'resonance', 'adsrToCutoff', 'lfoToCutoff'],
  adsr1: ['velocitySensitivity', 'attack', 'decay', 'sustain', 'release'],
  keyboard: ['glideTime', 'glideShape', 'glideTimeRateMorph', 'pitchBendRange']
};
const controlLabels = {
  lfoRate: 'PWM Rate',
  lfoToOsc1Shape: 'PWM Depth', lfoToOsc2Shape: 'PWM Depth',
  lfoToOsc1Tune: 'LFO Amount', lfoToOsc2Tune: 'LFO Amount',
  adsrToCutoff: 'Env Amount', lfoToCutoff: 'LFO Amount'
};

let page = 'osc1';
let modulation = {
  lfoPhase: 0, lfoValue: 0, adsrValue: 0, adsrActive: 0, adsrStage: 0,
  adsrStageProgress: 0, adsrVelocityScale: 1, lfo2Phase: 0, lfo2Value: 0,
  adsr2Value: 0, adsr2Active: 0, adsr2Stage: 0, adsr2StageProgress: 0,
  osc1Shape: 0, osc2Shape: .5, filterCutoff: 20000, filterResonance: 0
};

const clamp = (value, minimum, maximum) => Math.min(maximum, Math.max(minimum, value));

function parameter(identifier) {
  return parameters.get(identifier);
}

function value(identifier) {
  const spec = parameter(identifier);
  return values.get(identifier) ?? spec?.defaultValue ?? 0;
}

function edit(identifier, next) {
  const spec = parameter(identifier);
  if (!spec) return;
  values.set(identifier, Number(next));
  send(`value:${spec.id}:${next}`);
  renderModuleGraphics();
}

function knob(identifier, label = null, compact = false) {
  const spec = parameter(identifier);
  if (!spec) return document.createElement('span');
  const control = document.createElement('compost-knob');
  if (compact) control.className = 'compact';
  const attributes = {
    'parameter-id': spec.id,
    label: label || spec.name,
    min: spec.min,
    max: spec.max,
    value: value(identifier),
    'reset-value': spec.defaultValue,
    step: spec.step
  };
  for (const [name, setting] of Object.entries(attributes)) control.setAttribute(name, setting);
  if (spec.hasMid) control.setAttribute('mid', spec.mid);
  if (spec.curve === 'log') control.setAttribute('curve', 'log');
  if (spec.options) control.setAttribute('options', spec.options);
  if (spec.unit) control.setAttribute('unit', spec.unit);
  return control;
}

function select(identifier, label, options) {
  const spec = parameter(identifier);
  const control = document.createElement('compost-select');
  control.setAttribute('aria-label', label);
  control.setAttribute('parameter-id', spec.id);
  for (const [optionValue, text] of options.entries()) {
    const option = document.createElement('option');
    option.value = String(optionValue);
    option.textContent = text;
    control.append(option);
  }
  control.value = String(Math.round(value(identifier)));
  control.addEventListener('change', () => edit(identifier, Number(control.value)));
  return control;
}

function toggle(identifier, label) {
  const wrapper = document.createElement('label');
  wrapper.className = 'toggle';
  wrapper.append(document.createTextNode(label));
  const input = document.createElement('input');
  input.type = 'checkbox';
  input.setAttribute('parameter-id', parameter(identifier).id);
  input.checked = value(identifier) >= .5;
  input.addEventListener('change', () => edit(identifier, input.checked ? 1 : 0));
  wrapper.append(input);
  return wrapper;
}

function renderPage() {
  document.querySelector('#page-title').textContent = pageNames[page];
  const header = document.querySelector('#header-controls');
  const controls = document.querySelector('#knobs');
  header.replaceChildren();
  controls.replaceChildren();

  if (page === 'osc1' || page === 'osc2') {
    const second = page === 'osc2';
    header.append(select(second ? 'osc2Waveform' : 'osc1Waveform',
      second ? 'Oscillator 2 waveform' : 'Oscillator 1 waveform',
      second ? ['Saw', 'Ramp', 'Pulse', 'Triangle', 'Sine', 'Noise']
             : ['Saw', 'Ramp', 'Pulse', 'Triangle', 'Sine']));
    if (second) header.append(toggle('hardSync', 'SYNC'));
  } else if (page === 'lfo') {
    header.append(select('lfoWaveform', 'LFO waveform', ['Sine', 'Triangle', 'Square']));
  } else if (page === 'keyboard') {
    header.append(toggle('legato', 'LEGATO'));
  }

  let identifiers = pageParameters[page] || [];
  if (page === 'lfo') identifiers = ['lfoRate', 'lfoDepth'];
  for (const identifier of identifiers)
    controls.append(knob(identifier, page === 'lfo' ? null : controlLabels[identifier] || null));
}

function updateRail() {
  for (const button of document.querySelectorAll('.module')) {
    const isCurrent = button.dataset.page === page;
    if (isCurrent) button.setAttribute('aria-current', 'page');
    else button.removeAttribute('aria-current');
  }
}

for (const button of document.querySelectorAll('.module')) {
  button.addEventListener('click', () => {
    page = button.dataset.page;
    updateRail();
    renderPage();
  });
}

function svgElement(name, attributes) {
  const element = document.createElementNS(svgNamespace, name);
  for (const [attribute, setting] of Object.entries(attributes))
    element.setAttribute(attribute, setting);
  return element;
}

function draw(svg, pathData, point = null) {
  const path = svgElement('path', { d: pathData });
  const children = [path];
  if (point) children.push(svgElement('circle', { cx: point.x, cy: point.y, r: 2.5 }));
  svg.replaceChildren(...children);
}

function pathFromSamples(count, sample) {
  const points = [];
  for (let index = 0; index <= count; ++index) {
    const phase = index / count;
    points.push(`${index ? 'L' : 'M'} ${(phase * 48).toFixed(2)} ${(14 - sample(phase) * 9.52).toFixed(2)}`);
  }
  return points.join(' ');
}

function mirrorFold(input) {
  let result = input;
  while (result > 1 || result < -1) result = result > 1 ? 2 - result : -2 - result;
  return result;
}

function oscillatorSample(waveform, shape, phase, cycle = 0) {
  if (waveform === 0 || waveform === 1) {
    const control = waveform === 0 ? clamp(shape, 2, 4) : clamp(shape, 0, 2);
    const foldedDepth = control < 1 ? control : control < 2 ? 2 - control
      : control < 3 ? control - 2 : 4 - control;
    const baseSign = control < 1 || control >= 3 ? 1 : -1;
    const saw = baseSign * (1 - 2 * phase);
    const tau = 1 - foldedDepth;
    if (tau >= .999999) return saw;
    const cyclePolarity = cycle % 2 === 0 ? -1 : 1;
    const polarity = control < 2 ? cyclePolarity : -cyclePolarity;
    const folded = polarity * saw < -tau ? -saw : saw;
    return (2 * folded - polarity * (1 - tau)) / (1 + tau);
  }
  if (waveform === 2) return phase < clamp(shape, .02, .98) ? 1 : -1;
  const raw = waveform === 3
    ? (phase < .5 ? phase * 4 - 1 : 3 - phase * 4)
    : Math.sin(phase * Math.PI * 2);
  if (waveform === 3 || waveform === 4) return mirrorFold(raw * (1 + clamp(shape, 0, 1) * 3));
  if (waveform === 5) {
    const index = Math.floor(phase * 24);
    const noise = Math.sin(index * 12.9898) * 43758.5453;
    return (noise - Math.floor(noise)) * 2 - 1;
  }
  return 0;
}

function oscillatorPath(waveform, shape) {
  const count = waveform === 5 ? 24 : 48;
  const showsCycleEdges = waveform <= 2;
  const pulseWidth = clamp(shape, .02, .98);
  const lastPhase = 1 - 1 / count;
  const boundaryBeforePhase = waveform === 2
    ? Math.min(1 - .000001, Math.max(lastPhase, pulseWidth + .000001))
    : lastPhase;
  const samples = [];

  if (showsCycleEdges) {
    samples.push({ position: 0, phase: boundaryBeforePhase, cycle: -1, order: -2 });
    samples.push({ position: 0, phase: 0, cycle: 0, order: -1 });
    for (let index = 1; index < count; ++index) {
      const phase = index / count;
      samples.push({ position: phase, phase, cycle: 0, order: 0 });
    }
    if (waveform === 0 || waveform === 1) {
      const control = waveform === 0 ? clamp(shape, 2, 4) : clamp(shape, 0, 2);
      const edge = waveform === 0 ? (control - 2) / 2 : control / 2;
      if (edge > .000001 && edge < 1 - .000001) {
        samples.push({ position: edge, phase: edge - .000001, cycle: 0, order: -1 });
        samples.push({ position: edge, phase: edge + .000001, cycle: 0, order: 1 });
      }
    }
    if (waveform === 2) {
      samples.push({ position: pulseWidth, phase: pulseWidth - .000001, cycle: 0, order: -1 });
      samples.push({ position: pulseWidth, phase: pulseWidth, cycle: 0, order: 1 });
    }
    samples.push({ position: 1, phase: boundaryBeforePhase, cycle: 0, order: 0 });
    samples.push({ position: 1, phase: 0, cycle: 1, order: 1 });
  } else {
    for (let index = 0; index <= count; ++index) {
      const phase = index / count;
      samples.push({ position: phase, phase, cycle: 0, order: 0 });
    }
  }

  samples.sort((a, b) => a.position - b.position || a.order - b.order);
  return samples.map(({ position, phase, cycle }, index) => {
    const x = 1.5 + position * 45;
    const y = 14 - oscillatorSample(waveform, shape, phase, cycle) * 12.32;
    return `${index ? 'L' : 'M'} ${x.toFixed(2)} ${y.toFixed(2)}`;
  }).join(' ');
}

function renderOscillator(identifier, shapeIdentifier, visualIdentifier) {
  const waveform = Math.round(value(identifier));
  const shape = modulation[shapeIdentifier] || value(shapeIdentifier);
  draw(document.querySelector(`[data-visual="${visualIdentifier}"]`), oscillatorPath(waveform, shape));
}

function renderFilter() {
  const cutoff = clamp(modulation.filterCutoff || value('cutoff'), 20, 20000);
  const resonance = modulation.filterResonance || value('resonance');
  const cutoffPosition = Math.log(cutoff / 20) / Math.log(1000);
  const path = pathFromSamples(48, (position) => {
    const ratio = Math.pow(1000, position - cutoffPosition);
    const lowPass = 1 / Math.sqrt(1 + Math.pow(ratio, 4));
    const distance = (position - cutoffPosition) / .075;
    const bump = clamp(resonance / 100, 0, 1) * Math.exp(-.5 * distance * distance) * .74;
    const response = clamp(lowPass + bump, 0, 1.45);
    return (response / 1.45 - .5) * 2;
  });
  draw(document.querySelector('[data-visual="filter"]'), path);
}

function lfoSample(waveform, phase) {
  if (waveform === 1) return 1 - 4 * Math.abs(phase - .5);
  if (waveform === 2) return phase < .5 ? 1 : -1;
  return Math.sin(phase * Math.PI * 2);
}

function renderLFO(number) {
  const prefix = number === 1 ? 'lfo' : 'lfo2';
  const waveform = Math.round(value(`${prefix}Waveform`));
  const depth = clamp(value(`${prefix}Depth`) / 100, 0, 1);
  const phase = clamp(modulation[`${prefix}Phase`], 0, 1);
  const current = clamp(modulation[`${prefix}Value`], -1, 1);
  const path = pathFromSamples(48, (position) => lfoSample(waveform, position) * depth);
  draw(document.querySelector(`[data-visual="lfo${number}"]`), path,
    { x: phase * 48, y: 14 - current * 9.52 });
}

function finiteExponential(start, target, progress, completionRatio) {
  const asymptote = (target - start * completionRatio) / (1 - completionRatio);
  return asymptote + (start - asymptote) * Math.pow(completionRatio, clamp(progress, 0, 1));
}

function renderEnvelope(number) {
  const first = number === 1;
  const prefix = first ? '' : 'adsr2';
  const attack = Math.max(.02, value(first ? 'attack' : 'adsr2Attack'));
  const decay = Math.max(.02, value(first ? 'decay' : 'adsr2Decay'));
  const sustain = clamp(value(first ? 'sustain' : 'adsr2Sustain'), 0, 1);
  const release = Math.max(.02, value(first ? 'release' : 'adsr2Release'));
  const hold = Math.max(.08, (attack + decay + release) * .28);
  const total = attack + decay + hold + release;
  const attackX = 48 * attack / total;
  const decayX = 48 * (attack + decay) / total;
  const holdX = 48 * (attack + decay + hold) / total;
  const active = Boolean(modulation[`${prefix || 'adsr'}Active`]);
  const velocityScale = first && active ? clamp(modulation.adsrVelocityScale, 0, 1) : 1;
  const sustainValue = sustain * velocityScale;
  const points = [{ x: 0, y: 0 }];
  const append = (startX, endX, start, target, ratio) => {
    for (let index = 1; index <= 20; ++index) {
      const progress = index / 20;
      points.push({ x: startX + (endX - startX) * progress,
        y: finiteExponential(start, target, progress, ratio) });
    }
  };
  append(0, attackX, 0, velocityScale, .040);
  append(attackX, decayX, velocityScale, sustainValue, .047);
  points.push({ x: holdX, y: sustainValue });
  append(holdX, 48, sustainValue, 0, .049);
  const path = points.map((point, index) =>
    `${index ? 'L' : 'M'} ${point.x.toFixed(2)} ${(25.2 - clamp(point.y, 0, 1) * 21.84).toFixed(2)}`).join(' ');

  const stage = modulation[`${prefix || 'adsr'}Stage`];
  const progress = clamp(modulation[`${prefix || 'adsr'}StageProgress`], 0, 1);
  let pointX = 0;
  if (stage === 1) pointX = attackX * progress;
  else if (stage === 2) pointX = attackX + (decayX - attackX) * progress;
  else if (stage === 3) pointX = (decayX + holdX) * .5;
  else if (stage === 4) pointX = holdX + (48 - holdX) * progress;
  const current = clamp(modulation[`${prefix || 'adsr'}Value`], 0, 1);
  draw(document.querySelector(`[data-visual="adsr${number}"]`), path,
    { x: pointX, y: 25.2 - current * 21.84 });
}

function renderModuleGraphics() {
  renderOscillator('osc1Waveform', 'osc1Shape', 'osc1');
  renderOscillator('osc2Waveform', 'osc2Shape', 'osc2');
  renderFilter();
  renderEnvelope(1);
  renderLFO(1);
}

addEventListener('parameter-begin', ({ detail }) => send(`begin:${detail.parameterID}`));
addEventListener('parameter-edit', ({ detail }) => {
  const spec = parametersByID.get(String(detail.parameterID));
  if (spec) values.set(spec.identifier, Number(detail.value));
  send(`value:${detail.parameterID}:${detail.value}`);
  renderModuleGraphics();
});
addEventListener('parameter-end', ({ detail }) => send(`end:${detail.parameterID}`));

addEventListener('message', ({ data }) => {
  const text = decoder.decode(data);
  if (text.startsWith('parameter\t')) {
    const [, id, identifier, moduleName, name, min, max, defaultValue,
      step, mid, hasMid, curve, options, unit] = text.split('\t');
    const spec = {
      id, identifier, moduleName, name,
      min: Number(min), max: Number(max), defaultValue: Number(defaultValue),
      step: Number(step), mid: Number(mid), hasMid: hasMid === '1', curve, options, unit
    };
    parameters.set(identifier, spec);
    parametersByID.set(id, spec);
    return;
  }
  if (text === 'metadata-end') {
    updateRail();
    renderPage();
    renderModuleGraphics();
    return;
  }
  if (text.startsWith('values:')) {
    for (const pair of text.slice(7).split(';')) {
      const [id, next] = pair.split('=');
      const spec = parametersByID.get(id);
      if (!spec) continue;
      const numeric = Number(next);
      values.set(spec.identifier, numeric);
      for (const control of document.querySelectorAll(`[parameter-id="${id}"]`)) {
        if (control instanceof HTMLInputElement && control.type === 'checkbox')
          control.checked = numeric >= .5;
        else
          control.value = numeric;
      }
    }
    renderModuleGraphics();
    return;
  }
  if (text.startsWith('mod:')) {
    const [modulationText, scopeText] = text.split('|scope:');
    const scope = document.querySelector('#scope');
    const next = modulationText.slice(4).split(',').map(Number);
    const keys = [
      'lfoPhase', 'lfoValue', 'adsrValue', 'adsrActive', 'adsrStage',
      'adsrStageProgress', 'adsrVelocityScale', 'lfo2Phase', 'lfo2Value',
      'adsr2Value', 'adsr2Active', 'adsr2Stage', 'adsr2StageProgress',
      'osc1Shape', 'osc2Shape', 'filterCutoff', 'filterResonance'
    ];
    modulation = Object.fromEntries(keys.map((key, index) => [key, next[index]]));
    if (scopeText)
      scope.setSamples(Float32Array.from(scopeText.split(','), Number));
    renderModuleGraphics();
    return;
  }
});

renderModuleGraphics();
send('ready');
