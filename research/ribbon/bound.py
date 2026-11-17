"""Concrete ideal-random-band rank bound; no fitted failure probabilities.

See proof.txt for the argument. Output includes the Poissonization error.
The grid only selects valid witnesses; it is not needed for soundness.
"""
import argparse
import json
import math


def bound(n, m, width):
    starts = m - width + 1
    if starts <= n:
        return None
    best = None
    for j in range(1, 81):
        # Larger Poisson mean dominates fixed-n arrivals except for the
        # explicitly included lower-tail probability.
        rho = n / starts + (1 - n / starts) * j / 81
        mu = starts * rho
        log_delta = -mu + n + n * math.log(mu / n)
        for k in range(1, 1000):
            theta = math.log(2) * k / 1000
            t = math.exp(theta)
            log_a = rho * (t - 1) - theta
            if log_a >= 0:
                continue
            b = (1 - 1 / t) * math.exp(rho * (t / 2 - 1))
            c = max(1, b / -math.expm1(log_a))
            log_rank = math.log(mu * c) + rho * (t - 1) - width * theta
            hi, lo = max(log_delta, log_rank), min(log_delta, log_rank)
            log_total = hi + math.log1p(math.exp(lo - hi))
            bits = -log_total / math.log(2)
            if best is None or bits > best['bits']:
                best = dict(n=n, m=m, width=width, rho=rho, theta=theta,
                            C=c, bits=bits, poisson_bits=-log_delta/math.log(2))
    return best


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--log-n', type=int, default=20)
    a = p.parse_args()
    n = 1 << a.log_n
    for overhead in [1.05, 1.10, 1.15, 1.20, 1.25, 1.30, 1.40, 1.50]:
        for width in [64, 128, 192, 256, 320, 384, 512, 640, 768, 1024]:
            r = bound(n, math.ceil(n * overhead), width)
            if r and r['bits'] >= 40:
                print(json.dumps(dict(overhead=overhead, **r)), flush=True)
                break
