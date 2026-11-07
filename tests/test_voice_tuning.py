"""Tuning test of patch 7's voice macros (SUPERSAW, 808, NEBULA), offline.

Evaluates each macro's control graph from its files (connections.json + node settings), with the
node semantics of ofxOceanodeCollection (Vector Item Operations, Vector Reorder, Vector Resize,
Vector Chain, Manual Vector, routers), fed with Forms Box's Root Out / Scale Out for a grid of
harmonies (JSON lines from harmony_cases.cpp on stdin), and checks:
  - SUPERSAW and 808 pitch = Root Out + pitch bend
  - NEBULA: 14 notes, every pitch class in Scale Out, the first note on the chord root,
    shifted by the pitch bend
Writes the pitches it computed to <out.json> for the audio test (render_voices.scd).
Usage: harmony_cases | python3 -I test_voice_tuning.py <patch 7 folder> <out.json>
"""
import json, math, os, sys

P7, OUT = sys.argv[1], sys.argv[2]

class Graph:
    def __init__(self, folder):
        self.f = folder
        c = json.load(open(os.path.join(folder, "connections.json")))
        self.into = {}
        for s, ps in c.items():
            for sp, ds in ps.items():
                for d, dps in ds.items():
                    for dp in dps: self.into[(d, dp)] = (s, sp)
        self.nodes = {}
        for f in os.listdir(folder):
            if f.endswith(".json") and not f.startswith(".") and f[:-5] not in ("connections", "modules", "comments", "custom_guis", "description", "midi", "scope_config"):
                self.nodes[f[:-5]] = json.load(open(os.path.join(folder, f)))
        self.ext = {}
    def vec(self, v):
        if isinstance(v, list): return [float(x) for x in v]
        return [float(v)]
    def param(self, node, name):
        if (node, name) in self.into:
            s, sp = self.into[(node, name)]
            return self.out(s, sp)
        return self.vec(self.nodes[node][name.replace(".", "_").replace(" ", "_")])
    def out(self, node, port):
        typ = node.rsplit("_", 1)[0]
        j = self.nodes[node]
        if typ.startswith("Router"):
            return self.vec(self.ext[j["Name"]])
        if typ == "Manual_Vector":
            return self.vec(j["Value"])
        if typ == "Vector_Item_Operations":
            a, b = self.param(node, "Input.1"), self.param(node, "Input.2")
            n = max(len(a), len(b))
            g = lambda v, i: v[i] if i < len(v) else v[0]
            op = int(j["Op_"])
            f = {1: lambda x, y: x + y, 2: lambda x, y: x * y, 7: math.fmod, 9: lambda x, y: x - y,
                 8: lambda x, y: 0 if y == 0 else x / y, 10: math.pow}[op]
            return [f(g(a, i), g(b, i)) for i in range(n)]
        if typ == "Vector_Reorder":
            assert j["Op_"] == "0"
            return sorted(self.param(node, "Input"))
        if typ == "Vector_Resize":
            v, size = self.param(node, "Input"), int(float(j["Size"]))
            assert j["Resample"] == "0" and j["Fill"] == "1"
            o = list(v)
            while len(o) < size: o += v
            return o[:size]
        if typ == "Vector_Chain":
            o = []
            for i in range(int(j["Inputs"])): o += self.param(node, "Input %d" % i)
            return o
        raise Exception("no model for " + node)

cases = [json.loads(l) for l in sys.stdin if l.strip()]
G = {name: Graph(os.path.join(P7, m)) for name, m in (("SUPERSAW", "Macro_1"), ("808", "Macro_2"), ("NEBULA", "Macro_3"))}
TARGET = {"SUPERSAW": ("SC_SuperSaw13_1", "pitch"), "808": ("SC_Kick_808_1", "Pitch"), "NEBULA": ("SC_Analog13_1", "Pitch")}
fails, checks, renders = 0, 0, []
def check(ok, msg):
    global fails, checks
    checks += 1
    if not ok:
        fails += 1
        if fails <= 20: print("FAIL", msg)
for c in cases:
    for bend in (0.0, -0.37, 1.5):
        got = {}
        for name, g in G.items():
            g.ext = {"root": c["rootOut"], "scale": c["scaleOut"], "bend": bend, "level": 1, "opacity": 1, "kick": 0}
            got[name] = g.param(*TARGET[name])
        tag = "root %d trans %d deg %d scale %s bend %g" % (c["root"], c["trans"], c["degree"], c["scale"], bend)
        for v in ("SUPERSAW", "808"):
            check(len(got[v]) == 1 and abs(got[v][0] - (c["rootOut"] + bend)) < 1e-4, "%s %s: %s" % (v, tag, got[v]))
        neb = got["NEBULA"]
        pcs = [((x - bend) % 12) for x in neb]
        check(len(neb) == 14, "NEBULA size %d" % len(neb))
        check(all(min(abs(p - s), 12 - abs(p - s)) < 1e-4 for p in pcs for s in [min(c["scaleOut"], key=lambda s: min(abs(p - s), 12 - abs(p - s)))]), "NEBULA out of the scale: %s %s" % (tag, pcs))
        d0 = ((neb[0] - bend) - c["rootOut"]) % 12
        check(min(d0, 12 - d0) < 1e-4, "NEBULA first note not the chord root: %s %s" % (tag, neb))
        check(all(24 <= x <= 120 for x in neb), "NEBULA range %s" % neb)
        if bend == 0 and c["degree"] < 2 and c["trans"] in (0, 2):
            renders.append({"case": tag, "rootOut": c["rootOut"], "scaleOut": c["scaleOut"], "supersaw": got["SUPERSAW"][0], "kick808": got["808"][0], "nebula": neb})
json.dump(renders, open(OUT, "w"), indent=1)
print("%d harmonies x 3 bends: %d checks, %d failed; %d cases kept for the audio test" % (len(cases), checks, fails, len(renders)))
ex = renders[0]
print("example:", ex["case"], "-> Root Out", ex["rootOut"], "Scale Out", ex["scaleOut"], "| nebula", [int(x) for x in ex["nebula"]])
sys.exit(1 if fails else 0)
