// 1. Hover + go-to-definition for module names in `import X;` lines
//    (clangd 22 offers neither for module names).
// 2. A "cppreference" section under clangd's hover for every standard-library
//    name: exact page link + the page's opening summary (cached on disk).
const vscode = require("vscode");
const fs = require("fs");
const path = require("path");
const cppref = require("./cppref");

const IMPORT_RE = /^\s*(?:export\s+)?import\s+([A-Za-z_][\w.]*)\s*;/;
const LIBCXX = "/usr/lib/llvm-22/share/libc++/v1";

const KNOWN = {
  std: {
    file: path.join(LIBCXX, "std.cppm"),
    doc:
      "**module `std`** - the whole C++ standard library (libc++ 22), as one module.\n\n" +
      "`import std;` makes every `std::` name available: `std::cout`, `std::vector`, " +
      "`std::string`, `std::sort`, `std::format`, ...\n\n" +
      "Not included: **macros** (`assert`, `INT_MAX`, `EOF`, ...) - modules can't export " +
      "macros, so `#include <cassert>` / `<climits>` for those. Global C names like " +
      "`::printf` or `::size_t` need `import std.compat;`.\n\n" +
      "Hover any `std::` name for its own documentation.",
  },
  "std.compat": {
    file: path.join(LIBCXX, "std.compat.cppm"),
    doc:
      "**module `std.compat`** - everything in `std`, plus the C library's names in the " +
      "global namespace (`::printf`, `::size_t`, `::strlen`, ...).",
  },
  PPP: {
    file: undefined, // found in the workspace (include/PPP/PPP.ixx)
    doc:
      "**module `PPP`** - Stroustrup's support library for *Programming: Principles and " +
      "Practice Using C++* (3rd ed.). Re-exports `std`, plus:\n\n" +
      "- `PPP::Checked_vector`, `Checked_string`, `Checked_span` - range-checked `[]`\n" +
      "- `error(...)` - throw a `runtime_error` with a message\n" +
      "- `narrow<T>(x)` / `narrow_cast<T>(x)` - checked / unchecked conversions\n" +
      "- `expect(cond, msg)`, `random_int(min, max)`, `seed(...)`\n\n" +
      "`#include \"PPP.h\"` gives the same names plus `using namespace std;` and the " +
      "`vector`/`string`/`span` -> `Checked_*` macros.",
  },
};

function findModuleFile(name) {
  if (KNOWN[name] && KNOWN[name].file) return KNOWN[name].file;
  for (const folder of vscode.workspace.workspaceFolders || []) {
    const stack = [folder.uri.fsPath];
    while (stack.length) {
      const dir = stack.pop();
      let entries = [];
      try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch { continue; }
      for (const e of entries) {
        if (e.name.startsWith(".") || e.name === "build" || e.name === "node_modules") continue;
        const p = path.join(dir, e.name);
        if (e.isDirectory()) stack.push(p);
        else if (/\.(ixx|cppm|mpp|cxx|cpp)$/.test(e.name)) {
          try {
            const text = fs.readFileSync(p, "utf8");
            const re = new RegExp(`^\\s*export\\s+module\\s+${name.replace(/\./g, "\\.")}\\s*;`, "m");
            if (re.test(text)) return p;
          } catch {}
        }
      }
    }
  }
  return undefined;
}

function moduleAt(document, position) {
  const m = IMPORT_RE.exec(document.lineAt(position.line).text);
  if (!m) return undefined;
  const start = document.lineAt(position.line).text.indexOf(m[1], m[0].indexOf("import") + 6);
  const range = new vscode.Range(position.line, start, position.line, start + m[1].length);
  return range.contains(position) ? { name: m[1], range } : undefined;
}

// ------------------------------------------------------------ cppreference --
let cpprefIndex;
let summaryCache = {};
let cacheFile;
let saveTimer;

function config() {
  return vscode.workspace.getConfiguration("cppModules.cppreference");
}

async function clangdClient() {
  const ext = vscode.extensions.getExtension("llvm-vs-code-extensions.vscode-clangd");
  if (!ext) return undefined;
  if (!ext.isActive) await ext.activate();
  const api = ext.exports && ext.exports.getApi && ext.exports.getApi(1);
  return api && api.languageClient;
}

// Escape markdown outside `code spans` (cppreference text has * _ < > ...).
function mdEscape(text) {
  return text
    .split(/(`[^`]*`)/)
    .map((part, i) => (i % 2 ? part : part.replace(/[\\*_\[\]<>#|]/g, "\\$&")))
    .join("");
}

function saveCacheSoon() {
  clearTimeout(saveTimer);
  saveTimer = setTimeout(() => {
    try {
      fs.mkdirSync(path.dirname(cacheFile), { recursive: true });
      fs.writeFileSync(cacheFile, JSON.stringify(summaryCache));
    } catch {}
  }, 2000);
}

async function summaryFor(link) {
  if (link in summaryCache) return summaryCache[link];
  try {
    const s = await cppref.fetchSummary(link, 2500);
    summaryCache[link] = s;
    saveCacheSoon();
    return s;
  } catch {
    return ""; // offline / slow: link only, try again next time
  }
}

async function cppreferenceHover(document, position, token) {
  if (!config().get("enabled", true)) return undefined;
  const client = await clangdClient();
  if (!client || token.isCancellationRequested) return undefined;
  let infos;
  try {
    infos = await client.sendRequest(
      "textDocument/symbolInfo",
      {
        textDocument: { uri: client.code2ProtocolConverter.asUri(document.uri) },
        position: { line: position.line, character: position.character },
      },
      token
    );
  } catch {
    return undefined;
  }
  if (!Array.isArray(infos) || !infos.length) return undefined;
  if (!cpprefIndex) cpprefIndex = cppref.loadIndex();

  const wordRange = document.getWordRangeAtPosition(position);
  const word = wordRange ? document.getText(wordRange) : "";
  // Prefer the symbol whose name is what you hovered (std::string over basic_string).
  infos.sort((a, b) => (b.name === word) - (a.name === word));
  for (const info of infos) {
    let container = info.containerName || "";
    if (container && !container.endsWith("::")) container += "::";
    if (!/^(std::|PPP::Checked_)/.test(container + info.name)) continue;
    const qname = cppref.normalize(container, info.name);
    const hit = cppref.lookup(cpprefIndex, qname);
    if (!hit) continue;

    const md = new vscode.MarkdownString();
    const since = hit.since ? ` *(since ${hit.since.replace("c++", "C++")})*` : "";
    const shown = hit.approximate ? `${hit.name} (class page)` : qname;
    md.appendMarkdown(`**cppreference** · [\`${shown}\`](${cppref.url(hit.link)})${since}`);
    if (config().get("summaries", true)) {
      const summary = await summaryFor(hit.link);
      if (summary && !token.isCancellationRequested)
        md.appendMarkdown("\n\n" + mdEscape(summary));
    }
    return new vscode.Hover(md, wordRange);
  }
  return undefined;
}

function activate(context) {
  const selector = { language: "cpp" };
  cacheFile = path.join(context.globalStorageUri.fsPath, "cppreference-summaries.json");
  try { summaryCache = JSON.parse(fs.readFileSync(cacheFile, "utf8")); } catch {}
  context.subscriptions.push(
    vscode.commands.registerCommand("cppModules.cppreference.clearCache", () => {
      summaryCache = {};
      saveCacheSoon();
      vscode.window.showInformationMessage("cppreference summary cache cleared.");
    })
  );
  context.subscriptions.push(
    vscode.languages.registerHoverProvider(selector, {
      provideHover(document, position, token) {
        const mod = moduleAt(document, position);
        if (!mod) return cppreferenceHover(document, position, token);
        const file = findModuleFile(mod.name);
        let doc = KNOWN[mod.name] ? KNOWN[mod.name].doc : `**module \`${mod.name}\`**`;
        if (!KNOWN[mod.name] && file) {
          // Show the comment block right above `export module X;`, if any.
          const lines = fs.readFileSync(file, "utf8").split("\n");
          const at = lines.findIndex((l) => /^\s*export\s+module\b/.test(l));
          const comments = [];
          for (let i = at - 1; i >= 0 && /^\s*\/\//.test(lines[i]); i--)
            comments.unshift(lines[i].replace(/^\s*\/\/\s?/, ""));
          if (comments.length) doc += "\n\n" + comments.join("\n");
        }
        if (file) doc += `\n\n---\nSource: \`${file}\` (Ctrl+click to open)`;
        else doc += "\n\n_(module source not found in the workspace)_";
        return new vscode.Hover(new vscode.MarkdownString(doc), mod.range);
      },
    }),
    vscode.languages.registerDefinitionProvider(selector, {
      provideDefinition(document, position) {
        const mod = moduleAt(document, position);
        if (!mod) return;
        const file = findModuleFile(mod.name);
        if (!file) return;
        const lines = fs.readFileSync(file, "utf8").split("\n");
        const line = Math.max(0, lines.findIndex((l) => /^\s*export\s+module\b/.test(l)));
        return new vscode.Location(vscode.Uri.file(file), new vscode.Position(line, 0));
      },
    })
  );
}

module.exports = { activate, deactivate() {} };
