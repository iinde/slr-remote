"""Load the Miniworks halves (Thingiverse 7367243, Chem_Kid, CC BY-NC) in the analysis frame:
x across (0 centre, +x GPS side), y from the USB end face, z from the back face (seam z 10.79)."""
import numpy as np, trimesh, manifold3d as M
from pathlib import Path
SRC = str(Path(__file__).resolve().parents[2] / "vendor/miniworks/Meshtastic_Miniworks_V3.0.3MF")
_s = trimesh.load(SRC); _O = np.array([1271.64, 646.49, 849.85])
def _frame(n):
    m = _s.geometry[n]; v = m.vertices - _O
    t = trimesh.Trimesh(np.c_[-(v[:, 2] - 45.72 / 2), v[:, 1], v[:, 0]], m.faces, process=True)
    if t.volume < 0: t.invert()
    return M.Manifold(M.Mesh(np.asarray(t.vertices, np.float32), np.asarray(t.faces, np.uint32)))
MA, MB = _frame("body93767"), _frame("body93787")
