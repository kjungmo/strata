#!/usr/bin/env python3
"""Check that a saved map YAML and the image beside it belong to the same save.

`~/save_map` (grid2d) renames the PGM into place first and the YAML second. A crash
or failure between the two renames leaves the NEW image beside the PREVIOUS YAML,
and nav2_map_server loads that pair without complaint, with the old resolution and
origin. To make it detectable the YAML records the image it was written for:

    strata_image_bytes: <size of the PGM in bytes>
    strata_image_sha256: '<SHA-256 of the PGM, 64 hex digits>'

map_server ignores both keys and does not check them. Run this before trusting a
saved pair, in particular after an unclean shutdown. It
  * resolves `image:` as map_server does (a relative name is next to the YAML),
  * compares the image's size and SHA-256 with the YAML's two keys,
  * requires a binary PGM (P5) whose width x height x bytes per sample is exactly the
    pixel payload, with 0 < maxval < 65536,
  * requires a finite resolution > 0 and an origin of three finite numbers.

Exit status (one line is printed in every case):
  0  OK            the image is the one this YAML was written for, and both are well formed
  1  FAIL          do not use the pair: the image does not match the YAML, a file is
                   missing or unreadable, or the YAML or the PGM is malformed
  2                usage error (argparse)
  3  UNVERIFIABLE  the YAML has neither key (an older save, or another tool's); the
                   files are well formed but nothing ties this image to this YAML

The two keys tie the YAML to the image bytes only: two saves whose images are
byte-identical (say, the same map saved again under a different grid origin) cannot
be told apart.

Pure standard library (no ROS, no PyYAML), so it runs on any machine the files are
copied to. The YAML reader handles the flat mapping a map YAML is: one `key: value`
per line, quoted or plain scalars, `[a, b, c]` or `- a` lists, comments. Anything
else is reported as a FAIL, not guessed at.

Usage: check_saved_map.py MAP_YAML
"""
import argparse
import hashlib
import math
import os
import re
import sys

OK, FAIL, UNVERIFIABLE = 0, 1, 3
BYTES_KEY, SHA_KEY = 'strata_image_bytes', 'strata_image_sha256'
NUMBER = re.compile(r'[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?\Z')


class Bad(Exception):
    """The pair must not be used; the message says why."""


def scalar(text, where):
    """One YAML scalar: single-quoted, double-quoted or plain."""
    text = text.strip()
    if text[:1] == "'":
        body = text[1:-1]
        if len(text) < 2 or text[-1] != "'" or "'" in body.replace("''", ''):
            raise Bad(f'{where}: bad single-quoted value {text}')
        return body.replace("''", "'")
    if text[:1] == '"':
        if len(text) < 2 or text[-1] != '"' or re.search(r'(?<!\\)(\\\\)*"', text[1:-1]):
            raise Bad(f'{where}: bad double-quoted value {text}')
        return re.sub(r'\\(.)', lambda m: {'n': '\n', 't': '\t'}.get(m.group(1), m.group(1)), text[1:-1])
    return text


def strip_comment(line):
    """Drop a `# comment` (a # at the line start or after a space) outside quotes."""
    quote = None
    for i, c in enumerate(line):
        if quote:
            if c == quote:
                quote = None
        elif c in '\'"':
            quote = c
        elif c == '#' and (i == 0 or line[i - 1] in ' \t'):
            return line[:i]
    return line


def value(text, where):
    text = text.strip()
    if text.startswith('['):
        if not text.endswith(']'):
            raise Bad(f'{where}: list not closed on its line: {text}')
        inner = text[1:-1].strip()
        return [scalar(item, where) for item in inner.split(',')] if inner else []
    if text[:1] in '{|>&*!':
        raise Bad(f'{where}: unsupported YAML value {text}')
    return scalar(text, where)


def read_flat_yaml(path):
    """The top-level mapping of a map YAML, values as strings or lists of strings."""
    try:
        with open(path, encoding='utf-8') as fh:
            lines = fh.read().splitlines()
    except (OSError, UnicodeDecodeError) as e:
        raise Bad(f'cannot read the YAML: {e}')
    out, open_list = {}, None
    for number, raw in enumerate(lines, 1):
        where = f'line {number}'
        line = strip_comment(raw).rstrip()
        if not line.strip() or line.strip() in ('---', '...'):
            continue
        item = re.match(r'\s*-(\s+(.*))?\Z', line)
        if item:
            if open_list is None:
                raise Bad(f'{where}: list item outside a `key:` list')
            out[open_list].append(scalar(item.group(2) or '', where))
            continue
        if line[0] in ' \t':
            raise Bad(f'{where}: nested YAML is not a map YAML: {raw.strip()}')
        m = re.match(r'''('(?:[^']|'')*'|"[^"]*"|[^\s:'"][^:]*?)\s*:(\s+(.*))?\Z''', line)
        if not m:
            raise Bad(f'{where}: not a `key: value` line: {raw.strip()}')
        key = scalar(m.group(1), where)
        if key in out:
            raise Bad(f'{where}: key {key!r} appears twice')
        rest = (m.group(3) or '').strip()
        if rest:
            out[key], open_list = value(rest, where), None
        else:
            out[key], open_list = [], key   # a block list follows (or the value is empty)
    return out


def number(text, what):
    if not isinstance(text, str) or not NUMBER.match(text):
        raise Bad(f'{what} is not a number: {text!r}')
    v = float(text)
    if not math.isfinite(v):
        raise Bad(f'{what} is not finite: {text}')
    return v


def read_pgm(data, path):
    """Width, height and maxval of a binary PGM whose payload is exactly its pixels."""
    pos, fields = 0, []
    while len(fields) < 4:
        while pos < len(data) and (data[pos:pos + 1].isspace() or data[pos:pos + 1] == b'#'):
            if data[pos:pos + 1] == b'#':            # a comment runs to the end of its line
                end = data.find(b'\n', pos)
                pos = len(data) if end < 0 else end
            else:
                pos += 1
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace() and data[pos:pos + 1] != b'#':
            pos += 1
        if start == pos:
            raise Bad(f'{path}: the PGM header ends early (need P5, width, height, maxval)')
        fields.append(data[start:pos])
        if len(fields) == 1 and fields[0] != b'P5':
            raise Bad(f'{path}: not a binary PGM (starts with {data[:2]!r}, need b\'P5\')')
    if not all(f.isdigit() for f in fields[1:]):
        raise Bad(f'{path}: PGM width, height and maxval must be whole numbers, got '
                  f'{[f.decode("latin-1") for f in fields[1:]]}')
    width, height, maxval = (int(f) for f in fields[1:])
    if width <= 0 or height <= 0:
        raise Bad(f'{path}: PGM size {width} x {height} is not positive')
    if not 0 < maxval < 65536:
        raise Bad(f'{path}: PGM maxval {maxval} is outside 1..65535')
    if not data[pos:pos + 1].isspace():
        raise Bad(f'{path}: no whitespace byte between the PGM header and the pixels')
    sample = 1 if maxval < 256 else 2
    payload, want = len(data) - (pos + 1), width * height * sample
    if payload != want:
        raise Bad(f'{path}: PGM header says {width} x {height} ({want} pixel bytes) but the file '
                  f'holds {payload} after the header')
    return width, height, maxval


def check(yaml_path):
    """(exit status, one-line message) for one map YAML."""
    doc = read_flat_yaml(yaml_path)
    image = doc.get('image')
    if not isinstance(image, str) or not image:
        raise Bad('the YAML has no `image:`')
    # map_server: a name that does not start with '/' is relative to the YAML's directory.
    image_path = image if image.startswith('/') else os.path.join(os.path.dirname(yaml_path) or '.', image)
    if 'resolution' not in doc:
        raise Bad('the YAML has no `resolution:`')
    resolution = number(doc['resolution'], 'resolution')
    if resolution <= 0:
        raise Bad(f'resolution must be > 0, got {doc["resolution"]}')
    origin = doc.get('origin')
    if not isinstance(origin, list) or len(origin) != 3:
        raise Bad(f'origin must be a list of 3 numbers [x, y, yaw], got {origin!r}')
    origin = [number(v, f'origin[{i}]') for i, v in enumerate(origin)]
    try:
        with open(image_path, 'rb') as fh:
            data = fh.read()
    except OSError as e:
        raise Bad(f'cannot read the image the YAML names: {e}')

    recorded = [k for k in (BYTES_KEY, SHA_KEY) if k in doc]
    if len(recorded) == 1:
        raise Bad(f'the YAML has {recorded[0]} but not the other of {BYTES_KEY} / {SHA_KEY}')
    sha = hashlib.sha256(data).hexdigest()
    if recorded:
        want_bytes, want_sha = doc[BYTES_KEY], doc[SHA_KEY]
        if not isinstance(want_bytes, str) or not re.match(r'\d+\Z', want_bytes):
            raise Bad(f'{BYTES_KEY} is not a byte count: {want_bytes!r}')
        if not isinstance(want_sha, str) or not re.match(r'[0-9a-fA-F]{64}\Z', want_sha):
            raise Bad(f'{SHA_KEY} is not 64 hex digits: {want_sha!r}')
        if int(want_bytes) != len(data) or want_sha.lower() != sha:
            raise Bad(f'image does not match this YAML: saved by an interrupted save? '
                      f'({image_path} is {len(data)} bytes, sha256 {sha}; the YAML records '
                      f'{int(want_bytes)} bytes, sha256 {want_sha.lower()})')
    width, height, _ = read_pgm(data, image_path)
    what = (f'{image_path} {width} x {height}, {len(data)} bytes, sha256 {sha}; resolution {resolution:g}, '
            f'origin [{", ".join(format(v, ".17g") for v in origin)}]')
    if not recorded:
        return UNVERIFIABLE, (f'UNVERIFIABLE: {yaml_path}: no {BYTES_KEY} / {SHA_KEY} in the YAML (an older '
                              f'save, or another tool), so nothing ties this image to it; well formed: {what}')
    return OK, f'OK: {yaml_path}: the image is the one this YAML was saved with: {what}'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('yaml', metavar='MAP_YAML', help='the saved map YAML (its image is found as map_server finds it)')
    args = ap.parse_args()
    try:
        status, line = check(args.yaml)
    except Bad as e:
        status, line = FAIL, f'FAIL: {args.yaml}: {e}'
    print(line, file=sys.stdout if status == OK else sys.stderr)
    return status


if __name__ == '__main__':
    sys.exit(main())
