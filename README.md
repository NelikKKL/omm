# omm

Формулы на входе, 3D-модель на выходе. Ядро написано на C и собирается Emscripten'ом
в **обычный JavaScript** (`-sWASM=0`, никакого `.wasm`), на выходе один файл `omm.js`.

## Быстро

Формулы пишем прямо в HTML. Рядом с тегом появится интерактивный просмотрщик:

```html
<script src="omm.js"></script>

<script type="text/omm">
  z = sin(x)*cos(y)    for x,y in -3..3
</script>
```

Или в файле `.omm`:

```html
<omm-model src="torus.omm"></omm-model>
```

Почему `<script type="text/omm">`, а не свой тег `<omm>`: браузер не разбирает содержимое такого
`script`, поэтому `<`, `&` и прочие символы в формулах ничего не ломают. У `script` с чужим `type`
атрибут `src` не загружается, поэтому для файлов есть `<omm-model src="...">`.

Демо с живым редактором: `make serve`, затем <http://localhost:8000/examples/>
(файлы `.omm` читаются через `fetch`, поэтому нужен http, не `file://`).

## Язык `.omm`

Каждое выражение это одна модель. Комментарии начинаются с `#`.

```
z = sin(x)*cos(y)  for x,y in -3..3                       # поверхность z = f(x,y)

(sin(v)*cos(u), sin(v)*sin(u), cos(v))                    # параметрическая поверхность
  for u in 0..2*pi, v in 0..pi

let R = 2, r = 0.7                                        # константы
((R + r*cos(v))*cos(u), (R + r*cos(v))*sin(u), r*sin(v))
  for u in 0..2*pi, v in 0..2*pi
```

- `for` задаёт диапазоны: `for x,y in -3..3` (один диапазон на обе переменные) или
  `for u in 0..1, v in 0..2*pi` (у каждой свой). Границы могут быть выражениями. Если `for` нет,
  у `z = f(x,y)` берётся `x,y in -5..5`.
- Строка может продолжаться на следующей: после оператора или запятой, а также внутри скобок.
- Операторы: `+ - * / ^` (или `**`). Степень правоассоциативна: `-2^2 = -4`, `2^3^2 = 512`.
- Неявное умножение: `2pi`, `3x`, `2(x+1)`, `sin(x)cos(y)`. Приоритет как у `*`: `1/2x` это `(1/2)*x`.
- Функции: `sin cos tan asin acos atan sinh cosh tanh exp ln log log2 log10 sqrt cbrt abs floor ceil round sign frac`
  и с двумя аргументами `atan2 pow hypot min max mod`. Константы: `pi tau e`.
- Можно вставлять символы из учебников: `π × · ÷ − ² ³`.
- Точки, где формула не определена (`sqrt(-1)`, `1/0`), пропускаются вместе с треугольниками вокруг них.

## JavaScript API

```js
const models = await Omm.compile(source, { resolution: 64 });
// [{ positions: Float32Array, normals: Float32Array, indices: Uint32Array, bounds: [minx,miny,minz,maxx,maxy,maxz] }]

const view = await Omm.mount(document.getElementById('box'), source);   // canvas + WebGL-просмотрщик
```

`<omm-model>`: атрибуты `src`, `resolution` (1..512, по умолчанию 64); свойство `.source = '...'`
(главнее `src` и вложенного `script`); события `omm-ready` и `omm-error`; ошибка рисуется поверх
модели, её можно спрятать через `omm-model::part(error) { display: none }`.
Ошибки парсера содержат номер строки: `line 2: unknown name 'nope' ...`.

Ядро можно использовать и из C: см. `src/omm.h`.

## Сборка и тесты

```
make test-native    # тесты C-ядра под AddressSanitizer + UBSan (нужен только gcc)
make                # dist/omm.js (нужен Emscripten: emcc)
make smoke          # прогнать собранный bundle в Node
make serve          # http://localhost:8000/examples/
```

Сборка в чистый JS идёт через wasm2js: файл крупнее и работает медленнее, чем `.wasm`.
Если скорость станет узким местом, достаточно заменить `-sWASM=0` на `-sWASM=2` (wasm с откатом на JS)
в `Makefile`, исходники те же.

## GitHub Actions

`.github/workflows/build.yml`: на каждый push и pull request: нативные тесты, сборка Emscripten,
smoke-тест в Node, `dist/omm.js` кладётся в артефакты. Тег вида `v0.1.0` создаёт Release с `omm.js`.
Версия Emscripten закреплена в `EM_VERSION`.

## Устройство

```
src/lexer.c    текст -> токены
src/parser.c   токены -> модели (байткод), let, for
src/vm.c       вычисление байткода, таблица функций
src/mesh.c     сетка (u,v) -> вершины, нормали, треугольники
src/omm.c      публичный API
js/omm.js      обёртка над ядром, WebGL-просмотрщик, <omm-model>
```

## Дальше

- неявные поверхности `F(x,y,z) = 0` (surface nets или marching cubes)
- кривые с одним параметром
- цвета и материалы, булевы операции над моделями
