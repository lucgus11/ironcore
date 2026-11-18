/* VERSION DE SECOURS PREconstruite (clang wasm32) : permet de jouer sans Emscripten.
 * Ecrasee par './build.sh wasm' ou par la CI GitHub (vraie compilation Emscripten). */
async function IroncoreModule() {
  let mem;
  const env = { sinf: Math.sin, cosf: Math.cos };
  const res = await fetch('game.wasm');
  const { instance } = await WebAssembly.instantiate(await res.arrayBuffer(), { env });
  mem = instance.exports.memory;
  const M = {};
  for (const k in instance.exports) if (k.startsWith('game_')) M['_' + k] = instance.exports[k];
  Object.defineProperty(M, 'HEAPU8', { get: () => new Uint8Array(mem.buffer) });
  Object.defineProperty(M, 'HEAPF32', { get: () => new Float32Array(mem.buffer) });
  return M;
}
