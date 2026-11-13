"""Thin-feature probe near the USB end: area of material thinner than 0.4 mm in each layer of the print parts."""
import numpy as np, manifold3d as M, design as b
ZONE=b.box(-12,12,-1,4,b.S-3,b.H+1)
def probe(name):
    m=b.assembled[name]^ZONE; bb=m.bounding_box(); tot=0; worst=(0,0)
    for z in np.arange(bb[2]+.1,bb[5],.2):
        s=m.slice(float(z))
        if s.is_empty(): continue
        thin=s-s.offset(-.2,M.JoinType.Round).offset(.21,M.JoinType.Round)
        a=sum(c.area() for c in thin.decompose() if c.area()>.01); tot+=a
        if a>worst[1]: worst=(round(float(z),2),round(a,3))
    return round(tot,3),worst
for n in ('handheld_front','handheld_back','camera_front','camera_back'): print(n,'thin area sum',*probe(n))
