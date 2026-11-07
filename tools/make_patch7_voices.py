"""Adds the 1920 extra voices to 7--ESPECTRES_2027_5 (after make_patch7.py), as three macros fed
by Forms Box's Root Out / Scale Out, to hear that everything is in tune:

  SUPERSAW  SuperSaw13 at Root Out + pitch bend -> VadimFilter (sub cut) -> gain (level x Opacity)
  808       Kick_808 at Root Out + pitch bend, triggered by Kick -> VadimFilter -> gain (level x Opacity)
  NEBULA    Analog13 chord: the scale's pitch classes from the chord root (Scale Out - Root Out
            mod 12, sorted, 7 notes cycling), on the chord root's pitch class in the octave
            from MIDI 72, + the old octave offsets [0,2,1,2,1,2,3] x 12, played -12 and -24
            (14 voices, as the 1920 macro did on a fixed root of 75)
            -> MoogFilter (drifting cutoffs) -> gain x4 -> Reverb13 -> gain (level x Opacity)
            -> PanAz (rotating; 2 speakers, so it is heard in stereo)

Their node settings are copied from patch 6's Macro_2 (and its nebula macro). Forms Box.Audio and
the three macros go into an SC PolyMixer (patch 6's settings) -> the meters' LinearGain -> SC Output.
Forms Box publishes Pitch Bend and Opacity for them; each macro's level is set in the patch.
Not included: the old side-chain ducking of the supersaw / nebula and their 13-channel delays.
Usage: python3 -I make_patch7_voices.py <Presets/ESPECTRES folder>
"""
import json, os, sys, shutil

PR = sys.argv[1]
P6, P7 = os.path.join(PR, "6--ESPECTRES_2027_4"), os.path.join(PR, "7--ESPECTRES_2027_5")
M2, M2N = os.path.join(P6, "Macro_2"), os.path.join(P6, "Macro_2", "Macro_4")
FB = "Forms_Box_1"

def load(p): return json.load(open(p))
def save(p, j): json.dump(j, open(p, "w"), indent=4)
def src(folder, stem):
    j = load(os.path.join(folder, stem + ".json"))
    j.pop("expanded", None)
    return j

def router(kind, name, value=None, lo=None, hi=None):
    j = {"Exclude_From_Snapshots": "0", "Name": name}
    if kind == "f" or kind == "i":
        j.update({"Max": str(hi), "Min": str(lo), "Value": str(value)})
    elif kind == "v_f":
        j.update({"Max": float(hi), "Min": float(lo), "Value": value})
    return j

def vio(op, in2=None, trig=2, in1=None):
    j = {"Op_": str(op), "Trig_In": str(trig)}
    if in1 is not None: j["Input_1"] = in1
    if in2 is not None: j["Input_2"] = in2
    return j

class Macro:
    def __init__(self, name):
        self.name, self.nodes, self.edges, self.ports = name, [], [], []
    def add(self, typ, idx, j):
        stem = typ.replace(" ", "_") + "_" + str(idx)
        self.nodes.append((typ, idx, stem, j))
        return stem
    def port(self, kind, name, *a):
        stem = self.add("Router " + kind, sum(1 for t, *_ in self.nodes if t == "Router " + kind) + 1, router(kind, name, *a))
        self.ports.append((name, a[0] if a else None))
        return stem
    def wire(self, s, sp, d, dp): self.edges.append((s, sp, d, dp))
    def write(self, folder, idx, values):
        d = os.path.join(folder, "Macro_%d" % idx)
        if os.path.exists(d): shutil.rmtree(d)
        os.makedirs(d)
        mods, files = {}, []
        for k, (typ, i, stem, j) in enumerate(self.nodes):
            save(os.path.join(d, stem + ".json"), dict(j, expanded=True))
            files.append(stem + ".json")
            mods.setdefault(typ, {})["%02d" % i] = [float(-900 + 380 * (k % 6)), float(-400 + 170 * (k // 6))]
        save(os.path.join(d, "modules.json"), mods)
        c = {}
        stems = {stem for _, _, stem, _ in self.nodes}
        for s, sp, dd, dp in self.edges:
            assert s in stems and dd in stems, (self.name, s, dd)
            c.setdefault(s, {}).setdefault(sp, {}).setdefault(dd, {})[dp] = None
        save(os.path.join(d, "connections.json"), c)
        save(os.path.join(d, ".oceanode-node-files.json"), {"localMacros": [], "nodes": sorted(files), "protectedNodes": [], "version": 1})
        save(os.path.join(d, "comments.json"), {"NumComments": 0})
        save(os.path.join(d, "custom_guis.json"), {"panels": []})
        save(os.path.join(d, "description.json"), {"Description": self.name})
        save(os.path.join(d, "midi.json"), {})
        top = {"Active": "1", "Color": "0, 0, 0, 255", "Description": self.name, "LocalPreset": True, "Local_Name": "Local",
               "Reset_Ph_on_Active": "0", "RetriggerSnapshotOnActive": False, "Retrigger_Snapshot_on_Active": "0",
               "RouterSortOrder": [n for n, _ in self.ports], "Show_Names": "1", "Show_Snapshot?": "0", "Snapshot": "-1",
               "Snapshot_Cols": "8", "Snapshot_Rows": "2", "expanded": True}
        for n, v in self.ports:
            if v is not None: top[n] = v if isinstance(v, list) else str(v)
        top.update(values)
        save(os.path.join(folder, "Macro_%d.json" % idx), top)
        return self

# ---------------------------------------------------------------- SUPERSAW
ss = Macro("SUPERSAW")
r_root = ss.port("f", "root", 27, 0, 130)
r_bend = ss.port("f", "bend", 0, -48, 48)
r_lvl = ss.port("f", "level", 0.097, 0, 1)
r_op = ss.port("f", "opacity", 1, 0, 1)
r_out = ss.port("ScBus", "out")
pitch = ss.add("Vector Item Operations", 1, vio(1))
gain = ss.add("Vector Item Operations", 2, vio(2))
saw = ss.add("SC SuperSaw13", 1, dict(src(M2, "SC_SuperSaw13_1"), levels=1.0))
flt = ss.add("SC VadimFilter*", 1, src(M2, "SC_VadimFilter*_1"))
lg = ss.add("SC LinearGain*", 1, src(M2, "SC_LinearGain*_2"))
ss.wire(r_root, "Value", pitch, "Input.1"); ss.wire(r_bend, "Value", pitch, "Input.2")
ss.wire(pitch, "Output", saw, "pitch")
ss.wire(saw, "Out", flt, "In"); ss.wire(flt, "Out", lg, "In"); ss.wire(lg, "Out", r_out, "Value")
ss.wire(r_lvl, "Value", gain, "Input.1"); ss.wire(r_op, "Value", gain, "Input.2"); ss.wire(gain, "Output", lg, "Gain")

# ---------------------------------------------------------------- 808
k8 = Macro("808")
r_root = k8.port("f", "root", 27, 0, 130)
r_bend = k8.port("f", "bend", 0, -48, 48)
r_kick = k8.port("i", "kick", 0, 0, 1)
r_lvl = k8.port("f", "level", 0.2, 0, 1)
r_op = k8.port("f", "opacity", 1, 0, 1)
r_out = k8.port("ScBus", "out")
pitch = k8.add("Vector Item Operations", 1, vio(1))
gain = k8.add("Vector Item Operations", 2, vio(2))
f2v = k8.add("FloatToVoid", 1, src(M2, "FloatToVoid_2"))
v2t = k8.add("Void to Tick", 1, src(M2, "Void_to_Tick_2"))
kick = k8.add("SC Kick_808", 1, src(M2, "SC_Kick_808_1"))
flt = k8.add("SC VadimFilter*", 1, src(M2, "SC_VadimFilter*_2"))
lg = k8.add("SC LinearGain*", 1, src(M2, "SC_LinearGain*_3"))
k8.wire(r_root, "Value", pitch, "Input.1"); k8.wire(r_bend, "Value", pitch, "Input.2"); k8.wire(pitch, "Output", kick, "Pitch")
k8.wire(r_kick, "Value", f2v, "Float In"); k8.wire(f2v, "Void Out", v2t, "Void Input"); k8.wire(v2t, "Tick Output", kick, "trig")
k8.wire(kick, "Out", flt, "In"); k8.wire(flt, "Out", lg, "In"); k8.wire(lg, "Out", r_out, "Value")
k8.wire(r_lvl, "Value", gain, "Input.1"); k8.wire(r_op, "Value", gain, "Input.2"); k8.wire(gain, "Output", lg, "Gain")

# ---------------------------------------------------------------- NEBULA
nb = Macro("NEBULA")
r_root = nb.port("f", "root", 27, 0, 130)
r_scale = nb.port("v_f", "scale", [0.0, 3.0, 7.0, 10.0], 0, 11)
r_bend = nb.port("f", "bend", 0, -48, 48)
r_lvl = nb.port("f", "level", 0, 0, 1)
r_op = nb.port("f", "opacity", 1, 0, 1)
r_out = nb.port("ScBus", "out")
# the chord: intervals of the scale above the chord root, 7 of them, voiced like the 1920 nebula
root_pc = nb.add("Vector Item Operations", 1, vio(7, 12.0, trig=0))         # root mod 12
rel = nb.add("Vector Item Operations", 2, vio(9))                           # scale - root pc
rel12 = nb.add("Vector Item Operations", 3, vio(1, 12.0, trig=0))           # + 12 (fmod keeps the sign)
relpc = nb.add("Vector Item Operations", 4, vio(7, 12.0, trig=0))           # mod 12: 0..11 above the root
srt = nb.add("Vector Reorder", 1, {"Op_": "0"})                             # sort: the root (0) first
rsz = nb.add("Vector Resize", 1, {"Fill": "1", "Interp": "0", "Resample": "0", "Size": 7.0})
base = nb.add("Vector Item Operations", 5, vio(1, 72.0, trig=0))            # root pc in the octave from 72
based = nb.add("Vector Item Operations", 6, vio(1))                         # + pitch bend
notes = nb.add("Vector Item Operations", 7, vio(1))                         # intervals + base
octs = nb.add("Manual Vector", 4, src(M2N, "Manual_Vector_4"))              # [0,2,1,2,1,2,3]
octs12 = nb.add("Vector Item Operations", 8, vio(2, 12.0, trig=0))
voiced = nb.add("Vector Item Operations", 9, vio(1))
low1 = nb.add("Vector Item Operations", 10, vio(9, 12.0, trig=0))
low2 = nb.add("Vector Item Operations", 11, vio(9, 12.0, trig=0))
chain = nb.add("Vector Chain", 1, {"Inputs": "2", "Interleave": "0", "Listen_All": "1", "Offset": "0"})
nb.wire(r_root, "Value", root_pc, "Input.1")
nb.wire(r_scale, "Value", rel, "Input.1"); nb.wire(root_pc, "Output", rel, "Input.2")
nb.wire(rel, "Output", rel12, "Input.1"); nb.wire(rel12, "Output", relpc, "Input.1")
nb.wire(relpc, "Output", srt, "Input"); nb.wire(srt, "Output", rsz, "Input")
nb.wire(root_pc, "Output", base, "Input.1"); nb.wire(base, "Output", based, "Input.1"); nb.wire(r_bend, "Value", based, "Input.2")
nb.wire(rsz, "Output", notes, "Input.1"); nb.wire(based, "Output", notes, "Input.2")
nb.wire(octs, "Out", octs12, "Input.1"); nb.wire(notes, "Output", voiced, "Input.1"); nb.wire(octs12, "Output", voiced, "Input.2")
nb.wire(voiced, "Output", low1, "Input.1"); nb.wire(low1, "Output", low2, "Input.1")
nb.wire(low1, "Output", chain, "Input 0"); nb.wire(low2, "Output", chain, "Input 1")
# the 1920 nebula's sound (its Macro_4), unchanged
cnt = nb.add("Counter", 1, src(M2N, "Counter_1"))
ix1 = nb.add("Indexer", 1, src(M2N, "Indexer_1"))
ix2 = nb.add("Indexer", 2, src(M2N, "Indexer_2"))
mv1 = nb.add("Manual Vector", 1, src(M2N, "Manual_Vector_1"))
mv2 = nb.add("Manual Vector", 2, src(M2N, "Manual_Vector_2"))
nz = nb.add("Noise", 2, src(M2N, "Noise_2"))
ph = nb.add("Phasor", 1, src(M2N, "Phasor_1"))
ro = nb.add("Random Oscillator", 1, src(M2N, "Random_Oscillator_1"))
rg1 = nb.add("Ranger", 1, src(M2N, "Ranger_1"))
rg2 = nb.add("Ranger", 2, src(M2N, "Ranger_2"))
pw = nb.add("Vector Item Operations", 12, src(M2N, "Vector_Item_Operations_1"))
osc = nb.add("SC Analog13", 1, src(M2N, "SC_Analog13_1"))
moog = nb.add("SC MoogFilter13", 1, src(M2N, "SC_MoogFilter13_1"))
nb.wire(cnt, "Out", nz, "Input"); nb.wire(ix2, "Output", nz, "Seed"); nb.wire(nz, "Output", rg2, "Input"); nb.wire(rg2, "Output", moog, "pitch")
nb.wire(ix1, "Output", ro, "Index"); nb.wire(ph, "Phase", ro, "Phase"); nb.wire(ro, "Output", rg1, "Input")
nb.wire(rg1, "Output", pw, "Input.1"); nb.wire(pw, "Output", osc, "Levels")
nb.wire(mv1, "Out", osc, "vibF"); nb.wire(mv2, "Out", osc, "tremF")
nb.wire(chain, "Output", osc, "Pitch"); nb.wire(osc, "Out", moog, "In")
# then Macro_2's chain for it: x4, reverb, level, rotating pan
g4 = nb.add("SC LinearGain13", 1, src(M2, "SC_LinearGain13_1"))
rev = nb.add("SC Reverb13", 1, src(M2, "SC_Reverb13_1"))
lg = nb.add("SC LinearGain*", 1, src(M2, "SC_LinearGain*_4"))
gain = nb.add("Vector Item Operations", 13, vio(2))
ix3 = nb.add("Indexer", 3, src(M2, "Indexer_2"))
sph = nb.add("SC Phasor*", 1, src(M2, "SC_Phasor*_1"))
smap = nb.add("SC Mapper*", 1, src(M2, "SC_Mapper*_1"))
pan = nb.add("SC PanAz*", 1, dict(src(M2, "SC_PanAz*_1"), NumSpeakers="2"))
nb.wire(moog, "Out", g4, "In"); nb.wire(g4, "Out", rev, "In"); nb.wire(rev, "Out", lg, "In"); nb.wire(lg, "Out", pan, "In")
nb.wire(r_lvl, "Value", gain, "Input.1"); nb.wire(r_op, "Value", gain, "Input.2"); nb.wire(gain, "Output", lg, "Gain")
nb.wire(ix3, "Output", sph, "Phoff"); nb.wire(sph, "Out", smap, "In"); nb.wire(smap, "Out", pan, "Position")
nb.wire(pan, "Out", r_out, "Value")

# ---------------------------------------------------------------- top level
for i, m in ((1, ss), (2, k8), (3, nb)): m.write(P7, i, {})
shutil.copy(os.path.join(P6, "SC_PolyMixer*_1.json"), os.path.join(P7, "SC_PolyMixer*_1.json"))
mods = load(os.path.join(P7, "modules.json"))
fbpos = mods["Forms Box"]["01"]
mods["Macro"] = {"01": [fbpos[0] + 700, fbpos[1] - 300], "02": [fbpos[0] + 700, fbpos[1]], "03": [fbpos[0] + 700, fbpos[1] + 300]}
mods["SC PolyMixer*"] = {"01": [fbpos[0] + 1150, fbpos[1]]}
save(os.path.join(P7, "modules.json"), mods)
c = load(os.path.join(P7, "connections.json"))
E = [(s, sp, d, dp) for s, ps in c.items() for sp, ds in ps.items() for d, dps in ds.items() for dp in dps]
E = [e for e in E if e != (FB, "Audio", "SC_LinearGain*_1", "In")]
ADD = [(FB, "Audio", "SC_PolyMixer*_1", "In"), ("Macro_1", "out", "SC_PolyMixer*_1", "In2"),
       ("Macro_2", "out", "SC_PolyMixer*_1", "In3"), ("Macro_3", "out", "SC_PolyMixer*_1", "In4"),
       ("SC_PolyMixer*_1", "Out", "SC_LinearGain*_1", "In"),
       (FB, "Scale Out", "Macro_3", "scale"), (FB, "Kick", "Macro_2", "kick")]
for m in ("Macro_1", "Macro_2", "Macro_3"):
    ADD += [(FB, "Root Out", m, "root"), (FB, "Pitch Bend", m, "bend"), (FB, "Opacity", m, "opacity")]
E = sorted(set(E + ADD))
c = {}
for s, sp, d, dp in E: c.setdefault(s, {}).setdefault(sp, {}).setdefault(d, {})[dp] = None
save(os.path.join(P7, "connections.json"), c)
idx_p = os.path.join(P7, ".oceanode-node-files.json")
if os.path.exists(idx_p):
    idx = load(idx_p)
    idx["nodes"] = sorted(set(idx.get("nodes", [])) | {"Macro_1.json", "Macro_2.json", "Macro_3.json", "SC_PolyMixer*_1.json"})
    idx["localMacros"] = sorted(set(idx.get("localMacros", [])) | {"Macro_1", "Macro_2", "Macro_3"})
    save(idx_p, idx)
fb = load(os.path.join(P7, FB + ".json"))
for k in ("Pitch_Bend", "Opacity"):
    if k not in fb["formsPublished"]: fb["formsPublished"].append(k)
save(os.path.join(P7, FB + ".json"), fb)
print("voices added: Macro_1 SUPERSAW (%d nodes), Macro_2 808 (%d), Macro_3 NEBULA (%d); PolyMixer; %d connections"
      % (len(ss.nodes), len(k8.nodes), len(nb.nodes), len(E)))
