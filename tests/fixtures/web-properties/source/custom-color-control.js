// Presentation-only color editing. The caller owns committing the final HEX value.
const clamp = (value, max = 1) => Math.max(0, Math.min(max, value));
const wrapHue = value => ((value % 360) + 360) % 360;

export function parseColorHex(value) {
  const hex = String(value).trim().replace(/^#/, '');
  if (/^[\da-f]{3}$/i.test(hex)) return `#${[...hex].map(c => c + c).join('')}`.toLowerCase();
  return /^[\da-f]{6}$/i.test(hex) ? `#${hex.toLowerCase()}` : null;
}

export function rgbToHex(rgb) {
  return `#${rgb.map(channel => Math.round(clamp(channel, 255)).toString(16).padStart(2, '0')).join('')}`;
}

export function hexToRgb(hex) {
  return [1, 3, 5].map(index => Number.parseInt(hex.slice(index, index + 2), 16));
}

export function rgbToHsv(rgb, previousHue = 0) {
  const [r, g, b] = rgb.map(channel => channel / 255);
  const max = Math.max(r, g, b), min = Math.min(r, g, b), delta = max - min;
  const hue = !delta ? previousHue : max === r ? 60 * ((g - b) / delta)
    : max === g ? 60 * (2 + (b - r) / delta) : 60 * (4 + (r - g) / delta);
  return [wrapHue(hue), max ? delta / max : 0, max];
}

export function hsvToRgb([h, s, v]) {
  const c = v * s, x = c * (1 - Math.abs((wrapHue(h) / 60) % 2 - 1)), m = v - c;
  const sector = Math.floor(wrapHue(h) / 60);
  return [[c, x, 0], [x, c, 0], [0, c, x], [0, x, c], [x, 0, c], [c, 0, x]][sector]
    .map(channel => Math.round((channel + m) * 255));
}

export function rgbToHsl(rgb) {
  const [h, s, v] = rgbToHsv(rgb);
  const l = v * (1 - s / 2);
  return [h, !l || l === 1 ? 0 : (v - l) / Math.min(l, 1 - l) * 100, l * 100];
}

export function hslToRgb([h, s, l]) {
  s /= 100; l /= 100;
  const v = l + s * Math.min(l, 1 - l);
  return hsvToRgb([h, v ? 2 * (1 - l / v) : 0, v]);
}

export function createCustomColorControl({ documentRef = document, onApply, onCancel }) {
  const element = documentRef.createElement('div');
  element.className = 'ui-custom-color';
  element.innerHTML = `
    <div class="ui-custom-color-heading"><strong>사용자 지정</strong>
      <button type="button" class="ui-button icon-btn" data-custom-cancel aria-label="색상 선택 취소"><svg class="ui-icon" aria-hidden="true"><use href="#icon-close"/></svg></button>
    </div>
    <button type="button" class="ui-custom-color-plane" aria-label="채도와 명도. 좌우 방향키는 채도, 위아래 방향키는 명도 조절">
      <span class="ui-custom-color-cursor" aria-hidden="true"></span>
    </button>
    <label class="ui-custom-color-hue"><span class="ui-visually-hidden">색조</span><input type="range" min="0" max="360" step="1" value="0" aria-label="색조" /></label>
    <div class="ui-custom-color-value-row">
      <span class="ui-custom-color-preview" role="img" aria-label="선택한 색상"></span>
      <label class="ui-field ui-custom-color-hex"><span>HEX</span><input type="text" maxlength="7" spellcheck="false" autocomplete="off" aria-label="HEX 색상" /></label>
      <button type="button" class="ui-button btn ghost" data-custom-eyedropper title="화면에서 색상 추출">스포이트</button>
    </div>
    <div class="ui-custom-color-formats" role="group" aria-label="색상 값 형식">
      <button type="button" class="ui-button" data-custom-format="rgb" aria-pressed="true">RGB</button>
      <button type="button" class="ui-button" data-custom-format="hsl" aria-pressed="false">HSL</button>
    </div>
    <div class="ui-custom-color-channels">${['R', 'G', 'B'].map(name => `<label class="ui-field"><span>${name}</span><input type="number" min="0" max="255" step="1" aria-label="${name}" /></label>`).join('')}</div>
    <p class="ui-custom-color-message hidden" role="status" aria-live="polite"></p>
    <div class="ui-custom-color-actions"><button type="button" class="ui-button btn ghost" data-custom-cancel>취소</button><button type="button" class="ui-button btn" data-custom-apply>적용</button></div>`;
  const plane = element.querySelector('.ui-custom-color-plane');
  const hue = element.querySelector('input[type="range"]');
  const hex = element.querySelector('input[type="text"]');
  const channels = [...element.querySelectorAll('input[type="number"]')];
  const preview = element.querySelector('.ui-custom-color-preview');
  const apply = element.querySelector('[data-custom-apply]');
  const eyedropper = element.querySelector('[data-custom-eyedropper]');
  const message = element.querySelector('.ui-custom-color-message');
  const view = documentRef.defaultView;
  let hsv = [0, 0, 0], color = '#000000', format = 'rgb', active = false, busy = false;
  let pointerId = null, eyeRequest = null;

  function showMessage(text = '') {
    message.textContent = text;
    message.classList.toggle('hidden', !text);
  }

  function render({ keepInput = null } = {}) {
    plane.style.setProperty('--color-hue', `hsl(${hsv[0]} 100% 50%)`);
    plane.style.setProperty('--color-x', `${hsv[1] * 100}%`);
    plane.style.setProperty('--color-y', `${(1 - hsv[2]) * 100}%`);
    plane.setAttribute('aria-label', `채도 ${Math.round(hsv[1] * 100)}%, 명도 ${Math.round(hsv[2] * 100)}%. 좌우 방향키는 채도, 위아래 방향키는 명도 조절`);
    hue.value = String(hsv[0]);
    hue.setAttribute('aria-valuetext', `${Math.round(hsv[0])}도`);
    preview.style.backgroundColor = color;
    preview.setAttribute('aria-label', `선택한 색상 ${color.toUpperCase()}`);
    if (keepInput !== hex) hex.value = color.toUpperCase();
    const values = format === 'rgb' ? hexToRgb(color) : rgbToHsl(hexToRgb(color));
    const names = format === 'rgb' ? ['R', 'G', 'B'] : ['H', 'S', 'L'];
    channels.forEach((input, index) => {
      input.max = String(format === 'rgb' ? 255 : index ? 100 : 360);
      input.previousElementSibling.textContent = `${names[index]}${format === 'hsl' ? index ? ' %' : ' °' : ''}`;
      input.setAttribute('aria-label', names[index]);
      // Preserve all explicitly entered channels: RGB conversion rounding must
      // not shift H/S/L while the user moves between the three number fields.
      if (!channels.includes(keepInput)) input.value = String(Math.round(values[index]));
    });
    element.querySelectorAll('[data-custom-format]').forEach(button => button.setAttribute('aria-pressed', String(button.dataset.customFormat === format)));
    apply.disabled = busy || !!element.querySelector('[aria-invalid="true"]');
  }

  function clearInvalid() {
    [hex, ...channels].forEach(input => input.removeAttribute('aria-invalid'));
    showMessage();
  }

  function fromHex(value, keepInput = null) {
    color = value;
    hsv = rgbToHsv(hexToRgb(color), hsv[0]);
    render({ keepInput });
  }

  function fromHsv() {
    clearInvalid();
    color = rgbToHex(hsvToRgb(hsv));
    render();
  }

  function updatePoint(event) {
    const bounds = plane.getBoundingClientRect();
    if (!bounds.width || !bounds.height) return;
    hsv[1] = clamp((event.clientX - bounds.left) / bounds.width);
    hsv[2] = 1 - clamp((event.clientY - bounds.top) / bounds.height);
    fromHsv();
  }

  function releasePointer() {
    if (pointerId !== null && plane.hasPointerCapture(pointerId)) plane.releasePointerCapture(pointerId);
    pointerId = null;
  }

  plane.addEventListener('pointerdown', event => {
    if (!active || busy || pointerId !== null || !event.isPrimary || event.button !== 0) return;
    event.preventDefault();
    plane.focus({ preventScroll: true });
    pointerId = event.pointerId;
    plane.setPointerCapture(pointerId);
    updatePoint(event);
  });
  plane.addEventListener('pointermove', event => { if (event.pointerId === pointerId) updatePoint(event); });
  plane.addEventListener('pointerup', event => { if (event.pointerId === pointerId) { updatePoint(event); releasePointer(); } });
  plane.addEventListener('pointercancel', releasePointer);
  plane.addEventListener('lostpointercapture', () => { pointerId = null; });
  plane.addEventListener('keydown', event => {
    if (!event.key.startsWith('Arrow')) return;
    event.preventDefault();
    const step = event.shiftKey ? 0.1 : 0.01;
    if (event.key === 'ArrowLeft') hsv[1] = clamp(hsv[1] - step);
    if (event.key === 'ArrowRight') hsv[1] = clamp(hsv[1] + step);
    if (event.key === 'ArrowDown') hsv[2] = clamp(hsv[2] - step);
    if (event.key === 'ArrowUp') hsv[2] = clamp(hsv[2] + step);
    fromHsv();
  });
  hue.addEventListener('input', () => { hsv[0] = Number(hue.value); fromHsv(); });
  hex.addEventListener('input', () => {
    const value = parseColorHex(hex.value);
    hex.setAttribute('aria-invalid', String(!value));
    showMessage(value ? '' : 'HEX는 3자리 또는 6자리로 입력하세요.');
    if (value) { clearInvalid(); fromHex(value, hex); }
    else apply.disabled = true;
  });
  channels.forEach(input => input.addEventListener('input', () => {
    const valid = channels.every(field => field.value.trim() && field.checkValidity());
    channels.forEach(field => field.setAttribute('aria-invalid', String(!field.value.trim() || !field.checkValidity())));
    showMessage(valid ? '' : '표시된 범위 안의 숫자를 입력하세요.');
    if (!valid) { apply.disabled = true; return; }
    clearInvalid();
    const values = channels.map(field => Number(field.value));
    if (format === 'hsl') hsv[0] = values[0];
    fromHex(rgbToHex(format === 'rgb' ? values : hslToRgb(values)), input);
  }));
  element.querySelectorAll('[data-custom-format]').forEach(button => button.addEventListener('click', () => {
    format = button.dataset.customFormat;
    clearInvalid(); render();
  }));
  element.querySelectorAll('[data-custom-cancel]').forEach(button => button.addEventListener('click', onCancel));
  apply.addEventListener('click', async () => {
    if (!active || busy || apply.disabled || pointerId !== null) return;
    busy = true; apply.disabled = true;
    try { await onApply(color); }
    catch (error) { if (active) showMessage('색상을 적용하지 못했습니다. 다시 시도하세요.'); }
    finally { busy = false; if (active) render(); }
  });
  eyedropper.hidden = !view.EyeDropper;
  eyedropper.classList.toggle('hidden', eyedropper.hidden);
  eyedropper.addEventListener('click', async () => {
    if (!active || eyeRequest) return;
    const request = new view.AbortController();
    eyeRequest = request;
    eyedropper.disabled = true;
    try {
      const result = await new view.EyeDropper().open({ signal: request.signal });
      if (active && eyeRequest === request) { clearInvalid(); fromHex(parseColorHex(result.sRGBHex)); }
    } catch (error) {
      if (active && eyeRequest === request && error.name !== 'AbortError') showMessage('색상을 가져오지 못했습니다. 다시 시도하세요.');
    } finally {
      if (eyeRequest === request) { eyeRequest = null; eyedropper.disabled = false; }
    }
  });
  // Keep map shortcuts and the enclosing dialog's Escape out of color editing.
  element.addEventListener('keydown', event => {
    event.stopPropagation();
    if (event.key === 'Escape') { event.preventDefault(); onCancel(); }
    if (event.key === 'Enter' && event.target.matches('input:not([type="range"])')) {
      event.preventDefault(); apply.click();
    }
    if (event.key === 'Tab') {
      const items = [...element.querySelectorAll('button, input')].filter(item => !item.disabled && !item.hidden);
      const first = items[0], last = items.at(-1);
      if (event.shiftKey && event.target === first) { event.preventDefault(); last.focus(); }
      if (!event.shiftKey && event.target === last) { event.preventDefault(); first.focus(); }
    }
  });

  return Object.freeze({
    element,
    open(value) {
      active = true; busy = false; clearInvalid();
      hsv = [0, 0, 0];
      fromHex(parseColorHex(value) || '#000000');
      hex.focus({ preventScroll: true });
    },
    close() {
      active = false;
      releasePointer();
      eyeRequest?.abort(); eyeRequest = null;
      eyedropper.disabled = false;
    },
  });
}
