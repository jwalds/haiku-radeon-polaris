#!/usr/bin/env python3
"""soak-report: the verdict of a soak.sh run.

  soak-report.py <results directory>

- idle samples (no client) after the warm-up and at the end: GPU memory
  in use must be the same, the server's semaphores, ports, threads and
  areas must not have grown (except empty heap areas: malloc keeps the
  address space of a peak), its memory not by more than 8 MB, the system's semaphores and ports not by more
  than 20
- trends while running: growth per hour over the second half
- client runs and failures, server warnings by kind
- checkpoints (load paused): idle GPU memory must stay the same, the
  benchmark alone (glmark2 scenes, vkbench) must reach 95% of the first
  checkpoint at the last; idle memory trends are listed
- glmark2 under load per scene, medians of the first and last quarter
  (information only: the other clients make it noisy)
Exit code 1 if anything failed.
"""
import collections
import csv
import os
import re
import sys


def slope_per_hour(points):
    if len(points) < 3:
        return 0.0
    n = len(points)
    mx = sum(x for x, _ in points) / n
    my = sum(y for _, y in points) / n
    sxx = sum((x - mx) ** 2 for x, _ in points)
    if sxx == 0:
        return 0.0
    return sum((x - mx) * (y - my) for x, y in points) / sxx * 3600


# columns after the name in listarea, listsem, listport and ps -a
TRAILING = {'areas': 8, 'sems': 1, 'ports': 2, 'threads': 5}


def names(path, kind):
    """the names in a listarea/listsem/listport/ps -a dump, numbers
    replaced, counted"""
    counter = collections.Counter()
    if not os.path.exists(path):
        return counter
    trailing = TRAILING[kind]
    in_threads = False
    with open(path, errors='replace') as f:
        for line in f:
            fields = line.split()
            if kind == 'threads':
                # thread lines follow the "Thread  Id  State ..." header
                if fields[:2] == ['Thread', 'Id']:
                    in_threads = True
                    continue
                if not in_threads or len(fields) <= trailing \
                        or not fields[-trailing].isdigit():
                    continue
                name = ' '.join(fields[:-trailing])
            else:
                if len(fields) <= trailing + 1 or not fields[0].isdigit():
                    continue
                name = ' '.join(fields[1:-trailing])
            counter[re.sub(r'\d+', 'N', name)] += 1
    return counter


def heap_high_water(out, idle):
    """the server's areas grew only by heap areas while its memory didn't:
    malloc keeps the address space of a peak"""
    before = names(os.path.join(out, 'idle-start-areas.txt'), 'areas')
    after = names(os.path.join(out, 'idle-end-areas.txt'), 'areas')
    added = after - before
    alloc = int(idle['idle-end']['server_alloc_kb']) \
        - int(idle['idle-start']['server_alloc_kb'])
    return set(added) == {'heap area'} and alloc <= 8192


def median(values):
    values = sorted(values)
    n = len(values)
    return values[n // 2] if n % 2 else (values[n // 2 - 1] + values[n // 2]) / 2


def glmark2_series(path):
    """{scene: [fps, ...]} in run order, without values after a lost device"""
    series = collections.defaultdict(list)
    if not os.path.exists(path):
        return series
    with open(path, errors='replace') as f:
        text = f.read()
    for segment in text.split('=== glmark2 start'):
        lost = segment.find('DEVICE LOST')
        if lost >= 0:
            segment = segment[:lost]
        for scene, fps in re.findall(r'\[(\w+)\] [^:\n]*:.*?FPS:\s*(\d+)',
                segment, re.DOTALL):
            series[scene].append(float(fps))
    return series


def main():
    out = sys.argv[1]
    problems = []
    with open(os.path.join(out, 'samples.csv')) as f:
        rows = list(csv.DictReader(f))
    idle = {row['label']: row for row in rows if row['label'].startswith('idle')}
    running = [row for row in rows if row['label'] == 'run']
    duration = int(rows[-1]['elapsed']) if rows else 0
    print('soak run: %s, %.1f hours, %d samples' % (os.path.basename(out),
        duration / 3600, len(rows)))

    # idle comparison
    print('\nidle before / after:')
    rules = [
        ('vram', 0, 'bytes'), ('vis_vram', 0, 'bytes'), ('gtt', 0, 'bytes'),
        ('server_sems', 0, ''), ('server_ports', 0, ''),
        ('server_threads', 0, ''), ('server_areas', 0, ''),
        ('server_alloc_kb', 8192, 'KB'), ('sys_sems', 20, ''),
        ('sys_ports', 20, ''), ('sys_used_kb', None, 'KB'),
        ('app_server_kb', None, 'KB'), ('kernel_kb', None, 'KB'),
    ]
    if 'idle-start' in idle and 'idle-end' in idle:
        for name, allowed, unit in rules:
            try:
                before = int(idle['idle-start'][name])
                after = int(idle['idle-end'][name])
            except (KeyError, TypeError, ValueError):
                print('  %-16s missing' % name)
                problems.append('%s not sampled' % name)
                continue
            verdict = ''
            if allowed is not None and after - before > allowed:
                if name == 'server_areas' and heap_high_water(out, idle):
                    verdict = '  heap high-water mark (empty heap areas)'
                else:
                    verdict = '  LEAK'
                    problems.append('%s grew by %d %s' % (name,
                        after - before, unit))
            print('  %-16s %12d %12d  %+d %s%s' % (name, before, after,
                after - before, unit, verdict))
    else:
        problems.append('idle samples missing')

    # what was added, by name (numbers in names ignored)
    for kind in ('threads', 'areas', 'sems', 'ports'):
        before = names(os.path.join(out, 'idle-start-%s.txt' % kind), kind)
        after = names(os.path.join(out, 'idle-end-%s.txt' % kind), kind)
        added = after - before
        removed = before - after
        if added or removed:
            print('  %s: %s%s' % (kind,
                ('added ' + ', '.join('%s x%d' % (n, c) for n, c in
                    sorted(added.items()))) if added else '',
                ('; gone ' + ', '.join('%s x%d' % (n, c) for n, c in
                    sorted(removed.items()))) if removed else ''))

    # trends
    if running:
        half = [row for row in running
            if int(row['elapsed']) >= duration / 2]
        print('\ntrend over the second half (per hour):')
        for name in ('server_alloc_kb', 'server_areas', 'server_sems',
                'server_ports', 'server_threads', 'vram', 'gtt',
                'sys_used_kb', 'sys_sems', 'sys_ports'):
            points = []
            for row in half:
                try:
                    points.append((int(row['elapsed']), int(row[name])))
                except ValueError:
                    pass
            values = [y for _, y in points]
            if values:
                print('  %-16s %+12.0f   (min %d, max %d)' % (name,
                    slope_per_hour(points), min(values), max(values)))

    # client runs
    runs = collections.Counter()
    failures = collections.defaultdict(list)
    with open(os.path.join(out, 'events.log')) as f:
        for line in f:
            parts = line.split()
            if len(parts) < 5:
                continue
            runs[parts[2]] += 1
            if parts[3] != '0':
                failures[parts[2]].append('%s (exit %s)' % (parts[0], parts[3]))
    print('\nclient runs: %d' % sum(runs.values()))
    for name in sorted(runs):
        failed = failures.get(name, [])
        print('  %-16s %5d  %s' % (name, runs[name],
            'failed: ' + ', '.join(failed[:5]) + (' ...' if len(failed) > 5
                else '') if failed else 'ok'))
        if failed:
            problems.append('%s failed %d times' % (name, len(failed)))

    # server warnings by kind
    kinds = collections.Counter()
    with open(os.path.join(out, 'server.log'), errors='replace') as f:
        for line in f:
            if line.startswith('[!]'):
                kinds[re.sub(r'0x[0-9a-f]+|\d+', 'N', line.strip())] += 1
    print('\nserver warnings: %d' % sum(kinds.values()))
    for kind, count in kinds.most_common(12):
        print('  %5d  %s' % (count, kind[:100]))

    # checkpoints: idle memory and the benchmark alone, first vs last
    checkpoints = sorted((row for row in rows
        if re.match(r'idle-\d+$', row['label'])),
        key=lambda row: int(row['label'][5:]))
    if len(checkpoints) >= 2:
        print('\nidle at the checkpoints (%d):' % len(checkpoints))
        for name in ('server_alloc_kb', 'server_areas', 'vram', 'gtt',
                'sys_used_kb', 'app_server_kb', 'kernel_kb'):
            try:
                values = [int(row[name]) for row in checkpoints]
            except (KeyError, TypeError, ValueError):
                continue
            # rising at every checkpoint by more than noise (1 MB for KB
            # values, one area)
            noise = 1 if name == 'server_areas' else 1024 \
                if name.endswith('_kb') else 0
            rising = all(b >= a for a, b in zip(values, values[1:])) \
                and values[-1] - values[0] > noise
            print('  %-16s %s%s' % (name, ' '.join(str(v) for v in values),
                '  (rising at every checkpoint)' if rising else ''))
            if name in ('vram', 'gtt') and values[-1] != values[0]:
                problems.append('idle %s %d -> %d' % (name, values[0],
                    values[-1]))
    bench = collections.defaultdict(list)
    for n in range(0, 1000):
        glmark2 = os.path.join(out, 'checkpoints', '%d-glmark2.log' % n)
        vkbench = os.path.join(out, 'checkpoints', '%d-vkbench.log' % n)
        if not os.path.exists(glmark2):
            break
        with open(glmark2, errors='replace') as f:
            for scene, fps in re.findall(r'\[(\w+)\] [^:\n]*:.*?FPS:\s*(\d+)',
                    f.read(), re.DOTALL):
                bench[scene].append(float(fps))
        if os.path.exists(vkbench):
            with open(vkbench, errors='replace') as f:
                for name, value in re.findall(r'^(fill|copy):.*?([\d.]+) GB/s',
                        f.read(), re.MULTILINE):
                    bench['vkbench ' + name].append(float(value))
    if bench:
        print('\nbenchmark alone at the checkpoints:')
        for name, values in bench.items():
            verdict = ''
            if len(values) >= 2 and values[-1] < values[0] * 0.95:
                verdict = '  DRIFT'
                problems.append('%s: %g -> %g' % (name, values[0], values[-1]))
            print('  %-14s %s%s' % (name, ' '.join('%g' % v for v in values),
                verdict))

    # glmark2 under load (information: the other clients make it noisy)
    series = glmark2_series(os.path.join(out, 'glmark2.log'))
    if series:
        print('\nglmark2 FPS under load, median of the first / last quarter:')
        for scene, values in series.items():
            if len(values) < 8:
                print('  %-10s only %d values' % (scene, len(values)))
                continue
            quarter = len(values) // 4
            first = median(values[:quarter])
            last = median(values[-quarter:])
            print('  %-10s %8.0f %8.0f  (%d values, min %.0f)' % (scene,
                first, last, len(values), min(values)))

    print('\n%s' % ('FAILED: ' + '; '.join(problems) if problems
        else 'no leaks, drift or failures found'))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
