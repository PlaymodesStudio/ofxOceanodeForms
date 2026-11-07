"""Removes the ofxSantiNodes portal widgets (Slider, Value, Toggle, Button) from 7--ESPECTRES_2027_5,
in place (after make_patch7.py and make_patch7_voices.py; the canvas layout the app saved is kept),
with the portals that only linked them to Forms Box.

Kept: the custom GUI panel and the portals it uses (Speed "slider_speed" 59, SEQUENCE_ON 10,
newPNG 8, finalTex 5 / 8). Volume, Hue and Levels stay on Forms Box's node (their values were
copied from the portals by make_patch7.py); Header+ and Max Minutes are Forms Box settings again.
Forms Box stops publishing what only fed the widgets (Seq Minutes, Sequence Out, actualSnapshot,
Fade, Max Minutes). Prints a report.
Usage: python3 -I make_patch7_nogui.py <Presets/ESPECTRES folder>
"""
import json, os, sys

P7 = os.path.join(sys.argv[1], "7--ESPECTRES_2027_5")
FB = "Forms_Box_1"
def load(p): return json.load(open(p))
def save(p, j): json.dump(j, open(p, "w"), indent=4)

mods = load(os.path.join(P7, "modules.json"))
present = {t.replace(" ", "_") + "_" + str(int(i)): (t, i) for t, ids in mods.items() for i in ids}
KEEP_PORTALS = {"Portal_f_59", "Portal_b_10", "Portal_v_8", "Portal_Texture_5", "Portal_Texture_8"}
WIDGETS = ("Slider", "Value", "Toggle", "Button", "Int_Slider", "Int_Value", "Ranged_Slider", "Multislider", "Multitoggle")
gone = sorted(s for s in present
              if s.rsplit("_", 1)[0] in WIDGETS or (s.startswith("Portal_") and s not in KEEP_PORTALS))
# the custom GUI panel must not point at anything removed
gui = load(os.path.join(P7, "custom_guis.json"))
refs = {(w.get("parameterRef") or {}).get("parameterPath", "").split("/")[0]
        for p in gui.get("panels", []) for w in p.get("layout", {}).get("widgets", [])}
assert not (refs & set(gone)), refs & set(gone)

for s in gone:
    t, i = present[s]
    del mods[t][i]
    if not mods[t]: del mods[t]
    p = os.path.join(P7, s + ".json")
    if os.path.exists(p): os.remove(p)
save(os.path.join(P7, "modules.json"), mods)

c = load(os.path.join(P7, "connections.json"))
E = [(s, sp, d, dp) for s, ps in c.items() for sp, ds in ps.items() for d, dps in ds.items() for dp in dps]
keep = [e for e in E if e[0] not in gone and e[2] not in gone]
dropped = [e for e in E if e not in keep]
c = {}
for s, sp, d, dp in keep: c.setdefault(s, {}).setdefault(sp, {}).setdefault(d, {})[dp] = None
save(os.path.join(P7, "connections.json"), c)

idx_p = os.path.join(P7, ".oceanode-node-files.json")
if os.path.exists(idx_p):
    idx = load(idx_p)
    idx["nodes"] = sorted(f for f in idx.get("nodes", []) if os.path.exists(os.path.join(P7, f)))
    save(idx_p, idx)

fb = load(os.path.join(P7, FB + ".json"))
fb["formsPublished"] = [k for k in fb["formsPublished"]
                        if k not in ("Seq_Minutes", "Sequence_Out", "actualSnapshot", "Fade", "Max_Minutes")]
save(os.path.join(P7, FB + ".json"), fb)

names = {t.replace(" ", "_") + "_" + str(int(i)) for t, ids in mods.items() for i in ids}
bad = [e for e in keep if e[0] not in names or e[2] not in names]
print("removed %d nodes: %s" % (len(gone), ", ".join(gone)))
print("connections: %d dropped, %d left" % (len(dropped), len(keep)))
for e in dropped: print("  - %s.%s -> %s.%s" % e)
print("Forms Box publishes:", fb["formsPublished"])
print("dangling connections:", bad or "none", "| custom GUI refs ok:", sorted(r for r in refs if r))
