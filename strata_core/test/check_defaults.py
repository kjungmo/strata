#!/usr/bin/env python3
"""The shipped defaults agree in every place that states them (strata_core 0.2.0).

  usage: python3 -I check_defaults.py <repository root>

Checks, with no ROS and no YAML library:
  1. every key that both LayeredMapParams / PeriodicityParams (layered_map.hpp, periodicity.hpp)
     and a shipped params YAML state has the same value in both ("struct defaults == shipped YAML");
  2. the evidence keys of both YAMLs (evidence_model, m0, m1, alpha, beta, n_max_undecided) equal
     the values in paper/experiments/realdata/PREREG.json for that backend: the frozen value if the
     backend's status is "FROZEN", the conditional value (provisional) otherwise; the 3D backend
     falls back to the 2D pair while its own status is not frozen;
  3. evidence_calibration says what is true: grid2d "measured_2d", voxel3d "fallback_2d_pair" while
     the 3D status is not frozen;
  4. the node declares each evidence parameter from the library default (def.m0 ...), not from a
     second literal, and reads evidence_calibration.
Exits 1 and prints every difference when one is found.
"""
import json
import re
import sys
from pathlib import Path


def struct_defaults(text):
    out = {}
    for m in re.finditer(r'\b(?:double|int|bool)\s+(\w+)\s*\{([^}]*)\}', text):
        out[m.group(1)] = m.group(2).strip()
    return out


def yaml_flat(path):
    out = {}
    for line in path.read_text().splitlines():
        m = re.match(r'^\s{4}(\w+):\s*("[^"]*"|[^\s#]+)\s*(#.*)?$', line)
        if m:
            out[m.group(1)] = m.group(2)
    return out


def num(v):
    v = v.strip().strip('"')
    if v in ('true', 'false'):
        return v == 'true'
    try:
        return float(v)
    except ValueError:
        return v


def main(root):
    root = Path(root)
    problems = []
    text = (root / 'strata_core/include/strata_core/layered_map.hpp').read_text()
    body = re.search(r'struct LayeredMapParams \{(.*?)\n\};', text, re.S)
    if body is None:
        print('FAIL: cannot find struct LayeredMapParams in layered_map.hpp')
        return 1
    hdr = struct_defaults(body.group(1))
    hdr.update(struct_defaults((root / 'strata_core/include/strata_core/periodicity.hpp').read_text()))
    prereg = json.loads((root / 'paper/experiments/realdata/PREREG.json').read_text())
    yamls = {b: yaml_flat(root / f'strata/params/{b}.yaml') for b in ('grid2d', 'voxel3d')}

    # 1. struct defaults == YAML, for every key both state (the evidence_* keys are strings in the YAML)
    shared = 0
    for b, y in yamls.items():
        for k, v in y.items():
            if k in hdr and k not in ('evidence_model',):
                shared += 1
                if num(hdr[k]) != num(v):
                    problems.append(f'{b}.yaml {k}: {v} but the library default is {hdr[k]}')
    if shared < 2 * 15:
        problems.append(f'only {shared} keys compared with the library defaults; expected at least 30 (check the parser)')
    if 'EvidenceModel::Sprt' not in re.search(r'EvidenceModel evidence_model\{([^}]*)\}',
                                              (root / 'strata_core/include/strata_core/layered_map.hpp').read_text()).group(1):
        problems.append('the library default evidence_model is not Sprt')
    for b, y in yamls.items():
        if y.get('evidence_model', '').strip('"') != 'sprt':
            problems.append(f'{b}.yaml evidence_model is {y.get("evidence_model")}, shipped default is "sprt"')

    # 2. YAML == PREREG.json (frozen, else conditional); the 3D backend falls back to the 2D pair
    def source(backend):
        d = prereg[backend]
        if d.get('status', '').startswith('FROZEN') and 'frozen' in d:
            return d['frozen'], 'frozen'
        return None, 'not frozen'
    g2, g2s = source('grid2d')
    if g2 is None:
        g2 = prereg['grid2d']['conditional']
    g3, g3s = source('voxel3d')
    if g3 is None:
        g3 = g2   # the fallback is the 2D pair
    for b, vals, st in (('grid2d', g2, g2s), ('voxel3d', g3, g3s)):
        y = yamls[b]
        for k in ('m0', 'm1', 'alpha', 'beta', 'n_max_undecided'):
            if k not in vals:
                problems.append(f'PREREG.json has no {k} for {b}')
            elif num(y.get(k, 'missing')) != float(vals[k]):
                problems.append(f'{b}.yaml {k}: {y.get(k)} but PREREG.json ({st}) says {vals[k]}')
    # the library defaults are the 2D pair
    for k in ('m0', 'm1', 'alpha', 'beta', 'n_max_undecided'):
        if num(hdr[k]) != float(g2[k]):
            problems.append(f'library default {k}: {hdr[k]} but PREREG.json grid2d says {g2[k]}')

    # 3. calibration label
    if yamls['grid2d'].get('evidence_calibration', '').strip('"') != 'measured_2d':
        problems.append('grid2d.yaml evidence_calibration must be "measured_2d"')
    want3 = 'fallback_2d_pair' if g3s == 'not frozen' else 'measured_3d'
    if yamls['voxel3d'].get('evidence_calibration', '').strip('"') != want3:
        problems.append(f'voxel3d.yaml evidence_calibration must be "{want3}" while the 3D status is {prereg["voxel3d"]["status"]!r}')

    # 4. the node declares from the library defaults
    node = (root / 'strata/src/mapping_node.cpp').read_text()
    for k in ('m0', 'm1', 'alpha', 'beta', 'n_max_undecided'):
        if not re.search(r'declare_parameter<\w+>\("%s",\s*def\.%s\)' % (k, k), node):
            problems.append(f'mapping_node.cpp does not declare "{k}" from the library default def.{k}')
    if 'declare_parameter<std::string>("evidence_model", "sprt")' not in node:
        problems.append('mapping_node.cpp does not declare evidence_model with the default "sprt"')

    if problems:
        print('FAIL: the shipped defaults disagree:')
        for p in problems:
            print('  -', p)
        return 1
    print(f'OK: {shared} YAML keys equal the library defaults; evidence keys equal PREREG.json '
          f'(grid2d {g2s}, voxel3d {g3s}, falls back to the 2D pair); node declares from the library defaults')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
