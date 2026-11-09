// SPDX-License-Identifier: GPL-3.0-only
// The install page's package code (web/installer/fm1pkg.js) against tools/fm1pkg_make.py:
//   node tests/fm1/pkg_test.mjs RAVEL.fwsc OTA-STOCK.bin [STOCK.fwsc EXPECTED_BACK.fwsc]
// 1. rebuilding Ravel's package from its own files gives the same bytes;
// 2. with a stock package: stockBack() == the Python-built back-to-stock package.
import { readFileSync } from "node:fs";
import { filesOf, buildPackage, productOf, stockBack } from "../../web/installer/fm1pkg.js";
const rd = (p) => new Uint8Array(readFileSync(p));
let bad = 0;
const check = (what, ok) => { console.log(`${what.padEnd(56)} ${ok ? "ok" : "FAIL"}`); bad += ok ? 0 : 1; };
const eq = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);
const [ravelP, otaP, stockP, wantP] = process.argv.slice(2);
const ravel = rd(ravelP), f = filesOf(ravel);
check("Ravel's package rebuilt from its files: same bytes", eq(buildPackage(f[0], f[100], productOf(ravel)), ravel));
const ota = rd(otaP);
if (stockP) {
  const back = stockBack(rd(stockP), ota);
  check("stockBack == the Python build", eq(back, rd(wantP)));
  check("its identity is stock's", productOf(back) === productOf(rd(stockP)));
  check("its loader is ota-stock.bin", eq(filesOf(back)[100], ota));
}
let refused = false;
try { stockBack(buildPackage(new Uint8Array(0xA9000).fill(0xFF), f[100], "FM-1_016"), ota); } catch (e) { refused = true; }
check("a flash.bin of another size (V16's) is refused", refused);
console.log(bad ? "PACKAGE TEST FAILED" : "package test passed");
process.exit(bad ? 1 : 0);
