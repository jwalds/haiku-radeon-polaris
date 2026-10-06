#!/usr/bin/env python3
"""perfcheck: compares a test case's log with its performance floors.

  perfcheck.py <floors file> <case> <log>

glmark2 logs: the FPS of each scene ("[build] ...: FPS: 2678", also when
other output splits the line) and "score" (glmark2 Score). vkbench logs:
"fill" and "copy" (GB/s). Prints the metrics; exit 1 if one is below its
floor or missing, 0 otherwise (also when the case has no floors).
"""
import re
import sys


def floors_for(path, case):
    floors = []
    with open(path) as f:
        for line in f:
            line = line.split('#', 1)[0].split()
            if len(line) == 3 and line[0] == case:
                floors.append((line[1], float(line[2])))
    return floors


def metrics(log):
    found = {}
    # "[scene] <options>: FPS: n", lines of other output can come between
    for scene, fps in re.findall(r'\[(\w+)\] [^:\n]*:.*?FPS:\s*(\d+)', log,
            re.DOTALL):
        found[scene] = float(fps)
    score = re.search(r'glmark2 Score:\s*(\d+)', log)
    if score:
        found['score'] = float(score.group(1))
    for name, value in re.findall(r'^(fill|copy):.*?([\d.]+) GB/s', log,
            re.MULTILINE):
        found[name] = float(value)
    return found


def main():
    floors = floors_for(sys.argv[1], sys.argv[2])
    if not floors:
        return 0
    with open(sys.argv[3], errors='replace') as f:
        found = metrics(f.read())
    slow = []
    parts = []
    for name, floor in floors:
        value = found.get(name)
        if value is None:
            slow.append('%s missing' % name)
        elif value < floor:
            slow.append('%s %g < %g' % (name, value, floor))
        else:
            parts.append('%s %g' % (name, value))
    print(', '.join(slow) if slow else ', '.join(parts))
    return 1 if slow else 0


if __name__ == '__main__':
    sys.exit(main())
