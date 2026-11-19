# fixnames.py  usage: python fixnames.py profile.json.gz compress-profiling.exe out.json.gz
import gzip, json, os, subprocess, sys

prof_in, exe, prof_out = sys.argv[1:4]
IMAGE_BASE = 0x140000000   # check: objdump -p exe | Select-String ImageBase
exe_name = os.path.basename(exe).lower()

p = json.load(gzip.open(prof_in, 'rt', encoding='utf-8'))

# newer layout: tables in p['shared']; older layout: tables inside each thread
containers = [p['shared']] if 'shared' in p else p.get('threads', [])
if not containers or 'funcTable' not in containers[0]:
    print("Unrecognized layout. Top-level keys:", list(p.keys()))
    sys.exit(1)

# frameTable lives next to funcTable in both layouts
total = 0
for c in containers:
    sa = c.get('stringArray') or c.get('stringTable')
    ft, rt, fr = c['funcTable'], c['resourceTable'], c['frameTable']
    exe_res = {i for i in range(rt['length']) if sa[rt['name'][i]].lower() == exe_name}
    idx = [i for i in range(ft['length'])
           if ft['resource'][i] in exe_res and sa[ft['name'][i]].startswith('0x')]
    if not idx:
        continue
    addrs = [hex(IMAGE_BASE + int(sa[ft['name'][i]], 16)) for i in idx]
    out = subprocess.run(['addr2line', '-f', '-C', '-e', exe] + addrs,
                         capture_output=True, text=True).stdout.splitlines()
    canon = {}      # resolved name -> one func index, so each function appears once
    remap = {}      # old func index -> canonical func index
    for n, i in enumerate(idx):
        name = out[2 * n] if 2 * n < len(out) else '??'
        if name == '??':
            continue
        if name not in canon:
            sa.append(name)
            ft['name'][i] = len(sa) - 1
            canon[name] = i
        remap[i] = canon[name]
        total += 1
    fr['func'] = [remap.get(f, f) for f in fr['func']]

if 'meta' in p:
    p['meta']['symbolicated'] = True   # stop the viewer from re-symbolicating over our names
print(f"renamed {total} function addresses")
json.dump(p, gzip.open(prof_out, 'wt', encoding='utf-8'))