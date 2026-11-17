"""Build against the existing validated Peach tree, and benchmark serially."""
import argparse
import csv
import fcntl
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import time

p=argparse.ArgumentParser()
p.add_argument('action',choices=['build','test','bench','sanitize','differential','differential-sanitize'])
p.add_argument('--trials',type=int,default=6)
p.add_argument('--out',default='results')
p.add_argument('--quick',action='store_true')
mode=p.add_mutually_exclusive_group()
mode.add_argument('--sorted-only',action='store_true')
mode.add_argument('--shared-only',action='store_true')
p.add_argument('--ribbon-only',action='store_true')
p.add_argument('--dot',type=int,choices=[0,1,2],default=2)
p.add_argument('--exe',default='bench')
p.add_argument('--no-gfni',action='store_true',help='validate the existing AVX-512 fallback without GFNI/VBMI')
p.add_argument('--huge-pages',type=int,choices=[0,1],default=1)
p.add_argument('--forwarding',type=int,choices=[0,1],default=1)
p.add_argument('--baseline-exe',default='optimization-baseline/bench')
p.add_argument('--radix-bits',type=int,choices=[8,9,10,11],default=11)
p.add_argument('--batch',type=int,choices=[8,16,32],default=8)
p.add_argument('--direct-pivots',type=int,choices=[0,1],default=0)
p.add_argument('--wait-lock',type=int,default=0)
a=p.parse_args()
# Builds and correctness runs also avoid disturbing another timed job.
locks=[]
deadline=time.monotonic()+a.wait_lock
while True:
    try:
        for name in ['volepsi-benchmark.lock','hypercat-global-benchmark.lock','cat-blst-benchmark.lock']:
            lock=open('/tmp/'+name,'a');locks.append(lock)
            fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        break
    except BlockingIOError:
        for lock in locks: lock.close()
        locks=[]
        if time.monotonic()>=deadline: raise
        time.sleep(1)
here=Path(__file__).resolve().parent
base=Path('/tmp/volepsi-chosenbaa30-20261115')
exe=here/a.exe
flags=['-std=c++20','-march=native','-O3','-DNDEBUG','-pthread',
       '-DRIBBON_DOT='+str(a.dot),
       '-DRIBBON_HUGE_PAGES='+str(a.huge_pages),'-DRIBBON_FORWARDING='+str(a.forwarding),
       '-DRIBBON_RADIX_BITS='+str(a.radix_bits),
       '-DRIBBON_BATCH='+str(a.batch),
       '-DRIBBON_DIRECT_PIVOTS='+str(a.direct_pivots),
       '-DCRYPTOTOOLS_EDWARDS25519_ASM=1',
       '-I'+str(base/'psi-build'),'-I'+str(base/'psi'),
       '-isystem',str(base/'install/include'),'-isystem',str(base/'deps/include')]
if a.no_gfni: flags += ['-mno-gfni','-mno-avx512vbmi']
libs=[str(base/'psi-build/volePSI/libvolePSI.a')]
libs += [str(base/'install/lib'/x) for x in ['liblibOTe.a','libcryptoTools.a']]
libs += [str(base/'deps/lib'/x) for x in ['libsodium.a','libcoproto.a','libmacoro.a','libbitpolymul.a']]
env=dict(os.environ,LD_LIBRARY_PATH=str(base/'deps/lib'))
if a.action in ['build','sanitize','differential','differential-sanitize']:
    source=here/'bench.cpp'
    if a.action.startswith('differential'):
        reference=(here/'optimization-baseline/Ribbon.h').read_bytes()
        if hashlib.sha256(reference).hexdigest()!='e0316ff5fead517ae50630798795139739180460460f3e8c0ef0bfcaeb57fc03':
            raise RuntimeError('Unexpected baseline source')
        (here/'BaselineRibbon.h').write_text(reference.decode().replace('namespace ribbon {','namespace ribbon_baseline {'))
        source=here/'differential.cpp'
        exe=here/a.action
    if 'sanitize' in a.action:
        flags += ['-O1','-g1','-DRIBBON_TEST_ONLY','-fsanitize=address,undefined','-fno-omit-frame-pointer']
        if a.action=='sanitize': exe=here/'bench-sanitize'
    cmd=['g++',*flags,str(source),*libs,'-o',str(exe)]
    (here/(exe.name+'-command.json')).write_text(json.dumps(cmd,indent=2))
    subprocess.run(cmd,env=env,check=True)
    source_names=[source.name,'Ribbon.h']
    if a.action.startswith('differential'): source_names.append('BaselineRibbon.h')
    snapshot=here/(exe.name+'-sources');snapshot.mkdir(exist_ok=True)
    hashes={}
    for name in source_names:
        data=(here/name).read_bytes();(snapshot/name).write_bytes(data)
        hashes[name]=hashlib.sha256(data).hexdigest()
    (here/(exe.name+'-build.json')).write_text(json.dumps(dict(command=cmd,sources=hashes),indent=2))
    if a.action=='sanitize': subprocess.run([str(exe),'test'],env=env,check=True)
    if a.action.startswith('differential'): subprocess.run([str(exe)],env=env,check=True)
elif a.action=='test':
    subprocess.run([str(exe),'test'],env=env,check=True)
else:
    out=here/a.out;out.mkdir(exist_ok=True)
    meta=dict(time_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),
              binary_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
              compiler=subprocess.check_output(['g++','--version'],text=True).splitlines()[0],
              cpu=subprocess.check_output(['lscpu'],text=True),affinity=[2],trials=a.trials,
              input='fixed random keys/values; queries half members, half independent',
              encoder='fresh randomized free cells; fresh matrix seed each trial',
              sources=json.loads((here/(exe.name+'-build.json')).read_text())['sources'])
    meta['sorted_only']=a.sorted_only
    meta['shared_only']=a.shared_only
    meta['baseline_binary_sha256']=hashlib.sha256((here/a.baseline_exe).read_bytes()).hexdigest()
    meta['build_command']=json.loads((here/(exe.name+'-command.json')).read_text())
    meta['transparent_hugepages']=Path('/sys/kernel/mm/transparent_hugepage/enabled').read_text().strip()
    meta['cpu2_frequency']={key:Path('/sys/devices/system/cpu/cpu2/cpufreq',key).read_text().strip()
                           for key in ['scaling_governor','scaling_min_freq','scaling_max_freq','scaling_cur_freq']}
    (out/'metadata.json').write_text(json.dumps(meta,indent=2))
    summaries=[]
    profiles=[('baxos',1),(128,1.25),(112,1.3),(80,1.4)]
    if not a.quick: profiles += [(144,1.2),(192,1.15),(320,1.1),(640,1.05),(64,1.75)]
    profiles += [('baxos',1)]
    if a.ribbon_only: profiles=[p for p in profiles if p[0]!='baxos']
    for width,overhead in profiles:
        name=f'{width}-{overhead}'
        if (out/(name+'.csv')).exists(): name+='-repeat'
        target=here/a.baseline_exe if width=='baxos' else exe
        cmd=['taskset','-c','2',str(target),str(width),'20',str(overhead),str(a.trials)]
        if a.sorted_only: cmd.append('sorted-only')
        elif a.shared_only and width!='baxos': cmd.append('shared-only')
        print('START',name,flush=True)
        result=subprocess.run(cmd,env=env,text=True,capture_output=True,timeout=600)
        (out/(name+'.csv')).write_text(result.stdout)
        (out/(name+'.stderr')).write_text(result.stderr)
        if result.returncode: raise RuntimeError(result.stderr)
        rows=list(csv.DictReader(result.stdout.splitlines()))
        for kind,order in sorted({(r['kind'],r['order']) for r in rows}):
            group=[r for r in rows if r['kind']==kind and r['order']==order and int(r['trial'])>0]
            summary=dict(profile=name,kind=kind,order=order)
            for col in ['init_ms','start_hash_ms','sort_ms','coefficient_or_query_ms','eliminate_ms','backsolve_ms','total_ms']:
                summary[col]=statistics.median(float(r[col]) for r in group)
            summary['min_ms']=min(float(r['total_ms']) for r in group)
            summary['max_ms']=max(float(r['total_ms']) for r in group)
            summaries.append(summary)
            print(json.dumps(summary),flush=True)
        (out/'summary.json').write_text(json.dumps(summaries,indent=2))
    print('DONE: all benchmarks and correctness checks completed sequentially',flush=True)
