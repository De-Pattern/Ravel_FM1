// SPDX-License-Identifier: GPL-3.0-only
// The manual as a PDF: build/web/ravel-manual.html (tools/make_manual.py) printed by headless Chromium,
// A4, with the page's print styles (light, a chapter to a page, the contents and section links kept).
//   node tools/make_manual_pdf.mjs [IN.html] [OUT.pdf]
// Needs Playwright (npm i playwright) and a Chromium it can launch; CHROMIUM=path picks one.
import { chromium } from "playwright";
import { resolve } from "node:path";
const inp = resolve(process.argv[2] || "build/web/ravel-manual.html");
const out = resolve(process.argv[3] || "build/web/ravel-manual.pdf");
const browser = await chromium.launch(process.env.CHROMIUM ? { executablePath: process.env.CHROMIUM } : {});
const page = await browser.newPage();
await page.goto("file://" + inp, { waitUntil: "networkidle" });
await page.emulateMedia({ media: "print" });
await page.pdf({
  path: out, format: "A4", printBackground: true, outline: true, tagged: true,
  displayHeaderFooter: true, headerTemplate: "<span></span>",
  footerTemplate: '<div style="font:8px sans-serif;color:#777;width:100%;text-align:center">Ravel manual · <span class="pageNumber"></span> / <span class="totalPages"></span></div>',
  margin: { top: "16mm", bottom: "18mm", left: "15mm", right: "15mm" },
});
await browser.close();
console.log("manual pdf: " + out);
