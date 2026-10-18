import os, sys, csv, math
import numpy as np
D = '/home/cona/datasets/strata/boreas'
for sid in ['boreas-2020-11-26-13-58', 'boreas-2021-04-08-12-44', 'boreas-2021-06-17-17-52']:
    fr = sorted(f for f in os.listdir(f'{D}/{sid}/lidar') if f.endswith('.bin'))
    t0 = int(fr[0][:-4]) * 1e-6 - 0.5; t1 = int(fr[-1][:-4]) * 1e-6 + 0.5
    rows = []
    with open(f'{D}/{sid}/applanix/gps_post_process.csv') as f:
        rd = csv.reader(f); h = next(rd); ix = {n: i for i, n in enumerate(h)}
        for r in rd:
            t = float(r[ix['GPSTime']])
            if t > t1: break
            if t < t0: continue
            rows.append([float(r[ix[k]]) for k in ['GPSTime', 'vel_east', 'vel_north', 'heading', 'angvel_z']])
    a = np.array(rows)
    sp = np.hypot(a[:, 1], a[:, 2]); m = sp > 3
    vd = np.degrees(np.arctan2(a[:, 2], a[:, 1]))[m]; hd = np.degrees(a[:, 3])[m]
    s = (vd + hd + 180) % 360 - 180            # velocity dir + heading (expected const 88)
    d = (vd - hd + 180) % 360 - 180
    dh = np.gradient(np.unwrap(a[:, 3]), a[:, 0]); c = np.corrcoef(dh[m], a[m, 4])[0, 1]
    print(sid, 'rows', len(a), 'moving', m.sum(), 'vd+h p10/50/90', np.percentile(s, [10, 50, 90]).round(2),
          'vd-h range', d.min().round(1), d.max().round(1), 'corr(dh/dt, angvel_z)', round(c, 5))
