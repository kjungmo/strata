#!/usr/bin/env python3
"""Convert the STRATA LaTeX master (paper/arxiv_ref) into the long-form Markdown companion.

Mechanical: macros expanded from the generated macro files, numbers of sections, tables,
equations and theorems taken from main.aux, math kept verbatim, citations as pandoc [@key].
Usage: tex2md.py <arxiv_ref dir> <out.md>
"""
import re
import sys
from pathlib import Path

ROOT = Path(sys.argv[1])
OUT = Path(sys.argv[2])


# ---------------------------------------------------------------- helpers
def read(p):
    return Path(p).read_text(encoding="utf-8")


def strip_comments(s):
    out = []
    for line in s.split("\n"):
        i = 0
        while True:
            j = line.find("%", i)
            if j < 0:
                break
            k = j - 1
            nb = 0
            while k >= 0 and line[k] == "\\":
                nb += 1
                k -= 1
            if nb % 2 == 0:
                line = line[:j]
                break
            i = j + 1
        out.append(line)
    return "\n".join(out)


def group(s, i):
    """s[i] == '{' -> (content, index after the closing brace)."""
    assert s[i] == "{", s[i:i + 40]
    d = 0
    j = i
    while j < len(s):
        c = s[j]
        if c == "\\":
            j += 2
            continue
        if c == "{":
            d += 1
        elif c == "}":
            d -= 1
            if d == 0:
                return s[i + 1:j], j + 1
        j += 1
    raise ValueError("unbalanced: " + s[i:i + 80])


def opt(s, i):
    """optional [..] at s[i] -> (content or None, new index)."""
    if i < len(s) and s[i] == "[":
        d = 0
        for j in range(i, len(s)):
            if s[j] == "[":
                d += 1
            elif s[j] == "]":
                d -= 1
                if d == 0:
                    return s[i + 1:j], j + 1
    return None, i


def skip_ws(s, i):
    while i < len(s) and s[i] in " \t\n":
        i += 1
    return i


# ---------------------------------------------------------------- macros
MACROS = {}
for f in ("main.tex", "tables/e4_text.tex", "tables/numbers_v2.tex"):
    t = strip_comments(read(ROOT / f))
    for m in re.finditer(r"\\newcommand\{\\([A-Za-z]+)\}(\[\d\])?", t):
        if m.group(2):
            continue
        body, _ = group(t, m.end())
        MACROS[m.group(1)] = body
MACROS.pop("paramcols", None)

# ---------------------------------------------------------------- aux numbers
LAB = {}
for m in re.finditer(r"\\newlabel\{([^}]+)\}\{\{([^}]*)\}", read(ROOT / "main.aux")):
    LAB[m.group(1)] = m.group(2)

# ---------------------------------------------------------------- sources
def src(name):
    return strip_comments(read(ROOT / name))


def expand_inputs(t):
    def rep(m):
        p = m.group(1)
        if not p.endswith(".tex"):
            p += ".tex"
        if p.startswith("tables/"):
            return "\n" + src(p) + "\n"
        if p.startswith("figures/"):
            return "\n" + src(p) + "\n"
        return m.group(0)
    return re.sub(r"\\input\{([^}]+)\}", rep, t)


ORDER = ["0_abstract", "1_introduction", "2_related_works", "3_method", "3b_evidence",
         "4_experiments", "4b_calibration", "4c_realdata", "5_discussion", "6_conclusion"]
body = ""
for n in ORDER:
    body += src(f"sections/{n}.tex") + "\n"
body += "\n\\APPENDIXSTART\n" + src("sections/appendix.tex") + "\n"
# figures and tables come in through \input
body = expand_inputs(body)
body = expand_inputs(body)

# ---------------------------------------------------------------- macro expansion
names = sorted(MACROS, key=len, reverse=True)
mac_re = re.compile(r"\\(" + "|".join(map(re.escape, names)) + r")(?![A-Za-z])(\{\})?")


def expand_macros(t):
    for _ in range(8):
        n = mac_re.sub(lambda m: (" " if (re.search(r"\\[A-Za-z]+$", m.string[max(0, m.start() - 20):m.start()]) and MACROS[m.group(1)][:1].isalpha()) else "") + MACROS[m.group(1)], t)
        if n == t:
            break
        t = n
    return t


body = expand_macros(body)

# ---------------------------------------------------------------- environments
STUB = []


def stash(text):
    STUB.append(text)
    return f"\x00{len(STUB) - 1}\x00"


def unstash(t):
    for _ in range(6):
        n = re.sub(r"\x00(\d+)\x00", lambda m: STUB[int(m.group(1))], t)
        if n == t:
            break
        t = n
    return t


def find_env(t, name, start=0):
    """return (begin_idx, end_idx, optarg, content) of the first \\begin{name} at/after start."""
    m = re.compile(r"\\begin\{" + re.escape(name) + r"\}").search(t, start)
    if not m:
        return None
    i = m.end()
    o, i2 = opt(t, i)
    # nested same-name environments are not used
    e = t.find("\\end{" + name + "}", i2)
    return m.start(), e + len("\\end{" + name + "}"), o, t[i2:e]


def label_of(s):
    m = re.search(r"\\label\{([^}]+)\}", s)
    return m.group(1) if m else None


def tex_inline(s):
    """text-mode LaTeX -> markdown for a short fragment (used for captions and cells)."""
    return text_pass(s)


def convert_tabular(content):
    m = re.search(r"\\begin\{tabular\}", content)
    i = m.end()
    spec, i = group(content, i)
    e = content.rfind("\\end{tabular}")
    tb = content[i:e]
    ncols = len(re.findall(r"[lcrXp]", re.sub(r"\{[^{}]*\}", "", spec.replace(">{\\raggedright\\arraybackslash}", ""))))
    rows, cur, hdr_end = [], [], None
    tb = re.sub(r"\\(toprule|bottomrule)", "", tb)
    tb = re.sub(r"\\cmidrule(\([lr]+\))?\{[^}]*\}", "", tb)
    parts = re.split(r"(\\midrule)", tb)
    blocks = []
    for p in parts:
        if p == "\\midrule":
            blocks.append("|")
            continue
        for r in re.split(r"\\\\(?:\[[^\]]*\])?", p):
            if r.strip():
                blocks.append(r.strip())
    # rows before the first midrule are header rows
    hdr, data, seen = [], [], False
    for b in blocks:
        if b == "|":
            seen = True
            continue
        (data if seen else hdr).append(b)
    if not seen:
        hdr, data = data[:1], data[1:]

    def cells(r):
        out = []
        for c in re.split(r"(?<!\\)&", r):
            c = c.strip()
            mm = re.match(r"\\multicolumn\{(\d+|\\paramcols)\}\{[^}]*\}\{(.*)\}$", c, re.S)
            if mm:
                out.append(mm.group(2).strip())
                out += [""] * ((4 if mm.group(1) == "\\paramcols" else int(mm.group(1))) - 1)
            else:
                out.append(c)
        out = [tex_inline(x).replace("|", "\\|").replace("\n", " ") for x in out]
        return out + [""] * (ncols - len(out))

    H = [cells(r) for r in hdr]
    header = [" ".join(x for x in col if x).strip() for col in zip(*H)] if H else [""] * ncols
    lines = ["| " + " | ".join(header) + " |", "|" + "|".join(["---"] * ncols) + "|"]
    for r in data:
        lines.append("| " + " | ".join(cells(r)) + " |")
    return "\n".join(lines)


def conv_table(m_content, opt_):
    cap = ""
    mc = re.search(r"\\caption\{", m_content)
    if mc:
        cap, _ = group(m_content, mc.end() - 1)
    lab = label_of(m_content)
    tab = convert_tabular(re.sub(r"\\caption\{" + r".*", "", m_content, flags=re.S) if False else m_content)
    num = LAB.get(lab, "?")
    return f"\n\n**Table {num}.** {tex_inline(cap)}\n\n{tab}\n\n"


def conv_figure(content):
    cap = ""
    mc = re.search(r"\\caption\{", content)
    if mc:
        cap, _ = group(content, mc.end() - 1)
    lab = label_of(content)
    num = LAB.get(lab, "?")
    return (f"\n\n> **Figure {num}.** {tex_inline(cap)} *(Drawn in the PDF with TikZ/pgfplots from the "
            f"data in `paper/arxiv_ref/figures/data/`; not reproduced in this Markdown file.)*\n\n")


def alg_inline(s):
    out, i = "", 0
    while i < len(s):
        if s.startswith("\\tcp*", i):
            j = i + 5
            _, j = opt(s, j)
            c, j = group(s, j)
            out += "  // " + c
            i = j
        else:
            out += s[i]
            i += 1
    return out


def alg_render(s, ind, lines):
    i, buf = 0, ""
    pre = "  " * ind + "- "

    def flush():
        nonlocal buf
        if buf.strip():
            lines.append(pre + tex_inline(alg_inline(buf.strip())))
        buf = ""

    while i < len(s):
        for kw, label in (("\\ForEach{", "for each "), ("\\If{", "if ")):
            if s.startswith(kw, i):
                flush()
                cond, j = group(s, i + len(kw) - 1)
                j = skip_ws(s, j)
                body, j = group(s, j)
                lines.append(pre + tex_inline(label + cond + ":"))
                alg_render(body, ind + 1, lines)
                i = j
                break
        else:
            handled = False
            for kw, label in (("\\lIf{", "if "), ("\\lElseIf{", "else if ")):
                if s.startswith(kw, i):
                    flush()
                    cond, j = group(s, i + len(kw) - 1)
                    stmt, j = group(s, skip_ws(s, j))
                    lines.append(pre + tex_inline(label + cond + ": " + alg_inline(stmt)))
                    i = j
                    handled = True
                    break
            if handled:
                continue
            if s.startswith("\\;", i):
                flush()
                i += 2
            elif s.startswith("\\KwIn{", i) or s.startswith("\\KwOut{", i):
                flush()
                kw = "Input: " if s.startswith("\\KwIn", i) else "Output: "
                c, j = group(s, s.index("{", i))
                lines.append(("" if False else "") + "- **" + kw.strip() + "** " + tex_inline(c))
                i = j
            elif s.startswith("\\tcp*", i):
                j = i + 5
                _, j = opt(s, j)
                c, j = group(s, j)
                buf += "  // " + c
                i = j
            else:
                buf += s[i]
                i += 1
    flush()


def conv_algorithm(content):
    mc = re.search(r"\\caption\{", content)
    cap, j = group(content, mc.end() - 1)
    lab = label_of(content)
    num = LAB.get(lab, "?")
    b = re.sub(r"\\label\{[^}]*\}", "", content[j:])
    lines = []
    alg_render(b, 0, lines)
    return f"\n\n**Algorithm {num}.** {tex_inline(cap)}\n\n" + "\n".join(lines) + "\n\n"


def conv_list(content, ordered):
    items = re.split(r"\\item\b", content)[1:]
    out = []
    for k, it in enumerate(items, 1):
        it = it.strip()
        mark = f"{k}. " if ordered else "- "
        lines = it.split("\n")
        first = lines[0]
        rest = ["   " if ordered else "  "]
        out.append(mark + first + "".join("\n" + ("  " if l.strip() else "") + l for l in lines[1:]))
    return "\n\n" + "\n".join(out) + "\n\n"


THM = {"proposition": "Proposition", "remark": "Remark", "lemma": "Lemma", "theorem": "Theorem",
       "corollary": "Corollary", "definition": "Definition"}


def process_envs(t):
    # order matters: innermost first is not needed because nested envs are rare except
    # itemize containing quote/equation, handled by stashing math before lists.
    for env in ("table", "figure", "algorithm"):
        while True:
            r = find_env(t, env)
            if not r:
                break
            s, e, o, c = r
            rep = conv_table(c, o) if env == "table" else conv_figure(c) if env == "figure" else conv_algorithm(c)
            t = t[:s] + stash(rep) + t[e:]
    for env in THM:
        while True:
            r = find_env(t, env)
            if not r:
                break
            s, e, o, c = r
            lab = label_of(c)
            c = re.sub(r"\\label\{[^}]*\}", "", c)
            num = LAB.get(lab, "")
            head = f"**{THM[env]}{' ' + num if num else ''}"
            head += f" ({o}).**" if o else ".**"
            t = t[:s] + "\n\n" + head + " " + c.strip() + "\n\n" + t[e:]
    while True:
        r = find_env(t, "proof")
        if not r:
            break
        s, e, o, c = r
        t = t[:s] + "\n\n*Proof.* " + c.strip() + " $\\square$\n\n" + t[e:]
    while True:
        r = find_env(t, "quote")
        if not r:
            break
        s, e, o, c = r
        c = c.replace("\\small", "").replace("\\ttfamily", "").strip()
        code = re.sub(r"\\\\\s*", "\n", c)
        code = text_pass(code, code_block=True)
        t = t[:s] + stash("\n\n```bash\n" + code.strip() + "\n```\n\n") + t[e:]
    return t


def math_blocks(t):
    for env in ("equation", "align*", "align"):
        while True:
            r = find_env(t, env)
            if not r:
                break
            s, e, o, c = r
            lab = label_of(c)
            c = re.sub(r"\\label\{[^}]*\}", "", c).strip()
            tag = f" \\tag{{{LAB[lab]}}}" if lab in LAB and env != "align*" else ""
            if env.startswith("align"):
                c = "\\begin{aligned}\n" + c + "\n\\end{aligned}"
            c = c.replace("\\code{", "\\texttt{")
            t = t[:s] + stash(f"\n\n$$\n{c}{tag}\n$$\n\n") + t[e:]
    t = re.sub(r"(?<!\\)\\\[(.*?)(?<!\\)\\\]", lambda m: stash("\n\n$$\n" + m.group(1).strip() + "\n$$\n\n"), t, flags=re.S)
    return t


# ---------------------------------------------------------------- text pass
def cite_replace(t):
    def one(m):
        kind, rest = m.group(1), m.group(2)
        i = 0
        o1, i = opt(rest, i)
        o2, i = opt(rest, i)
        keys = group(rest, i)[0] if rest[i:i + 1] == "{" else ""
        keys = [k.strip() for k in keys.split(",")]
        pre, post = (o1, o2) if o2 is not None else (None, o1)
        return None

    out, i = [], 0
    pat = re.compile(r"\\(citep|citet)(?![A-Za-z])")
    while True:
        m = pat.search(t, i)
        if not m:
            out.append(t[i:])
            break
        out.append(t[i:m.start()])
        j = m.end()
        o1, j = opt(t, j)
        o2, j = opt(t, j)
        keys, j = group(t, j)
        keys = [k.strip() for k in keys.split(",")]
        loc = (o2 if o2 is not None else o1)
        loc = loc.replace("~", " ") if loc else None
        if m.group(1) == "citep":
            parts = ["@" + k for k in keys]
            if loc:
                parts[-1] += ", " + loc
            out.append("[" + "; ".join(parts) + "]")
        else:
            s = "; ".join("@" + k for k in keys)
            if loc:
                s += " [" + loc + "]"
            out.append(s)
        i = j
    return "".join(out)


def brace_cmd(t, name, fn):
    out, i = [], 0
    pat = re.compile(r"\\" + name + r"(?![A-Za-z])")
    while True:
        m = pat.search(t, i)
        if not m:
            out.append(t[i:])
            break
        j = skip_ws(t, m.end())
        if j >= len(t) or t[j] != "{":
            out.append(t[i:m.end()])
            i = m.end()
            continue
        c, j2 = group(t, j)
        out.append(t[i:m.start()])
        out.append(fn(c))
        i = j2
    return "".join(out)


def code_span(c):
    c = c.replace("\\_", "_").replace("\\#", "#").replace("\\%", "%").replace("\\&", "&").replace("\\{", "{").replace("\\}", "}")
    c = c.replace("\\textasciitilde", "~").replace("~", " ").replace("\\$", "$")
    c = re.sub(r"\\textbackslash\s*", "\\\\", c)
    c = c.replace("$", "")
    return "`" + c.replace("`", "'") + "`"


def text_pass(t, code_block=False):
    # math spans protected
    math = []

    def keep(m):
        math.append(m.group(0))
        return f"\x01{len(math) - 1}\x01"

    t = re.sub(r"\$[^$]+\$", keep, t)
    if code_block:
        t = t.replace("\\_", "_").replace("\\%", "%").replace("\\&", "&").replace("\\{", "{").replace("\\}", "}")
        t = t.replace("-{}-", "--").replace("{-}", "").replace("\\textbackslash", "\\")
    t = cite_replace(t)
    t = brace_cmd(t, "code", code_span)
    t = brace_cmd(t, "nolinkurl", code_span)
    t = brace_cmd(t, "texttt", code_span)
    t = brace_cmd(t, "url", lambda c: "<" + c + ">")
    t = brace_cmd(t, "emph", lambda c: "*" + c + "*")
    t = brace_cmd(t, "textit", lambda c: "*" + c + "*")
    t = brace_cmd(t, "textbf", lambda c: "**" + c + "**")
    t = brace_cmd(t, "footnote", lambda c: " ^[" + c + "]")
    t = brace_cmd(t, "text", lambda c: c)
    t = brace_cmd(t, "textsuperscript", lambda c: c)
    # references
    t = re.sub(r"\\eqref\{([^}]+)\}", lambda m: "(" + LAB.get(m.group(1), "?") + ")", t)
    t = re.sub(r"\\ref\{([^}]+)\}", lambda m: LAB.get(m.group(1), "?"), t)
    # sectioning handled earlier; clean layout commands
    for c in ("noindent", "small", "footnotesize", "centering", "raggedright", "ttfamily", "itshape",
              "medskip", "smallskip", "bigskip", "clearpage", "newpage", "par", "scriptsize", "normalsize"):
        t = re.sub(r"\\" + c + r"(?![A-Za-z])\s*", "", t)
    t = re.sub(r"\\(vspace|hspace)\*?\{[^}]*\}", "", t)
    t = re.sub(r"\\label\{[^}]*\}", "", t)
    t = re.sub(r"\\setlength\{[^}]*\}\{[^}]*\}", "", t)
    t = t.replace("\\S", "§")
    t = t.replace("\\ ", " ").replace("\\,", " ").replace("\\;", " ").replace("\\!", "").replace("\\@", "")
    t = t.replace("---", "—").replace("--", "–")
    t = t.replace("``", "“").replace("''", "”")
    t = t.replace("\\textasciitilde", "\\~").replace("~", " ")
    t = t.replace("\\dots", "…").replace("\\ldots", "…")
    t = t.replace("\\&", "&")
    t = t.replace("\\\\", "\n")
    t = re.sub(r"\{\}", "", t) if not code_block else t
    t = t.replace("\\_", "\\_") if not code_block else t
    t = re.sub(r"\\textasciicircum", "^", t)
    t = t.replace("\\mbox", "")
    t = re.sub(r"\{(\[[^\]\n]*\])\}", r"\1", t)
    t = t.replace("\\quad", "")
    # leftover simple braces from LaTeX grouping like {=} {,}
    t = t.replace("{,}", ",").replace("{=}", "=")
    for i, mth in enumerate(math):
        mth = mth.replace("\\dfrac", "\\frac")
        t = t.replace(f"\x01{i}\x01", expand_math(mth))
    return t


def expand_math(m):
    # references inside math (e.g. \eqref) were handled before; ties inside math are harmless
    m = re.sub(r"\\eqref\{([^}]+)\}", lambda x: "(" + LAB.get(x.group(1), "?") + ")", m)
    m = m.replace("\\code{", "\\texttt{").replace("\\nolinkurl{", "\\texttt{")
    m = m.replace("\\lnot", "\\neg").replace("\\land", "\\wedge").replace("\\lor", "\\vee")
    m = m.replace("\\gets", "\\leftarrow")
    m = m.replace("\\bmod", "\\bmod")
    return m


# ---------------------------------------------------------------- sectioning
def build():
    t = body
    # abstract
    ab = re.search(r"\\begin\{abstract\}(.*?)\\end\{abstract\}", t, re.S)
    abstract = ab.group(1)
    t = t[:ab.start()] + "\x02ABSTRACT\x02" + t[ab.end():]
    t = math_blocks(t)
    t = process_envs(t)
    # itemize / enumerate (after math stashed) -- items may hold stubs
    for env, ordered in (("itemize", False), ("enumerate", True)):
        while True:
            r = find_env(t, env)
            if not r:
                break
            s, e, o, c = r
            t = t[:s] + conv_list(c, ordered) + t[e:]
    # sections
    sec, sub, app = [0], [0], [False]
    letters = "ABCDEFGHIJ"

    def sec_rep(m):
        kind = m.group(1)
        title, _ = group(t, m.end() - 1)
        return None

    out, i = [], 0
    pat = re.compile(r"\\(section|subsection|subsubsection|paragraph)\*?(?![A-Za-z])\s*\{")
    n_sec = 0
    n_sub = 0
    n_ssub = 0
    appendix = False
    while True:
        m = pat.search(t, i)
        if not m:
            out.append(t[i:])
            break
        out.append(t[i:m.start()])
        title, j = group(t, m.end() - 1)
        lab = ""
        mm = re.match(r"\s*\\label\{([^}]+)\}", t[j:])
        if mm:
            lab = mm.group(1)
            j += mm.end()
        kind = m.group(1)
        pre = t[:m.start()]
        if "\\APPENDIXSTART" in pre and not appendix:
            appendix = True
            n_sec = 0
        if kind == "section":
            n_sec += 1
            n_sub = 0
            num = letters[n_sec - 1] if appendix else str(n_sec)
            hd = f"# {'Appendix ' if appendix else ''}{num} {text_pass(title)}"
            cur = num
        elif kind == "subsection":
            n_sub += 1
            n_ssub = 0
            num = f"{cur}.{n_sub}"
            hd = f"## {num} {text_pass(title)}"
        elif kind == "subsubsection":
            n_ssub += 1
            num = f"{cur}.{n_sub}.{n_ssub}"
            hd = f"### {num} {text_pass(title)}"
        else:
            num = ""
            hd = f"**{text_pass(title)}**"
        if lab and kind != "paragraph":
            if LAB.get(lab) and LAB[lab] != num:
                print(f"NUMBER MISMATCH {lab}: aux {LAB[lab]} mine {num}", file=sys.stderr)
            hd += f" {{#{lab.replace('subsec:', 'sec:')}}}"
        out.append("\n\n" + hd + "\n\n")
        i = j
    t = "".join(out)
    t = t.replace("\\APPENDIXSTART", "")
    t = text_pass(t)
    t = unstash(t)
    # stubs were produced by text_pass of their own; but nested stubs inside list items resolved
    abs_md = text_pass(abstract)
    t = t.replace("\x02ABSTRACT\x02", "# Abstract {-}\n\n" + abs_md.strip() + "\n")
    t = unstash(t)
    # tidy
    t = re.sub(r"[ \t]+\n", "\n", t)
    t = re.sub(r"\n{3,}", "\n\n", t)
    return t.strip() + "\n"


md = build()
HEADER = """---
title: "STRATA: One Geometry-Free Persistence-and-Periodicity Engine for Selectable 2D/3D Lifelong LiDAR Mapping"
author:
  - name: Jungmo Kang
    affiliation: Independent Researcher, Seoul, Republic of Korea
    email: kangjmo91@gmail.com
    orcid: 0009-0002-0394-1861
    url: https://github.com/kjungmo/strata/tree/paper-v2
keywords:
  - lifelong mapping
  - occupancy grids
  - sequential probability ratio test
  - persistence filtering
  - FreMEn periodicity
  - ROS 2
---

> This Markdown file is the companion of the submitted manuscript, whose source is
> `paper/arxiv_ref/` (PDF: `paper/strata_paper.pdf`). It is generated from that source
> by `paper/arxiv_ref/tools/tex2md.py`: the text, numbers, claims and scope are the
> same, section and equation numbers are those of the PDF, figures are not reproduced
> (the PDF draws them), and citations are pandoc keys that `references.bib` resolves.
> Where the wording differs, the manuscript is authoritative.

"""
md = HEADER + md
OUT.write_text(md, encoding="utf-8")
print("written", OUT, len(md))
