'use strict';
// Runs the built bundle in Node: parse, mesh, errors, memory growth.
// CI runs this after emcc; locally: make smoke  (OMM_BUNDLE=path overrides the bundle).
const assert = require('assert');
const path = require('path');
const Omm = require(process.env.OMM_BUNDLE || path.join(__dirname, '..', 'dist', 'omm.js'));

const near = (a, b, eps = 1e-4) => Math.abs(a - b) <= eps * (1 + Math.abs(b));

async function main() {
  // 1. explicit surface z = f(x, y)
  const [m] = await Omm.compile('z = sin(x)*cos(y)\n  for x,y in -3..3', { resolution: 16 });
  assert.strictEqual(m.positions.length, 17 * 17 * 3);
  assert.strictEqual(m.normals.length, m.positions.length);
  assert.strictEqual(m.indices.length, 16 * 16 * 6);
  assert.ok(m.positions.every(Number.isFinite) && m.normals.every(Number.isFinite));
  assert.ok(m.indices.every((i) => i < 17 * 17));
  assert.ok(near(m.bounds[0], -3) && near(m.bounds[3], 3), 'x bounds are the "for" range');

  // 2. parametric surface (unit sphere), two models in one scene
  const sphere = '(sin(v)*cos(u), sin(v)*sin(u), cos(v)) for u in 0..2*pi, v in 0..pi';
  const models = await Omm.compile(sphere + '\nz = x*y for x,y in -1..1', { resolution: 24 });
  assert.strictEqual(models.length, 2);
  const p = models[0].positions;
  for (let i = 0; i < p.length; i += 3) {
    assert.ok(near(Math.hypot(p[i], p[i + 1], p[i + 2]), 1, 1e-5), 'sphere vertex ' + i / 3 + ' is on the unit sphere');
  }

  // 3. errors say what and where
  await assert.rejects(Omm.compile('z = 1\nz = nope(x)'), /line 2: unknown name 'nope'/);
  await assert.rejects(Omm.compile('x^2 + y^2 + z^2 = 1'), /implicit/);
  await assert.rejects(Omm.compile('z = sqrt(-1 - x*x)'), /finite/);

  // 4. memory growth: three 512x512 meshes live in the heap at once (~40 MB), and
  //    results copied out before the heap grew must stay intact afterwards
  const src = 'z = x*y  for x,y in 0..1\n'.repeat(3);
  const big = await Omm.compile(src, { resolution: 512 });
  assert.strictEqual(big.length, 3);
  for (const g of big) {
    assert.strictEqual(g.positions.length, 513 * 513 * 3);
    assert.ok(near(g.positions[2], 0), 'first vertex: x=0, y=0 -> z=0');
    assert.ok(near(g.positions[g.positions.length - 1], 1), 'last vertex: x=1, y=1 -> z=1');
  }

  console.log('smoke test passed');
}

main().catch((e) => { console.error(e); process.exit(1); });
