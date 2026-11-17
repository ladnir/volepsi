"""Select witnesses, then re-evaluate them with 80-digit decimal arithmetic."""
from decimal import Decimal as D, localcontext
import json
import math
from pathlib import Path
from bound import bound

profiles=[(1.05,640),(1.10,320),(1.15,192),(1.20,144),
          (1.25,128),(1.30,112),(1.40,80),(1.75,64)]
rows=[]
with localcontext() as context:
    context.prec=80
    for overhead,width in profiles:
        r=bound(2**20,math.ceil(2**20*overhead),width)
        n,m,w=D(r['n']),D(r['m']),D(r['width'])
        rho,theta=D(str(r['rho'])),D(str(r['theta']))
        mu=(m-w+1)*rho
        t=theta.exp()
        a=(rho*(t-1)-theta).exp()
        assert n/(m-w+1)<rho<1 and 1<t<2 and a<1
        b=(1-1/t)*(rho*(t/2-1)).exp()
        c=max(D(1),b/(1-a))
        delta=(-mu+n+n*(mu/n).ln()).exp()
        failure=delta+mu*c*(rho*(t-1)-w*theta).exp()
        assert failure<D(2)**(-40)
        r['verified_bits_decimal80']=str(-failure.ln()/D(2).ln())
        r['overhead']=overhead
        rows.append(r)
        print(f"overhead={overhead:.2f} width={width} bits={float(r['verified_bits_decimal80']):.6f}")
Path(__file__).with_name('parameters.json').write_text(json.dumps(rows,indent=2)+'\n')
