"""Conservative ROM overfitting bounds, separate from honest rank failure.

For m cells of L bits, a seed of s bits and q distinct label queries:
  Pr[at least t matches] <= 2^(s + L*m) binom(q,t) 2^(-L*t).
Use binom(q,t) <= (e*q/t)^t. The independent label oracle is essential.
This is a bound on the extraction game, not full protocol security.
"""
from decimal import Decimal, localcontext
import json


def log_bound(m, label_bits, query_bits, t, seed_bits=128):
    with localcontext() as c:
        c.prec = 80
        ln2 = Decimal(2).ln()
        return (Decimal(seed_bits + label_bits*m) + Decimal(t) *
                (Decimal(query_bits-label_bits) + (1-Decimal(t).ln())/ln2))


def cap(m, label_bits, query_bits, failure_bits=128, seed_bits=128):
    # All candidates beyond m are on the decreasing branch for L >= log2(q).
    assert label_bits >= query_bits
    lo, hi = m, max(m+1, 2*m)
    while log_bound(m,label_bits,query_bits,hi,seed_bits) > -failure_bits:
        hi *= 2
    while lo+1 < hi:
        mid=(lo+hi)//2
        if log_bound(m,label_bits,query_bits,mid,seed_bits) <= -failure_bits:
            hi=mid
        else:
            lo=mid
    assert log_bound(m,label_bits,query_bits,hi,seed_bits) <= -failure_bits
    return hi-1  # event is |X_eff| >= hi


def exact_small_check():
    from math import comb
    # Verify the binomial relaxation independently on small integers.
    with localcontext() as c:
        c.prec=80
        for m in [1,2,5]:
            for ell in [4,8,16]:
                for t in range(m+1,17):
                    exact=Decimal(ell*m-ell*t)+Decimal(comb(16,t)).ln()/Decimal(2).ln()
                    assert exact <= log_bound(m,ell,4,t,0)
        # A simple analytic certificate: with q<=2^128, L=256, s=128,
        # t=2m+2 gives s+L*m+(128-L)*t=-128 even without the factorial.
        for m in [1,2,64,1000,1310720,1411712]:
            assert log_bound(m,256,128,2*m+2,128) < -128


if __name__ == '__main__':
    exact_small_check()
    n=1<<20
    result=[]
    for backend,m in [('ribbon',1310720),('baxos',1411712)]:
        for q in [64,96,100,128]:
            for ell in [128,256,384,512,640,1024,1280]:
                bound=cap(m,ell,q)
                result.append(dict(backend=backend,n=n,m=m,label_bits=ell,
                    query_bits=q,seed_bits=128,failure_bits=128,extraction_cap=bound,
                    cap_over_m=bound/m,cap_over_n=bound/n,
                    log2_probability_upper_bound=str(log_bound(m,ell,q,bound+1))))
    print(json.dumps(result,indent=2))
