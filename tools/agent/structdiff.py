import json,subprocess,sys
unit,fn=sys.argv[1],sys.argv[2]
out=subprocess.run(['./build/tools/objdiff-cli','diff','-u',unit,fn,'-o','-','--format','json'],capture_output=True,text=True).stdout
d=json.loads(out)
def gs(side):
    for s in d[side]['symbols']:
        if s['name']==fn: return s['instructions']
L=gs('left');R=gs('right')
def f(x):
    i=x.get('instruction'); return i.get('formatted','') if i else '(gap)'
def mn(s): return s.split()[0] if s and s not in ('(gap)','(end)') else s
n=max(len(L),len(R))
print(f'{fn}: target={len(L)} base={len(R)}')
for k in range(n):
    l=f(L[k]) if k<len(L) else '(end)'
    r=f(R[k]) if k<len(R) else '(end)'
    # show where MNEMONICS differ (structural) with context
    if mn(l)!=mn(r):
        lo=max(0,k-3)
        for j in range(lo,min(n,k+4)):
            ll=f(L[j]) if j<len(L) else '(end)'
            rr=f(R[j]) if j<len(R) else '(end)'
            mark=' <== STRUCT' if mn(ll)!=mn(rr) else (' (op)' if ll!=rr else '')
            print(f'  {j:>3} T:{ll:<32} B:{rr:<32}{mark}')
        print('  ...')
