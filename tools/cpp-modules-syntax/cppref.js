// cppreference lookup: qualified C++ name -> cppreference page + short summary.
// Pure Node (no vscode import) so it can be tested from the command line.
//
// Symbol index: index-functions-cpp.xml from github.com/p12tic/cppreference-doc
// (CC BY-SA 3.0, same license as cppreference.com).
"use strict";
const fs = require("fs");
const path = require("path");

const BASE = "https://en.cppreference.com/w/";

function decode(s) {
  return s
    .replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&quot;/g, '"')
    .replace(/&#39;/g, "'").replace(/&nbsp;/g, " ")
    .replace(/&#(\d+);/g, (_, n) => String.fromCodePoint(+n))
    .replace(/&#x([0-9a-f]+);/gi, (_, n) => String.fromCodePoint(parseInt(n, 16)))
    .replace(/&amp;/g, "&");
}

function attrs(tag) {
  const out = {};
  for (const m of tag.matchAll(/(\w+)="([^"]*)"/g)) out[m[1]] = decode(m[2]);
  return out;
}

// name -> { link, since } ; aliases resolved lazily.
function loadIndex(file = path.join(__dirname, "index-functions-cpp.xml")) {
  const xml = fs.readFileSync(file, "utf8");
  const entries = new Map();
  const aliases = new Map();
  const stack = []; // enclosing <class>/<enum>: { name, link, since }
  for (const m of xml.matchAll(/<(\/?)(\w+)([^>]*?)(\/?)>/g)) {
    const [, closing, tag, rest, selfClose] = m;
    const container = tag === "class" || tag === "enum";
    if (closing) {
      if (container) stack.pop();
      continue;
    }
    const a = attrs(rest);
    const parent = stack[stack.length - 1];
    if (!parent) {  // top level: std::vector, std::sort, std::string (alias) ...
      if (!a.name) continue;
      if (a.alias) aliases.set(a.name, a.alias);
      else if (a.link) entries.set(a.name, { link: a.link, since: a.since });
      if (container && !selfClose) stack.push({ name: a.name, link: a.link, since: a.since });
      continue;
    }
    const short = parent.name.split("::").pop();
    let name, link;
    if (tag === "constructor") { name = short; link = a.link || short; }
    else if (tag === "destructor") { name = "~" + short; link = a.link || "~" + short; }
    else if (a.name) { name = a.name; link = a.link || a.name; }
    else {
      if (container && !selfClose) stack.push({ name: parent.name + "::?", link: null });
      continue;
    }
    const resolved = !parent.link ? null
      : link === "." ? parent.link
      : link.includes("/") ? link
      : parent.link + "/" + link;
    if (tag === "overload") {  // non-member overloads: std::swap, operator<< ...
      const full = name.includes("::") ? name : "std::" + name;
      if (resolved && !entries.has(full)) entries.set(full, { link: resolved, since: a.since });
    } else if (tag !== "inherits" && tag !== "specialization") {
      const full = parent.name + "::" + name;
      if (resolved) entries.set(full, { link: resolved, since: a.since || parent.since });
    }
    if (container && !selfClose) stack.push({ name: parent.name + "::" + name, link: resolved, since: a.since });
  }
  return { entries, aliases };
}

// "std::__1::vector::" + "push_back" -> "std::vector::push_back"
function normalize(container, name) {
  let q = (container || "") + name;
  q = q.replace(/<[^<>]*(?:<[^<>]*>[^<>]*)*>/g, "");          // drop template args
  const parts = q.split("::");
  const last = parts.pop();
  q = [...parts.filter((p) => p && !p.startsWith("__")), last].join("::"); // libc++ inline ns
  q = q.replace(/^PPP::Checked_vector\b/, "std::vector")
       .replace(/^PPP::Checked_string\b/, "std::string")
       .replace(/^PPP::Checked_span\b/, "std::span");
  return q;
}

function lookup(index, qname) {
  if (!qname || qname.split("::").pop().startsWith("_")) return null; // libc++ internals
  const tryName = (n) => {
    for (let i = 0; i < 4 && index.aliases.has(n); i++) n = index.aliases.get(n);
    return index.entries.has(n) ? { name: n, ...index.entries.get(n) } : null;
  };
  let hit = tryName(qname);
  if (hit) return hit;
  // std::string::size -> std::basic_string::size via the alias of the class part
  const parts = qname.split("::");
  for (let cut = parts.length - 1; cut >= 2; cut--) {
    const head = parts.slice(0, cut).join("::");
    if (index.aliases.has(head)) {
      hit = tryName(index.aliases.get(head) + "::" + parts.slice(cut).join("::"));
      if (hit) return hit;
    }
  }
  // Unknown member of a known class: point at the class page.
  if (parts.length > 2) {
    const cls = tryName(parts.slice(0, -1).join("::"));
    if (cls) return { ...cls, approximate: true };
  }
  return null;
}

function url(link) {
  return BASE + link;
}

// First paragraph(s) after the declaration box of a cppreference page.
function extractSummary(html, maxLen = 450) {
  let start = html.indexOf('class="t-dcl-begin"');
  if (start >= 0) start = html.indexOf("</table>", start);
  if (start < 0) start = html.indexOf('class="mw-parser-output"');
  if (start < 0) return "";
  let end = html.length;
  for (const stop of ["<h3", "<h2", "<table", '<div class="t-navbar']) {
    const i = html.indexOf(stop, start + 8);
    if (i >= 0 && i < end) end = i;
  }
  let body = html.slice(start, end);
  body = body
    .replace(/<code[^>]*>([\s\S]*?)<\/code>/g, (_, c) => "`" + c.replace(/<[^>]+>/g, "") + "`")
    .replace(/<(br|\/p|\/div|\/li)[^>]*>/g, "\n\n")
    .replace(/<[^>]+>/g, "");
  const paras = decode(body)
    .split(/\n\s*\n/)
    .map((p) => p.replace(/\s+/g, " ").replace(/``/g, "").trim())
    .filter((p) => p.length > 15);
  let out = "";
  for (const p of paras) {
    if (/^Inheritance diagram|^Member types|^Template parameters/.test(p)) break;
    if (out && out.length + p.length > maxLen) break;
    out += (out ? "\n\n" : "") + p;
    if (out.length > maxLen / 2) break;
  }
  return out.length > maxLen ? out.slice(0, maxLen - 1) + "…" : out;
}

async function fetchSummary(link, timeoutMs = 3000) {
  const res = await fetch(url(link), {
    signal: AbortSignal.timeout(timeoutMs),
    headers: { "User-Agent": "cpp-modules-syntax-vscode/0.1" },
  });
  if (!res.ok) throw new Error("HTTP " + res.status);
  return extractSummary(await res.text());
}

module.exports = { loadIndex, normalize, lookup, url, extractSummary, fetchSummary };
