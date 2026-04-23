import fs from "fs";
import path from "path";
import { execSync } from "child_process";
import { marked } from "marked";

const releasesDir = path.join(process.cwd(), "..", "release-notes");

// Map of version string → ISO date the tag was pushed. Populated lazily once
// per build; git tags in the parent repo are the source of truth.
function getTagDates() {
  try {
    const out = execSync(
      "git for-each-ref --format=\"%(refname:short) %(creatordate:iso-strict)\" refs/tags/",
      { cwd: process.cwd(), encoding: "utf8" }
    );
    const map = {};
    for (const line of out.split("\n")) {
      const parts = line.trim().split(/\s+/);
      if (parts.length >= 2) map[parts[0]] = parts[1];
    }
    return map;
  } catch {
    return {};
  }
}

// Map H2 heading text → bucket name on the release object.
const BUCKET = [
  { re: /new\s+features?/i,             bucket: "added" },
  { re: /new\s+boards?/i,               bucket: "added" },
  { re: /added/i,                       bucket: "added" },
  { re: /improvements?/i,               bucket: "changed" },
  { re: /changed|enhancements?/i,       bucket: "changed" },
  { re: /bug\s+fixes?|^fixed$|^fixes?$/i, bucket: "fixed" },
  { re: /removed|deprecated/i,          bucket: "removed" },
  { re: /breaking/i,                    bucket: "removed" },
  { re: /contributors?/i,               bucket: "contributors" },
  { re: /install|download/i,            bucket: "install" },
];

function bucketFor(heading) {
  const trimmed = heading.trim();
  for (const { re, bucket } of BUCKET) if (re.test(trimmed)) return bucket;
  return "other";
}

// Split markdown by top-level H2 headings into [{ heading, body }] sections.
function splitByH2(md) {
  const lines = md.split(/\r?\n/);
  const out = [];
  let cur = { heading: null, body: [] };
  for (const line of lines) {
    const m = line.match(/^##\s+(.+?)\s*$/);
    if (m && !line.startsWith("###")) {
      if (cur.heading !== null || cur.body.length > 0) out.push(cur);
      cur = { heading: m[1], body: [] };
    } else {
      cur.body.push(line);
    }
  }
  out.push(cur);
  return out;
}

// Turn a body block into structured items. We support three MD shapes:
// 1) Blockquote item:    "> **Title**\n> description"  (release-notes style for New Features / New Boards)
// 2) Bullet item:        "- **Label** — description"
// 3) Plain bullet:       "- free text"
function parseItems(bodyText) {
  const items = [];
  const trailing = []; // paragraphs that live after items (e.g. "Six new boards in one release.")

  // First, pull blockquote groups (stretches of consecutive `> …` lines).
  // Each line that starts with `> **Title**` begins a new item — subsequent
  // non-bold `> …` lines attach to the current item as continuation.
  const lines = bodyText.split(/\r?\n/);
  let i = 0;
  while (i < lines.length) {
    const line = lines[i];
    if (/^\s*>/.test(line)) {
      const groups = [];
      let current = null;
      while (i < lines.length && /^\s*>/.test(lines[i])) {
        const stripped = lines[i].replace(/^\s*>\s?/, "");
        if (/^\s*\*\*/.test(stripped) || current === null) {
          if (current) groups.push(current);
          current = [stripped];
        } else {
          current.push(stripped);
        }
        i++;
      }
      if (current) groups.push(current);

      for (const group of groups) {
        const joined = group.join("\n").trim();
        const titleMatch = joined.match(/^\*\*(.+?)\*\*\s*(.*)$/s);
        if (titleMatch) {
          const title = titleMatch[1].trim();
          let desc = titleMatch[2].replace(/^\n+/, "").trim();
          desc = desc.replace(/^[—–-]\s*/, "");
          items.push({ title, desc });
        } else if (joined) {
          items.push({ title: null, desc: joined });
        }
      }
    } else if (/^\s*-\s+/.test(line)) {
      // Bullet item — may span multiple lines of continuation (indented).
      const block = [line.replace(/^\s*-\s+/, "")];
      i++;
      while (i < lines.length && /^\s{2,}\S/.test(lines[i])) {
        block.push(lines[i].trim());
        i++;
      }
      const joined = block.join(" ").trim();
      const m = joined.match(/^\*\*(.+?)\*\*\s*(?:[—–-]\s*)?(.*)$/s);
      if (m && m[2]) {
        items.push({ title: m[1].trim(), desc: m[2].trim() });
      } else if (m) {
        items.push({ title: m[1].trim(), desc: "" });
      } else {
        items.push({ title: null, desc: joined });
      }
    } else if (line.trim()) {
      trailing.push(line);
      i++;
    } else {
      i++;
    }
  }

  const note = trailing.join("\n").trim();
  return { items, note };
}

function renderItemDesc(desc) {
  if (!desc) return "";
  return marked.parseInline(desc);
}

function renderPreamble(md) {
  // Content before the first H2 (preamble / hero note)
  return marked.parse(md, { headerIds: false, mangle: false });
}

function renderInstall(md) {
  return marked.parse(md, { headerIds: false, mangle: false });
}

function readReleaseFile(filePath) {
  const raw = fs.readFileSync(filePath, "utf8");

  // Title comes from first H1, or filename.
  const h1 = raw.match(/^#\s+(.+)$/m);
  const title = h1 ? h1[1].trim() : null;

  // Drop first H1 line and optional *YYYY-MM-DD* date line (shown via git tag date instead).
  const body = raw
    .replace(/^#\s+.+\n?/, "")
    .replace(/^\*\d{4}-\d{2}-\d{2}\*\s*\n?/, "");

  const sections = splitByH2(body);

  const release = {
    title,
    preamble: "",
    added: [],
    changed: [],
    fixed: [],
    removed: [],
    contributors: [],
    install: "",
    notes: {}, // per-bucket trailing notes
  };

  for (const sec of sections) {
    if (sec.heading === null) {
      const pre = sec.body.join("\n").trim();
      if (pre) release.preamble = renderPreamble(pre);
      continue;
    }
    const bucket = bucketFor(sec.heading);
    const text = sec.body.join("\n");

    if (bucket === "install") {
      release.install = renderInstall(text);
      continue;
    }

    const { items, note } = parseItems(text);
    if (bucket === "contributors") {
      release.contributors = items.map((it) => it.title || it.desc).filter(Boolean);
      continue;
    }
    if (bucket === "other") continue;

    for (const it of items) {
      release[bucket].push({
        title: it.title,
        html: renderItemDesc(it.desc),
      });
    }
    if (note) release.notes[bucket] = renderPreamble(note);
  }

  return release;
}

export function getAllReleases() {
  const files = fs.readdirSync(releasesDir).filter((f) => f.endsWith(".md"));
  const tagDates = getTagDates();

  return files
    .map((filename) => {
      const version = filename.replace(".md", "");
      const data = readReleaseFile(path.join(releasesDir, filename));
      return { version, date: tagDates[version] || null, ...data };
    })
    .sort((a, b) => {
      const pa = a.version.split(".").map(Number);
      const pb = b.version.split(".").map(Number);
      for (let i = 0; i < 3; i++) {
        if ((pb[i] ?? 0) !== (pa[i] ?? 0)) return (pb[i] ?? 0) - (pa[i] ?? 0);
      }
      return 0;
    });
}

export function getLatestRelease() {
  return getAllReleases()[0] || null;
}
