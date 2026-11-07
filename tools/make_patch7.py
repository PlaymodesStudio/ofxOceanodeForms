"""6--ESPECTRES_2027_4 -> 7--ESPECTRES_2027_5: the single-node patch.

Forms Box now does the post-processing (Output is the final image) and the sonification (Audio,
an ScBus). This removes the patch's video chain (HSLFilter, PixelStretch, opacity Ranger/Mixer,
Levels, the line macro, the column macro) and its audio (the sonificator macro, the L/R macro,
the PolyMixer and the vector chains), and wires:
    Forms Box.Output -> Portal Texture "finalTex" (-> Display Output, custom GUI preview)
    Forms Box.Audio  -> SC LinearGain* 1 (gain 1, the custom GUI's meters) -> SC Output + Spectrogram
    portals hue / Levels / Volume -> Forms Box Hue / Levels / Volume
Patch 6 is not touched. Prints a report.
Usage: python3 -I make_patch7.py <Presets/ESPECTRES folder>
"""
import json, os, sys, shutil

PR = sys.argv[1]
SRC, DST = os.path.join(PR, "6--ESPECTRES_2027_4"), os.path.join(PR, "7--ESPECTRES_2027_5")
if os.path.exists(DST):
    sys.exit(DST + " exists; remove it first")
shutil.copytree(SRC, DST)
FB = "Forms_Box_1"

# ---- nodes that go (file stem -> modules.json type, id)
GONE = ["HSLFilter_1", "PixelStretch_1", "Levels_1", "Mixer_1", "Mixer_4", "Ranger_11", "Ranger_13", "Ranger_14",
        "Ranger_15", "Macro_2", "Macro_4", "Macro_11", "Macro_12", "SC_PolyMixer*_1", "Number_1", "Number_5",
        "Vector_Chain_1", "Vector_Rotator_1", "Vector_Rotator_2", "Vector_Resize_1", "Vector_Resize_2",
        "Vector_Item_Operations_31", "Vector_Item_Operations_33",
        # the L/R balance display (now inside Forms Box) and the fade into the PolyMixer
        "Portal_f_105", "Portal_f_106", "Slider_6", "Portal_f_90"]

def stem_to_type(stem):
    t, i = stem.rsplit("_", 1)
    return t.replace("_", " "), int(i)

# ---- connections
conn = json.load(open(os.path.join(DST, "connections.json")))
E = [(s, sp, d, dp) for s, ps in conn.items() for sp, ds in ps.items() for d, dps in ds.items() for dp in dps]
keep, dropped = [], []
for e in E:
    s, sp, d, dp = e
    if s in GONE or d in GONE:
        dropped.append(e); continue
    if (s, sp, d, dp) == ("Portal_f_96", "Value", "SC_LinearGain*_1", "Gain"):
        dropped.append(e); continue     # the volume is Forms Box's now
    keep.append(e)
ADD = [(FB, "Output", "Portal_Texture_5", "Value"),
       (FB, "Audio", "SC_LinearGain*_1", "In"),
       ("Portal_f_114", "Value", FB, "Hue"),
       ("Portal_f_94", "Value", FB, "Levels"),
       ("Portal_f_96", "Value", FB, "Volume")]
new = sorted(set(keep + ADD))
c = {}
for s, sp, d, dp in new:
    c.setdefault(s, {}).setdefault(sp, {}).setdefault(d, {})[dp] = None
json.dump(c, open(os.path.join(DST, "connections.json"), "w"), indent=4)

# ---- modules.json, node files, macro folders
mods = json.load(open(os.path.join(DST, "modules.json")))
removed = []
for stem in GONE:
    t, i = stem_to_type(stem)
    for k in list(mods.get(t, {})):
        if int(k) == i:
            del mods[t][k]; removed.append(stem)
    if t in mods and not mods[t]:
        del mods[t]
    for p in (os.path.join(DST, stem + ".json"), os.path.join(DST, stem)):
        if os.path.isdir(p): shutil.rmtree(p)
        elif os.path.exists(p): os.remove(p)
json.dump(mods, open(os.path.join(DST, "modules.json"), "w"), indent=4)
# leftovers of nodes that were already gone in patch 6 (files without a module)
present = {t.replace(" ", "_") + "_" + str(int(i)) for t, ids in mods.items() for i in ids}
stale = []
for f in os.listdir(DST):
    if f.endswith(".json") and f[:-5] not in present and f[:-5].rsplit("_", 1)[-1].isdigit():
        stale.append(f); os.remove(os.path.join(DST, f))
idx_p = os.path.join(DST, ".oceanode-node-files.json")
if os.path.exists(idx_p):
    idx = json.load(open(idx_p))
    idx["nodes"] = sorted(f for f in idx.get("nodes", []) if os.path.exists(os.path.join(DST, f)))
    idx["localMacros"] = sorted(m for m in idx.get("localMacros", []) if os.path.isdir(os.path.join(DST, m)))
    json.dump(idx, open(idx_p, "w"), indent=4)

# ---- the meters' LinearGain: unity (Forms Box has the volume)
lg_p = os.path.join(DST, "SC_LinearGain*_1.json")
lg = json.load(open(lg_p)); lg["Gain"] = 1.0; lg["N_Chan"] = "2"
json.dump(lg, open(lg_p, "w"), indent=4)

# ---- Forms Box: what is published, the window's settings (typed, from patch 6's values)
fb_p = os.path.join(DST, FB + ".json")
fb = json.load(open(fb_p))
def num(v, kind):
    if kind == bool: return str(v) in ("1", "true", "True")
    if kind == int: return int(float(v))
    return float(v)
ctl = {}
for key, kind in (("Analog_Mode", bool), ("BPM_Add", float), ("Max_Minutes", float), ("Auto_Header", bool),
                  ("Tremolo", bool), ("Hard_Cut", bool), ("Header_Min", float), ("Header_Max", float),
                  ("Width", int), ("Height", int), ("actualBarIn", int)):
    if key in fb: ctl[key] = num(fb[key], kind)
ctl["Post"] = True
ctl["Sound"] = True
ctl["Kick_Folder"] = os.path.join(os.path.abspath(DST), "samples", "KICKS")
# the patch's levels / hue / volume portal values as the starting values
for portal, key in (("Portal_f_94", "Levels"), ("Portal_f_114", "Hue"), ("Portal_f_96", "Volume")):
    try: ctl[key] = float(json.load(open(os.path.join(DST, portal + ".json")))["Value"])
    except Exception: pass
fb["formsControls"] = ctl
fb["formsPublished"] = ["Volume", "Hue", "Levels", "Kick_On",
                        # what the rest of the patch still uses
                        "Width", "Height", "Max_Minutes", "Reset_Setlist", "Seq_Minutes", "Sequence_Out",
                        "actualSnapshot", "Fade", "Restart_Setlist", "Setlist_Snapshot"]
json.dump(fb, open(fb_p, "w"), indent=4)

# ---- report
print("patch:", DST)
print("removed nodes (%d):" % len(removed), ", ".join(removed))
print("stale files removed:", ", ".join(stale) or "none")
print("connections: %d kept, %d dropped, %d added -> %d" % (len(keep), len(dropped), len(ADD), len(new)))
for e in ADD: print("  + %s.%s -> %s.%s" % e)
left = sorted({t for t in mods})
print("nodes left (%d types):" % len(left), ", ".join("%s x%d" % (t, len(mods[t])) for t in left))
# every connection must point at a node that exists
names = present | {FB}
bad = [e for e in new if e[0] not in names or e[2] not in names]
print("dangling connections:", bad or "none")
gui = json.load(open(os.path.join(DST, "custom_guis.json")))
refs = [(w.get("parameterRef") or {}).get("parameterPath") for p in gui.get("panels", []) for w in p.get("layout", {}).get("widgets", []) if (w.get("parameterRef") or {}).get("parameterPath")]
missing = [r for r in refs if r.split("/")[0] not in names]
print("custom GUI widgets:", len(refs), "| pointing at removed nodes:", missing or "none")
