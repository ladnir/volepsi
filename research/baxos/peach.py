import argparse,fcntl,hashlib,json,os,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('action',choices=['build','test','sanitize','bench','profile','dump']);p.add_argument('--exe',default='baseline');p.add_argument('--out',default='baseline');p.add_argument('--root',default='.');p.add_argument('--mode',default='both');p.add_argument('--trials',default=8,type=int);p.add_argument('--logn',default=20,type=int)
p.add_argument('--isa',choices=['native','avx2','scalar'],default='native')
a=p.parse_args();here=Path(__file__).resolve().parent
locks=[]
for name in ['volepsi-benchmark.lock','hypercat-global-benchmark.lock','cat-blst-benchmark.lock']:
    f=open('/tmp/'+name,'a');fcntl.flock(f,fcntl.LOCK_EX|fcntl.LOCK_NB);locks.append(f)
base=Path('/tmp/volepsi-chosenbaa30-20261115');env=dict(os.environ,LD_LIBRARY_PATH=str(base/'deps/lib'),UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
exe=here/a.exe
if a.action in ['build','test','sanitize']:
    flags=['-std=c++20','-march=native','-O3','-g1','-DNDEBUG','-pthread',
           '-DCRYPTOTOOLS_EDWARDS25519_ASM=1','-I'+str((here/a.root).resolve()),
           '-I'+str(base/'psi-build'),'-I'+str(base/'psi'),
           '-isystem',str(base/'install/include'),'-isystem',str(base/'deps/include')]
    libs=[str(base/'psi-build/volePSI/libvolePSI.a')]
    libs += [str(base/'install/lib'/x) for x in ['liblibOTe.a','libcryptoTools.a']]
    libs += [str(base/'deps/lib'/x) for x in ['libsodium.a','libcoproto.a','libmacoro.a','libbitpolymul.a']]
    if a.isa=='avx2': flags+=['-mno-avx512f']
    if a.isa=='scalar': flags+=['-mno-avx512f','-DVOLEPSI_PAXOS_FORCE_SCALAR']
    sources_to_compile=[str(here/'bench.cpp')]
    if a.action in ['test','sanitize']:
        sources_to_compile=[str(here/'tests.cpp'),str(here/'tests/Paxos_Tests.cpp')]
        flags+=['-UNDEBUG']
    if a.action=='sanitize': flags+=['-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer']
    command=['g++',*flags,*sources_to_compile,*libs,'-o',str(exe)]
    subprocess.run(command,env=env,check=True)
    sources={}
    snap=here/(a.exe+'-sources');snap.mkdir(exist_ok=True)
    for name in ['Paxos.h','PaxosImpl.h','PxUtil.h','PaxosGf128.h']:
        data=(here/a.root/'volePSI'/name).read_bytes();sources[name]=hashlib.sha256(data).hexdigest();(snap/name).write_bytes(data)
    for source in sources_to_compile:
        path=Path(source);data=path.read_bytes();sources[str(path.relative_to(here))]=hashlib.sha256(data).hexdigest();(snap/path.name).write_bytes(data)
    (here/(a.exe+'-build.json')).write_text(json.dumps(dict(command=command,sources=sources),indent=2))
    if a.action in ['test','sanitize']:
        r=subprocess.run([str(exe)],env=env,text=True,capture_output=True)
        (here/(a.exe+'-tests.txt')).write_text(r.stdout+r.stderr)
        print(r.stdout+r.stderr);r.check_returncode()
else:
    out=here/'results';out.mkdir(exist_ok=True)
    command=['taskset','-c','2',str(exe),str(a.logn),str(a.trials),a.mode]
    if a.action=='profile': command=['perf','record','-o',str(out/(a.out+'.data')),'--',*command]
    if a.action=='dump': command+= [str(out/(a.out+'.bin'))]
    r=subprocess.run(command,env=env,text=True,capture_output=True,check=True)
    (out/(a.out+'.csv')).write_text(r.stdout);(out/(a.out+'.stderr')).write_text(r.stderr)
    meta=dict(cpu=subprocess.check_output(['lscpu'],text=True),compiler=subprocess.check_output(['g++','--version'],text=True).splitlines()[0],frequency={name:Path('/sys/devices/system/cpu/cpu2/cpufreq',name).read_text().strip() for name in ['scaling_governor','scaling_min_freq','scaling_max_freq']},command=command,time_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),binary_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),build=json.loads((here/(a.exe+'-build.json')).read_text()))
    (out/(a.out+'.json')).write_text(json.dumps(meta,indent=2));print(r.stdout)
    if a.action=='profile':
        report=subprocess.check_output(['perf','report','-i',str(out/(a.out+'.data')),'--stdio','--no-children','--percent-limit','1'],text=True)
        (out/(a.out+'-profile.txt')).write_text(report);print(report[:14000])
