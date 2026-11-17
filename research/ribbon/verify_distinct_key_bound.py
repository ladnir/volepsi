"""Evaluate the Baxos ideal-cipher factorial bound; no AES security assumption test."""
from decimal import Decimal, localcontext
from fractions import Fraction
from math import comb, factorial
import json


def exponent(m,t,bits=128):
    with localcontext() as c:
        c.prec=80
        d=Decimal(t)
        return Decimal(bits*(m+1))-(d*d.ln()-d+1)/Decimal(2).ln()


def cap(m):
    lo,hi=1,max(2,2*m)
    # Extra bit of slack beyond the requested exponent -128.
    while exponent(m,hi)>-129: hi*=2
    while lo+1<hi:
        mid=(lo+hi)//2
        if exponent(m,mid)<=-129:hi=mid
        else:lo=mid
    return hi-1


def check_identities():
    for N in range(2,20):
        for t in range(1,N+1):
            falling=factorial(N)//factorial(N-t)
            assert Fraction(comb(N,t),falling)==Fraction(1,factorial(t))
    # Verify the numerical relaxation against exact integer factorials.
    with localcontext() as c:
        c.prec=80
        for t in range(1,300):
            lower=Decimal(t)*Decimal(t).ln()-Decimal(t)+1
            assert lower<=Decimal(factorial(t)).ln()
    # Enumerate a four-element permutation domain and every target map.
    # For fixed D and XOR labels, x+D(x) is just such a target map.
    from itertools import permutations,product
    N=4;perms=list(permutations(range(N)))
    for target in product(range(N),repeat=N):
        counts=[sum(a==b for a,b in zip(p,target)) for p in perms]
        for t in range(1,N+1):
            probability=Fraction(sum(v>=t for v in counts),factorial(N))
            assert probability<=min(Fraction(1),Fraction(1,factorial(t)))


if __name__=='__main__':
    check_identities()
    n=1<<20;m=1411712;bound=cap(m)
    print(json.dumps(dict(model='ideal cipher; all seeds except k; all tables and inputs',
        backend='baxos',n=n,m=m,cap=bound,cap_over_n=bound/n,cap_over_m=bound/m,
        log2_probability_upper_bound=str(exponent(m,bound+1)),
        validation='factorial identity, integral relaxation, exhaustive N=4 target maps'),indent=2))
