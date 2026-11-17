"""Independently recompute the selector's exported witnesses at 80 digits."""
import csv, decimal, json, sys
from decimal import Decimal as D

decimal.getcontext().prec=80
ln2=D(2).ln()
checked=0
minimum_margin=None
for row in csv.DictReader(open(sys.argv[1])):
    n,m,w,ssp=(D(row[x]) for x in ['n','columns','width','ssp'])
    rho,theta=D(row['rho']),D(row['theta'])
    if rho==0:
        assert m==w and w>n
        bits=w-n
    else:
        starts=m-w+1;t=theta.exp();mu=starts*rho
        assert n/starts<rho<1 and 1<t<2
        a=(rho*(t-1)).exp()/t
        assert a<1
        b=(1-1/t)*(rho*(t/2-1)).exp()
        c=max(D(1),b/(1-a))
        failure=(-mu+n+n*(mu/n).ln()).exp()+mu*c*(rho*(t-1)-w*theta).exp()
        bits=-failure.ln()/ln2
    assert bits>=ssp+2-D('1e-10'), row
    assert abs(bits-D(row['bits']))<D('1e-6'), row
    margin=bits-ssp
    minimum_margin=margin if minimum_margin is None else min(minimum_margin,margin)
    checked+=1
print(json.dumps(dict(checked=checked,decimal_digits=80,minimum_margin_bits=str(minimum_margin)),indent=2))
