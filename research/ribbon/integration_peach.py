"""Run in an isolated source/build tree on Peach; every action takes all locks."""
import argparse, concurrent.futures, fcntl, hashlib, json, os, shutil, subprocess, time
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('action',choices=['build','test','bench','params','sanitize','avx2','portable','regression','decode-build','decode-pages'])
p.add_argument('--backend',choices=['baxos','ribbon'],default='ribbon')
p.add_argument('--logn',type=int,default=20)
p.add_argument('--trials',type=int,default=8)
p.add_argument('--malicious',type=int,default=0)
p.add_argument('--name',default='')
a=p.parse_args()
root=Path('/tmp/volepsi-ribbon-integration-20261115')
src=root/'src';base=Path('/tmp/volepsi-chosenbaa30-20261115')
locks=[]
for name in ['volepsi-benchmark.lock','hypercat-global-benchmark.lock','cat-blst-benchmark.lock']:
    f=open('/tmp/'+name,'a');fcntl.flock(f,fcntl.LOCK_EX|fcntl.LOCK_NB);locks.append(f)
env=dict(os.environ,LD_LIBRARY_PATH=str(base/'deps/lib'),UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
results=root/'results';results.mkdir(exist_ok=True)
name=a.name or a.action
exe=root/('integration-decode' if a.action in ['decode-build','decode-pages'] else 'integration')
if a.action in ['build','sanitize','avx2','portable','decode-build']:
    flags=['-std=c++20','-march=native','-O3','-g1','-DNDEBUG','-pthread','-DCRYPTOTOOLS_EDWARDS25519_ASM=1',
           '-I'+str(src),'-I'+str(root/'build'),'-isystem',str(base/'install/include'),'-isystem',str(base/'deps/include')]
    if (root/'hardening-include/libOTe/Base/McRosRoy.h').exists():
        flags=['-I'+str(root/'hardening-include'),*flags]
    sources=[str(src/('research/ribbon/integration_decode.cpp' if a.action=='decode-build' else 'research/ribbon/integration.cpp'))]
    libs=[str(root/'build/tests/libvolePSI_Tests.a'),str(root/'build/volePSI/libvolePSI.a')]
    if a.action=='sanitize':
        # Recompile every affected translation unit, not just the harness.
        sources += [str(src/x) for x in ['tests/Ribbon_Tests.cpp','tests/RsOprf_Tests.cpp','volePSI/RibbonOkvs.cpp','volePSI/RsOprf.cpp','volePSI/RsPsi.cpp']]
        flags+=['-UNDEBUG']
        exe=root/('integration-'+a.action)
    elif a.action in ['avx2','portable']:
        sources += [str(src/'volePSI/RibbonOkvs.cpp')]
        exe=root/('integration-'+a.action)
    if a.action=='sanitize': flags+=['-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer']
    if a.action=='avx2': flags+=['-mno-avx512f']
    if a.action=='portable': flags+=['-mno-avx512f','-DVOLEPSI_RIBBON_PORTABLE']
    libs += [str(base/'install/lib'/x) for x in ['liblibOTe.a','libcryptoTools.a']]
    libs += [str(base/'deps/lib'/x) for x in ['libsodium.a','libcoproto.a','libmacoro.a','libbitpolymul.a']]
    if a.action=='sanitize':
        # Existing macoro bug: an object-independent helper is called before
        # the base constructor. Fix only the isolated include overlay; keep
        # vptr checking enabled and preserve the exact patch in the results.
        overlay=root/'sanitize-include';rel=Path('macoro/detail/when_all_awaitable.h')
        shutil.copytree(base/'deps/include/macoro',overlay/'macoro',dirs_exist_ok=True)
        old=(base/'deps/include'/rel).read_text()
        new=old.replace('size_t tasks_size(TASK_CONTAINER& tasks)','static size_t tasks_size(TASK_CONTAINER& tasks)')
        assert new!=old
        (overlay/rel).parent.mkdir(parents=True,exist_ok=True);(overlay/rel).write_text(new)
        import difflib
        (results/'macoro-static-helper.patch').write_text(''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/'+str(rel),tofile='b/'+str(rel))))
        # InlinePoly also downcasts raw storage before constructing the model.
        # Construct in storage first and use the placement-new result.
        shutil.copytree(base/'deps/include/coproto',overlay/'coproto',dirs_exist_ok=True)
        rel=Path('coproto/Common/InlinePoly.h');old=(base/'deps/include'/rel).read_text()
        new=old.replace('ModelController<U>* ptr = (ModelController<U>*) & getController();\n\t\t\t\tnew (ptr) ModelController<U>(std::forward<Args>(args)...);',
                        'auto* ptr = new (static_cast<void*>(&mStorage)) ModelController<U>(std::forward<Args>(args)...);')
        assert new!=old
        (overlay/rel).write_text(new)
        (results/'coproto-storage-construction.patch').write_text(''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/'+str(rel),tofile='b/'+str(rel))))
        flags=['-I'+str(overlay),*flags]
    objects=root/(name+'-objects');objects.mkdir(exist_ok=True)
    commands=[]
    for i,source in enumerate(sources):
        commands.append(['g++',*flags,'-c',source,'-o',str(objects/(str(i)+'.o'))])
    def compile_one(pair):
        i,command=pair
        with (objects/(str(i)+'.log')).open('w') as f: subprocess.run(command,env=env,stdout=f,stderr=f,check=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(compile_one,enumerate(commands)))
    command=['g++',*flags,*[str(objects/(str(i)+'.o')) for i in range(len(sources))],*libs,'-o',str(exe)]
    with (results/(name+'-build.log')).open('w') as f: subprocess.run(command,env=env,stdout=f,stderr=f,check=True)
    command=commands+[command]
    meta={'command':command,'sources':{str(x.relative_to(src)):hashlib.sha256(x.read_bytes()).hexdigest() for x in src.rglob('*') if x.is_file() and x.suffix in ['.cpp','.h']}}
    if (root/'hardening-include/libOTe/Base/McRosRoy.h').exists():
        meta['mrr_header_sha256']=hashlib.sha256((root/'hardening-include/libOTe/Base/McRosRoy.h').read_bytes()).hexdigest()
    (results/(name+'-build.json')).write_text(json.dumps(meta,indent=2))
    if a.action in ['build','decode-build']: raise SystemExit(0)
    command=[str(exe),'test']
elif a.action=='regression':
    exe=root/'build/frontend/frontend';command=[str(exe),'-u']
else:
    command=['taskset','-c','2',str(exe)]
    if a.action=='bench': command += [a.backend,str(a.logn),str(a.trials),str(a.malicious)]
    elif a.action!='decode-pages': command += [a.action]
r=subprocess.run(command,env=env,text=True,capture_output=True,timeout=600)
(results/(name+'.txt')).write_text(r.stdout+r.stderr)
meta={'command':command,'utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
      'cpu':subprocess.check_output(['lscpu'],text=True),'compiler':subprocess.check_output(['g++','--version'],text=True).splitlines()[0],
      'frequency':{x:Path('/sys/devices/system/cpu/cpu2/cpufreq',x).read_text().strip() for x in ['scaling_governor','scaling_min_freq','scaling_max_freq']}}
(results/(name+'.json')).write_text(json.dumps(meta,indent=2))
print(r.stdout+r.stderr);r.check_returncode()
