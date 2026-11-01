/* omm.js - the JavaScript side of omm: loads the C core, turns .omm source into
 * meshes, draws them with WebGL, and provides the <omm-model> element.
 *
 * In the built bundle (dist/omm.js) the Emscripten factory `OmmCore` is defined
 * right above this file, so the bundle is one classic <script>.
 */
(function (root) {
  'use strict';

  var VERSION = '0.1.0';
  var MAX_SOURCE = 200000;   /* characters; the source is copied onto the wasm2js stack */

  /* ------------------------------------------------------------------ core */

  var corePromise = null;

  function loadCore() {
    if (!corePromise) {
      if (typeof OmmCore !== 'function') {
        return Promise.reject(new Error('OmmCore is not defined: load omm-core.js before omm.js'));
      }
      corePromise = Promise.resolve(OmmCore()).then(function (M) {
        return {
          M: M,
          parse:  M.cwrap('omm_parse', 'number', ['string']),
          error:  M.cwrap('omm_last_error', 'string', []),
          free:   M.cwrap('omm_free', null, ['number']),
          count:  M.cwrap('omm_model_count', 'number', ['number']),
          build:  M.cwrap('omm_build', 'number', ['number', 'number', 'number']),
          nv:     M.cwrap('omm_vertex_count', 'number', ['number', 'number']),
          ni:     M.cwrap('omm_index_count', 'number', ['number', 'number']),
          pos:    M.cwrap('omm_positions', 'number', ['number', 'number']),
          nor:    M.cwrap('omm_normals', 'number', ['number', 'number']),
          idx:    M.cwrap('omm_indices', 'number', ['number', 'number']),
          bounds: M.cwrap('omm_bounds', 'number', ['number', 'number'])
        };
      });
    }
    return corePromise;
  }

  /* Parse .omm source and build one mesh per model.
   * Resolves to [{ positions: Float32Array, normals: Float32Array,
   *                indices: Uint32Array, bounds: [minx,miny,minz,maxx,maxy,maxz] }, ...]
   * Rejects with an Error whose message says what is wrong and on which line. */
  function compile(source, opts) {
    var res = Math.round(Number(opts && opts.resolution) || 64);
    res = Math.max(1, Math.min(512, res));
    source = String(source == null ? '' : source);
    if (source.length > MAX_SOURCE) {
      return Promise.reject(new Error('source is too large (max ' + MAX_SOURCE + ' characters)'));
    }
    return loadCore().then(function (c) {
      var scene = c.parse(source), models = [], i, n;
      if (!scene) throw new Error(c.error() || 'could not parse the source');
      try {
        n = c.count(scene);
        for (i = 0; i < n; i++) {
          if (c.build(scene, i, res) !== 0) throw new Error(c.error() || 'could not build the mesh');
          /* The heap views are replaced when memory grows, so read them from M every time
           * and copy straight away: the pointers die with the next omm_build/omm_free. */
          var nv = c.nv(scene, i), ni = c.ni(scene, i);
          var p = c.pos(scene, i) >>> 2, q = c.nor(scene, i) >>> 2;
          var r = c.idx(scene, i) >>> 2, b = c.bounds(scene, i) >>> 2;
          models.push({
            positions: c.M.HEAPF32.slice(p, p + nv * 3),
            normals:   c.M.HEAPF32.slice(q, q + nv * 3),
            indices:   c.M.HEAPU32.slice(r, r + ni),
            bounds:    Array.prototype.slice.call(c.M.HEAPF32.subarray(b, b + 6))
          });
        }
      } finally {
        c.free(scene);
      }
      return models;
    });
  }

  /* ---------------------------------------------------------------- matrices */

  function perspective(fovy, aspect, near, far) {
    var f = 1 / Math.tan(fovy / 2), nf = 1 / (near - far);
    return new Float32Array([
      f / aspect, 0, 0, 0,
      0, f, 0, 0,
      0, 0, (far + near) * nf, -1,
      0, 0, 2 * far * near * nf, 0
    ]);
  }

  function lookAt(eye, c, up) {
    var zx = eye[0] - c[0], zy = eye[1] - c[1], zz = eye[2] - c[2];
    var l = Math.sqrt(zx * zx + zy * zy + zz * zz) || 1;
    zx /= l; zy /= l; zz /= l;
    var xx = up[1] * zz - up[2] * zy, xy = up[2] * zx - up[0] * zz, xz = up[0] * zy - up[1] * zx;
    l = Math.sqrt(xx * xx + xy * xy + xz * xz) || 1;
    xx /= l; xy /= l; xz /= l;
    var yx = zy * xz - zz * xy, yy = zz * xx - zx * xz, yz = zx * xy - zy * xx;
    return new Float32Array([
      xx, yx, zx, 0,
      xy, yy, zy, 0,
      xz, yz, zz, 0,
      -(xx * eye[0] + xy * eye[1] + xz * eye[2]),
      -(yx * eye[0] + yy * eye[1] + yz * eye[2]),
      -(zx * eye[0] + zy * eye[1] + zz * eye[2]),
      1
    ]);
  }

  /* ------------------------------------------------------------------ viewer */

  var VERT = [
    'attribute vec3 aPos;',
    'attribute vec3 aNor;',
    'uniform mat4 uProj;',
    'uniform mat4 uView;',
    'varying vec3 vNor;',
    'void main() {',
    '  vNor = (uView * vec4(aNor, 0.0)).xyz;',
    '  gl_Position = uProj * uView * vec4(aPos, 1.0);',
    '}'
  ].join('\n');

  var FRAG = [
    'precision mediump float;',
    'varying vec3 vNor;',
    'uniform vec3 uColor;',
    'void main() {',
    '  vec3 n = normalize(vNor);',
    '  if (!gl_FrontFacing) n = -n;',            /* two-sided: both faces of a surface are lit */
    '  vec3 l = normalize(vec3(0.35, 0.55, 0.75));',
    '  float diff = max(dot(n, l), 0.0);',
    '  float spec = pow(max(dot(n, normalize(l + vec3(0.0, 0.0, 1.0))), 0.0), 40.0);',
    '  gl_FragColor = vec4(uColor * (0.28 + 0.72 * diff) + vec3(0.22 * spec), 1.0);',
    '}'
  ].join('\n');

  var PALETTE = [
    [0.25, 0.58, 0.93], [0.95, 0.52, 0.22], [0.30, 0.75, 0.47],
    [0.86, 0.36, 0.60], [0.60, 0.48, 0.92], [0.88, 0.74, 0.25]
  ];
  var FOVY = Math.PI / 4;

  function compileShader(gl, type, src) {
    var s = gl.createShader(type);
    gl.shaderSource(s, src);
    gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
      var msg = gl.getShaderInfoLog(s);
      gl.deleteShader(s);
      throw new Error('shader error: ' + msg);
    }
    return s;
  }

  function linkProgram(gl) {
    var vs = compileShader(gl, gl.VERTEX_SHADER, VERT);
    var fs = compileShader(gl, gl.FRAGMENT_SHADER, FRAG);
    var p = gl.createProgram();
    gl.attachShader(p, vs);
    gl.attachShader(p, fs);
    gl.linkProgram(p);
    gl.deleteShader(vs);
    gl.deleteShader(fs);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error('link error: ' + gl.getProgramInfoLog(p));
    return p;
  }

  /* A WebGL orbit viewer for the models returned by compile().
   * Drag to rotate, wheel or pinch to zoom, double-click to reset. z is up. */
  function View(canvas, models) {
    var self = this, ctxOpts = { antialias: true, alpha: true, premultipliedAlpha: true };
    var gl = canvas.getContext('webgl2', ctxOpts) || canvas.getContext('webgl', ctxOpts);
    if (!gl) throw new Error('WebGL is not available in this browser');
    var isGL2 = typeof WebGL2RenderingContext !== 'undefined' && gl instanceof WebGL2RenderingContext;
    if (!isGL2 && !gl.getExtension('OES_element_index_uint')) {
      throw new Error('this browser lacks WebGL2 and OES_element_index_uint');
    }
    this.canvas = canvas;
    this.gl = gl;
    this.models = models;
    this._raf = 0;
    this._disposed = false;
    this._frame(models);
    this.reset();
    this._init();

    canvas.addEventListener('webglcontextlost', function (e) { e.preventDefault(); });
    canvas.addEventListener('webglcontextrestored', function () { self._init(); self.requestRender(); });
    this._bindPointer();
    if (typeof ResizeObserver !== 'undefined') {
      this._ro = new ResizeObserver(function () { self.resize(); });
      this._ro.observe(canvas);
    } else {
      this._onWinResize = function () { self.resize(); };
      root.addEventListener('resize', this._onWinResize);
    }
    this.resize();
  }

  View.prototype._frame = function (models) {
    var lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity], i, k;
    for (i = 0; i < models.length; i++) {
      var b = models[i].bounds;
      for (k = 0; k < 3; k++) {
        if (b[k] < lo[k]) lo[k] = b[k];
        if (b[3 + k] > hi[k]) hi[k] = b[3 + k];
      }
    }
    if (!models.length) { lo = [-1, -1, -1]; hi = [1, 1, 1]; }
    this.center = [(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2];
    var dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
    this.radius = Math.sqrt(dx * dx + dy * dy + dz * dz) / 2 || 1;
  };

  View.prototype._init = function () {
    var gl = this.gl, prog = linkProgram(gl);
    this.prog = prog;
    this.loc = {
      aPos: gl.getAttribLocation(prog, 'aPos'), aNor: gl.getAttribLocation(prog, 'aNor'),
      uProj: gl.getUniformLocation(prog, 'uProj'), uView: gl.getUniformLocation(prog, 'uView'),
      uColor: gl.getUniformLocation(prog, 'uColor')
    };
    this.meshes = this.models.map(function (m, i) {
      function buf(target, data) {
        var b = gl.createBuffer();
        gl.bindBuffer(target, b);
        gl.bufferData(target, data, gl.STATIC_DRAW);
        return b;
      }
      return {
        pos: buf(gl.ARRAY_BUFFER, m.positions),
        nor: buf(gl.ARRAY_BUFFER, m.normals),
        idx: buf(gl.ELEMENT_ARRAY_BUFFER, m.indices),
        count: m.indices.length,
        color: PALETTE[i % PALETTE.length]
      };
    });
  };

  View.prototype.reset = function () {
    this.yaw = -Math.PI / 3;
    this.pitch = Math.PI / 7;
    this.zoom = 1;
    this.requestRender();
  };

  View.prototype._bindPointer = function () {
    var self = this, c = this.canvas, pts = {};
    function count() { return Object.keys(pts).length; }
    function span() {
      var k = Object.keys(pts), a = pts[k[0]], b = pts[k[1]];
      return Math.sqrt(Math.pow(a.x - b.x, 2) + Math.pow(a.y - b.y, 2)) || 1;
    }
    function clampZoom() { self.zoom = Math.max(0.15, Math.min(6, self.zoom)); }

    c.addEventListener('pointerdown', function (e) {
      try { c.setPointerCapture(e.pointerId); } catch (err) { /* synthetic events */ }
      pts[e.pointerId] = { x: e.clientX, y: e.clientY };
    });
    c.addEventListener('pointermove', function (e) {
      var p = pts[e.pointerId], before;
      if (!p) return;
      if (count() === 1) {
        self.yaw -= (e.clientX - p.x) * 0.008;
        self.pitch += (e.clientY - p.y) * 0.008;
        self.pitch = Math.max(-1.5, Math.min(1.5, self.pitch));
      } else if (count() === 2) {
        before = span();
        p.x = e.clientX; p.y = e.clientY;
        self.zoom *= before / span();
        clampZoom();
      }
      p.x = e.clientX; p.y = e.clientY;
      self.requestRender();
    });
    function up(e) { delete pts[e.pointerId]; }
    c.addEventListener('pointerup', up);
    c.addEventListener('pointercancel', up);
    c.addEventListener('wheel', function (e) {
      e.preventDefault();
      self.zoom *= Math.exp(e.deltaY * 0.0015);
      clampZoom();
      self.requestRender();
    }, { passive: false });
    c.addEventListener('dblclick', function () { self.reset(); });
  };

  View.prototype.resize = function () {
    if (this._disposed) return;
    var c = this.canvas, dpr = Math.min(root.devicePixelRatio || 1, 2);
    var w = Math.max(1, Math.round(c.clientWidth * dpr)), h = Math.max(1, Math.round(c.clientHeight * dpr));
    if (c.width !== w || c.height !== h) { c.width = w; c.height = h; }
    this.requestRender();
  };

  View.prototype.requestRender = function () {
    var self = this;
    if (this._raf || this._disposed) return;
    this._raf = root.requestAnimationFrame(function () { self._raf = 0; self.draw(); });
  };

  View.prototype.draw = function () {
    var gl = this.gl, c = this.canvas, L = this.loc, i;
    if (this._disposed || gl.isContextLost()) return;

    var aspect = c.width / c.height;
    var fovx = 2 * Math.atan(Math.tan(FOVY / 2) * aspect);
    var fit = this.radius / Math.sin(Math.min(FOVY, fovx) / 2) * 1.05;   /* distance that frames the scene */
    var dist = fit * this.zoom;
    var cp = Math.cos(this.pitch);
    var eye = [
      this.center[0] + dist * cp * Math.cos(this.yaw),
      this.center[1] + dist * cp * Math.sin(this.yaw),
      this.center[2] + dist * Math.sin(this.pitch)
    ];

    gl.viewport(0, 0, c.width, c.height);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.enable(gl.DEPTH_TEST);
    gl.useProgram(this.prog);
    gl.uniformMatrix4fv(L.uProj, false,
      perspective(FOVY, aspect, Math.max(dist * 0.02, this.radius * 1e-3), dist + this.radius * 2.5));
    gl.uniformMatrix4fv(L.uView, false, lookAt(eye, this.center, [0, 0, 1]));
    gl.enableVertexAttribArray(L.aPos);
    gl.enableVertexAttribArray(L.aNor);

    for (i = 0; i < this.meshes.length; i++) {
      var m = this.meshes[i];
      if (!m.count) continue;
      gl.bindBuffer(gl.ARRAY_BUFFER, m.pos);
      gl.vertexAttribPointer(L.aPos, 3, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ARRAY_BUFFER, m.nor);
      gl.vertexAttribPointer(L.aNor, 3, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, m.idx);
      gl.uniform3fv(L.uColor, m.color);
      gl.drawElements(gl.TRIANGLES, m.count, gl.UNSIGNED_INT, 0);
    }
  };

  View.prototype.dispose = function () {
    var gl = this.gl;
    if (this._disposed) return;
    this._disposed = true;
    if (this._raf) root.cancelAnimationFrame(this._raf);
    if (this._ro) this._ro.disconnect();
    if (this._onWinResize) root.removeEventListener('resize', this._onWinResize);
    if (!gl.isContextLost()) {
      this.meshes.forEach(function (m) { gl.deleteBuffer(m.pos); gl.deleteBuffer(m.nor); gl.deleteBuffer(m.idx); });
      gl.deleteProgram(this.prog);
    }
  };

  /* Put a canvas into `container`, compile `source`, show it. Resolves to the View. */
  function mount(container, source, opts) {
    return compile(source, opts).then(function (models) {
      var canvas = document.createElement('canvas');
      canvas.style.cssText = 'display:block;width:100%;height:100%;touch-action:none';
      container.appendChild(canvas);
      return new View(canvas, models);
    });
  }

  /* ------------------------------------------------------------ <omm-model> */

  var STYLE = [
    ':host{display:block;position:relative;height:360px;min-height:160px}',
    'canvas{display:block;width:100%;height:100%;touch-action:none;cursor:grab;outline:none}',
    'canvas:active{cursor:grabbing}',
    '.msg{position:absolute;left:8px;right:8px;bottom:8px;margin:0;padding:8px 10px;border-radius:6px;',
    'font:12px/1.45 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;white-space:pre-wrap;',
    'color:#fff;background:rgba(176,0,32,.92)}',
    '.msg[hidden]{display:none}'
  ].join('');

  function defineElement() {
    if (typeof customElements === 'undefined' || typeof HTMLElement === 'undefined') return;
    if (customElements.get('omm-model')) return;

    class OmmModel extends HTMLElement {
      static get observedAttributes() { return ['src', 'resolution']; }

      constructor() {
        super();
        this._source = null;
        this._view = null;
        this._models = null;
        this._token = 0;
        this._ready = false;
      }

      /* formulas set from JS; wins over src and over an inline <script type="text/omm"> */
      get source() { return this._source; }
      set source(text) {
        this._source = text == null ? null : String(text);
        if (this._ready) this.reload();
      }

      get models() { return this._models; }
      get view() { return this._view; }

      connectedCallback() {
        var self = this;
        if (!this._root) {
          this._root = this.attachShadow({ mode: 'open' });
          this._root.innerHTML = '<style>' + STYLE + '</style><canvas part="canvas"></canvas><pre class="msg" part="error" hidden></pre>';
          this._msg = this._root.querySelector('.msg');
        }
        function start() { self._ready = true; self.reload(); }
        /* children (an inline <script>) are not parsed yet while the document is loading */
        if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
        else start();
      }

      disconnectedCallback() {
        this._token++;
        this._ready = false;
        this._dropView();
      }

      attributeChangedCallback(name, oldValue, newValue) {
        if (this._ready && oldValue !== newValue) this.reload();
      }

      _dropView() {
        if (this._view) { this._view.dispose(); this._view = null; }
      }

      _showError(err) {
        this._msg.textContent = err ? (err.message || String(err)) : '';
        this._msg.hidden = !err;
      }

      _getSource() {
        var src, inline;
        if (this._source != null) return Promise.resolve(this._source);
        src = this.getAttribute('src');
        if (src) {
          return fetch(src).then(function (r) {
            if (!r.ok) throw new Error('cannot load ' + src + ' (HTTP ' + r.status + ')');
            return r.text();
          });
        }
        inline = this.querySelector('script[type="text/omm"]');
        return Promise.resolve(inline ? inline.textContent : this.textContent);
      }

      reload() {
        var self = this, token = ++this._token;
        this._dropView();
        this._showError(null);
        return this._getSource().then(function (text) {
          if (token !== self._token) return null;
          if (!String(text).trim()) {
            throw new Error('no formulas found: use <script type="text/omm">, the src attribute, or the source property');
          }
          return compile(text, { resolution: parseInt(self.getAttribute('resolution'), 10) || 64 });
        }).then(function (models) {
          var old, canvas;
          if (!models || token !== self._token) return;
          old = self._root.querySelector('canvas');
          canvas = document.createElement('canvas');
          canvas.setAttribute('part', 'canvas');
          self._root.replaceChild(canvas, old);   /* a fresh canvas gets a fresh GL context */
          self._models = models;
          self._view = new View(canvas, models);
          self.dispatchEvent(new CustomEvent('omm-ready', { detail: { models: models } }));
        }).catch(function (err) {
          if (token !== self._token) return;
          self._showError(err);
          if (root.console) root.console.error('[omm]', err);
          self.dispatchEvent(new CustomEvent('omm-error', { detail: { error: err } }));
        });
      }
    }

    customElements.define('omm-model', OmmModel);
  }

  /* A bare <script type="text/omm"> becomes a viewer right after it.
   * Copies class, style and data-resolution from the script to the viewer. */
  function autoInit() {
    var list = document.querySelectorAll('script[type="text/omm"]'), i;
    for (i = 0; i < list.length; i++) {
      var s = list[i], el;
      if (s.__ommDone || (s.closest && s.closest('omm-model'))) continue;
      s.__ommDone = true;
      el = document.createElement('omm-model');
      el.source = s.textContent;
      if (s.getAttribute('class')) el.setAttribute('class', s.getAttribute('class'));
      if (s.getAttribute('style')) el.setAttribute('style', s.getAttribute('style'));
      if (s.getAttribute('data-resolution')) el.setAttribute('resolution', s.getAttribute('data-resolution'));
      s.parentNode.insertBefore(el, s.nextSibling);
    }
  }

  /* ----------------------------------------------------------------- export */

  var Omm = {
    version: VERSION,
    compile: compile,
    View: View,
    mount: mount,
    ready: function () { return loadCore().then(function () { return Omm; }); }
  };

  if (typeof document !== 'undefined') {
    defineElement();
    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', autoInit);
    else autoInit();
  }

  root.Omm = Omm;
  if (typeof module === 'object' && module && module.exports) module.exports = Omm;
})(typeof globalThis !== 'undefined' ? globalThis : this);
