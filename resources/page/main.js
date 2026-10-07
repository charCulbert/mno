import './compost/components/compost-knob.js';
import './compost/components/compost-scope.js';
import './compost/components/compost-popup.js';

import {sendMessage, onMessage} from './lib/messages.js';

const parameters = new Map();
const parametersByID = new Map();
const values = new Map();
const svgNamespace = 'http://www.w3.org/2000/svg';

const pageNames = {
  osc1: 'Oscillator 1', osc2: 'Oscillator 2', filter: 'Filter', adsr1: 'Envelope 1',
  adsr2: 'Envelope 2', lfo1: 'LFO 1', lfo2: 'LFO 2', keyboard: 'Keyboard'
};
const pageParameters = {
  osc1: ['osc1Shape', 'osc1Tune', 'osc1Level', 'osc1PWMRate', 'osc1PWMDepth'],
  osc2: ['osc2Shape', 'osc2Tune', 'osc2Level', 'osc2PWMRate', 'osc2PWMDepth'],
  filter: ['cutoff', 'resonance'],
  adsr1: ['velocitySensitivity', 'attack', 'decay', 'sustain', 'release'],
  adsr2: ['adsr2Attack', 'adsr2Decay', 'adsr2Sustain', 'adsr2Release'],
  lfo1: ['lfoRate', 'lfoDepth'],
  lfo2: ['lfo2Rate', 'lfo2Depth'],
  keyboard: ['glideTime', 'glideShape', 'glideTimeRateMorph', 'pitchBendRange']
};

// The modulation sources, and the knobs they can reach: each route is
// <source prefix><destination>, e.g. lfo2ToCutoff.
const sources = { adsr1: 'adsrTo', adsr2: 'adsr2To', lfo1: 'lfoTo', lfo2: 'lfo2To' };
const destinations = {
  osc1Tune: 'Osc1Tune', osc1Shape: 'Osc1Shape', osc1Level: 'Osc1Level',
  osc2Tune: 'Osc2Tune', osc2Shape: 'Osc2Shape', osc2Level: 'Osc2Level',
  cutoff: 'Cutoff', resonance: 'Resonance'
};
const route = (source, destination) => sources[source] + destinations[destination];

let page = 'osc1';
let mapping = null; // the source whose routes the knobs edit, or null
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
  sendMessage({type: 'value', id: Number(spec.id), value: Number(next)});
  renderModuleGraphics();
}

function knob(identifier, label = null) {
  const spec = parameter(identifier);
  if (!spec) return document.createElement('span');
  const control = document.createElement('compost-knob');
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

// A styled dropdown: a button that opens a compost-popup menu. Exposes
// `value` and fires `change` like the native select it replaces, so the
// parameter plumbing below does not care which one it is talking to.
class MnoSelect extends HTMLElement {
  #options = [];
  #value = '';
  #button;
  #popup;

  connectedCallback() {
    if (this.#button) return;
    this.#button = document.createElement('button');
    this.#button.type = 'button';
    this.#button.className = 'mno-select-button';
    this.#button.setAttribute('aria-haspopup', 'menu');
    if (this.hasAttribute('aria-label'))
      this.#button.setAttribute('aria-label', this.getAttribute('aria-label'));
    this.#popup = document.createElement('compost-popup');
    if (this.hasAttribute('aria-label'))
      this.#popup.setAttribute('label', this.getAttribute('aria-label'));
    this.#popup.addEventListener('popup-select', ({ detail }) => {
      if (detail.value === this.#value) return;
      this.value = detail.value;
      this.dispatchEvent(new Event('change', { bubbles: true }));
    });
    this.#button.addEventListener('click', () => this.#popup.open({ anchor: this.#button }));
    this.append(this.#button, this.#popup);
    this.#render();
  }

  setOptions(labels) {
    this.#options = labels.map((label, index) => ({ value: String(index), label }));
    this.#render();
  }

  get value() { return this.#value; }
  set value(next) {
    this.#value = String(Math.round(Number(next)));
    this.#render();
  }

  #render() {
    if (!this.#button) return;
    this.#popup.setItems(this.#options.map((option) => ({ ...option, selected: option.value === this.#value })));
    this.#popup.value = this.#value;
    const current = this.#options.find((option) => option.value === this.#value);
    this.#button.textContent = current ? current.label : '';
  }
}
customElements.define('mno-select', MnoSelect);

function select(identifier, label, options) {
  const spec = parameter(identifier);
  const control = document.createElement('mno-select');
  control.setAttribute('aria-label', label);
  control.setAttribute('parameter-id', spec.id);
  control.setOptions(options);
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

// A knob a source can modulate: rings for its routes, or, while mapping, the
// mapping source's route amount in that source's colour.
function destinationKnob(identifier) {
  const wrapper = document.createElement('div');
  wrapper.className = 'mod-knob';
  wrapper.dataset.destination = identifier;
  const control = mapping ? knob(route(mapping, identifier), parameter(identifier).name) : knob(identifier);
  if (mapping) control.classList.add('route');
  wrapper.append(control, svgElement('svg', { class: 'mod-rings', 'aria-hidden': 'true' }));
  return wrapper;
}

function ringPath(radius, amount) {
  const end = clamp(amount / 100, -1, 1) * 135 * Math.PI / 180;
  const visible = Math.sign(end || 1) * Math.max(Math.abs(end), .05);
  const point = (angle) => ({ x: Math.sin(angle) * radius, y: -Math.cos(angle) * radius });
  const tip = point(visible);
  return { d: `M 0 ${-radius} A ${radius} ${radius} 0 ${Math.abs(visible) > Math.PI ? 1 : 0} ${visible > 0 ? 1 : 0} ${tip.x.toFixed(2)} ${tip.y.toFixed(2)}`, tip };
}

function renderRings() {
  for (const wrapper of document.querySelectorAll('.mod-knob')) {
    const svg = wrapper.querySelector('.mod-rings');
    const dial = wrapper.querySelector('compost-knob')?.shadowRoot?.querySelector('.dial');
    if (!svg || !dial) continue;
    const box = dial.getBoundingClientRect(), outer = wrapper.getBoundingClientRect();
    const radius = box.width / 2;
    const size = radius * 2 + 30;
    svg.setAttribute('width', size);
    svg.setAttribute('height', size);
    svg.setAttribute('viewBox', `${-size / 2} ${-size / 2} ${size} ${size}`);
    svg.style.left = `${box.left - outer.left + radius - size / 2}px`;
    svg.style.top = `${box.top - outer.top + radius - size / 2}px`;
    const rings = [];
    if (mapping) {
      // The route's amount, from 12 o'clock, in place of the knob's own fill.
      const amount = value(route(mapping, wrapper.dataset.destination));
      if (Math.abs(amount) >= .0001) {
        const group = svgElement('g', { class: 'mod-ring route-arc', 'data-source': mapping });
        group.append(svgElement('path', { d: ringPath(radius - 3.5, amount).d }));
        rings.push(group);
      }
      svg.replaceChildren(...rings);
      continue;
    }
    Object.keys(sources).forEach((source, index) => {
      const amount = value(route(source, wrapper.dataset.destination));
      if (Math.abs(amount) < .0001) return;
      const { d, tip } = ringPath(radius + 3 + index * 2.5, amount);
      const group = svgElement('g', { class: 'mod-ring', 'data-source': source });
      group.append(svgElement('path', { d }), svgElement('circle', { cx: tip.x, cy: tip.y, r: 1.8 }));
      rings.push(group);
    });
    svg.replaceChildren(...rings);
  }
}

function setMapping(next) {
  mapping = next;
  document.querySelector('.rail').classList.toggle('mapping', Boolean(mapping));
  for (const button of document.querySelectorAll('.module'))
    button.classList.toggle('mapping', Boolean(mapping) && button.dataset.source === mapping);
  renderPage();
}

function renderPage() {
  document.querySelector('#page-title').textContent = mapping
    ? `${pageNames[page]} · ${pageNames[mapping]} amounts` : pageNames[page];
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
  } else if (page === 'lfo1' || page === 'lfo2') {
    header.append(select(page === 'lfo1' ? 'lfoWaveform' : 'lfo2Waveform',
      `${pageNames[page]} waveform`, ['Sine', 'Triangle', 'Square']));
  } else if (page === 'keyboard') {
    header.append(toggle('legato', 'LEGATO'));
  }

  controls.className = mapping ? `knobs mapping-${mapping}` : 'knobs';
  for (const identifier of pageParameters[page])
    controls.append(identifier in destinations ? destinationKnob(identifier) : knob(identifier));
  requestAnimationFrame(renderRings);
}

function updateRail() {
  for (const button of document.querySelectorAll('.module')) {
    const isCurrent = button.dataset.page === page;
    if (isCurrent) button.setAttribute('aria-current', 'page');
    else button.removeAttribute('aria-current');
  }
}

// A click opens a module's page. On a source, a hold (or right-click) toggles
// mapping, and a drag onto a knob it can modulate adds that route.
for (const button of document.querySelectorAll('.module')) {
  let suppressClick = false;
  button.addEventListener('click', () => {
    if (suppressClick) { suppressClick = false; return; }
    if (button.dataset.source && button.dataset.source === mapping) { setMapping(null); return; }
    page = button.dataset.page;
    updateRail();
    if (mapping) setMapping(null); else renderPage();
  });
  const source = button.dataset.source;
  if (!source) continue;
  const toggleMapping = () => setMapping(mapping === source ? null : source);
  button.addEventListener('contextmenu', (event) => { event.preventDefault(); toggleMapping(); });

  button.addEventListener('pointerdown', (event) => {
    if (event.button !== 0) return;
    const start = { x: event.clientX, y: event.clientY };
    let ghost = null, held = false;
    const hold = setTimeout(() => { held = true; toggleMapping(); }, 380);
    const targetAt = (x, y) => document.elementFromPoint(x, y)?.closest('.mod-knob');
    const move = (e) => {
      if (!ghost && Math.hypot(e.clientX - start.x, e.clientY - start.y) > 6 && !held) {
        clearTimeout(hold);
        ghost = document.createElement('div');
        ghost.className = 'drag-ghost';
        ghost.style.setProperty('--src', getComputedStyle(button).getPropertyValue('--src'));
        document.body.append(ghost);
        for (const wrapper of document.querySelectorAll('.mod-knob')) wrapper.classList.add('drop-target');
        document.querySelector('#knobs').classList.add(`mapping-${source}`);
      }
      if (ghost) { ghost.style.left = `${e.clientX}px`; ghost.style.top = `${e.clientY}px`; }
    };
    const up = (e) => {
      clearTimeout(hold);
      removeEventListener('pointermove', move);
      removeEventListener('pointerup', up);
      removeEventListener('pointercancel', up);
      if (held) suppressClick = true;
      if (!ghost) return;
      suppressClick = true;
      ghost.remove();
      const target = e.type === 'pointerup' ? targetAt(e.clientX, e.clientY) : null;
      if (target) {
        const id = route(source, target.dataset.destination);
        if (Math.abs(value(id)) < .0001) {
          const spec = parameter(id);
          sendMessage({type: 'begin', id: Number(spec.id)});
          edit(id, 50);
          sendMessage({type: 'end', id: Number(spec.id)});
        }
        setMapping(source);
      } else renderPage();
    };
    addEventListener('pointermove', move);
    addEventListener('pointerup', up);
    addEventListener('pointercancel', up);
  });
}

// Clicking the page's background, a header control, or Escape ends mapping.
document.querySelector('.page').addEventListener('click', (event) => {
  if (mapping && !event.target.closest('.mod-knob')) setMapping(null);
});
addEventListener('keydown', (event) => { if (event.key === 'Escape' && mapping) setMapping(null); });
addEventListener('resize', () => renderRings());

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
    active ? { x: pointX, y: 25.2 - current * 21.84 } : null);
}

function renderModuleGraphics() {
  renderOscillator('osc1Waveform', 'osc1Shape', 'osc1');
  renderOscillator('osc2Waveform', 'osc2Shape', 'osc2');
  renderFilter();
  renderEnvelope(1);
  renderEnvelope(2);
  renderLFO(1);
  renderLFO(2);
}

addEventListener('parameter-begin', ({ detail }) => sendMessage({type: 'begin', id: Number(detail.parameterID)}));
addEventListener('parameter-edit', ({ detail }) => {
  const spec = parametersByID.get(String(detail.parameterID));
  if (spec) values.set(spec.identifier, Number(detail.value));
  sendMessage({type: 'value', id: Number(detail.parameterID), value: Number(detail.value)});
  renderModuleGraphics();
  renderRings();
});
addEventListener('parameter-end', ({ detail }) => {
  // Escape cancels a gesture: compost resets the control to its start value and
  // reports it only here, so resend it or the plugin keeps the last dragged value.
  if (detail.cancelled) sendMessage({type: 'value', id: Number(detail.parameterID), value: Number(detail.value)});
  sendMessage({type: 'end', id: Number(detail.parameterID)});
});

const modulationKeys = [
  'lfoPhase', 'lfoValue', 'adsrValue', 'adsrActive', 'adsrStage',
  'adsrStageProgress', 'adsrVelocityScale', 'lfo2Phase', 'lfo2Value',
  'adsr2Value', 'adsr2Active', 'adsr2Stage', 'adsr2StageProgress',
  'osc1Shape', 'osc2Shape', 'filterCutoff', 'filterResonance'
];

onMessage((message) => {
  if (message.type === 'metadata') {
    for (const p of message.parameters) {
      const spec = {
        id: String(p.id), identifier: p.identifier, moduleName: p.module, name: p.name,
        min: p.min, max: p.max, defaultValue: p.initial, step: p.step, mid: p.mid, hasMid: p.hasMid,
        curve: p.curve, options: p.options.join('|'), unit: p.unit
      };
      parameters.set(spec.identifier, spec);
      parametersByID.set(spec.id, spec);
    }
    updateRail();
    renderPage();
    renderModuleGraphics();
  } else if (message.type === 'values') {
    message.values.forEach((numeric, index) => {
      const spec = parametersByID.get(String(index));
      if (!spec) return;
      values.set(spec.identifier, numeric);
      for (const control of document.querySelectorAll(`[parameter-id="${spec.id}"]`)) {
        if (control instanceof HTMLInputElement && control.type === 'checkbox')
          control.checked = numeric >= .5;
        else
          control.value = numeric;
      }
    });
    renderModuleGraphics();
    renderRings();
  } else if (message.type === 'visual') {
    modulation = Object.fromEntries(modulationKeys.map((key, index) => [key, message.modulation[index]]));
    if (message.scope.length)
      document.querySelector('#scope').setSamples(Float32Array.from(message.scope));
    renderModuleGraphics();
  }
});

// The modulation and scope are pulled on the animation clock, while the page is visible.
const pull = () => {
  requestAnimationFrame(pull);
  if (!document.hidden) sendMessage({type: 'visual'});
};

renderModuleGraphics();
sendMessage({type: 'ready'});
requestAnimationFrame(pull);
