// Offline (NRT) tests of the Forms synthdefs. Never plays audio: only Score.recordNRT renders.
// Run:  perl -e 'alarm shift; exec @ARGV' 600 sclang -D test_formsBox_nrt.scd [outdir]
// Loads the compiled defs from CompiledSynthdefs/Forms (so the shipped files are tested),
// renders float WAVs to outdir, then runs test_formsBox_levels.py on them.
(
var here, defDir, outDir, kickDir, kickFile, opts, renders, runNext, mkBank, n = 512, sr = 48000;
var tapDuck, testTone, nan = 0/0;

here = PathName(thisProcess.nowExecutingPath).pathOnly.standardizePath;
if(here.isEmpty) { here = File.getcwd };
defDir = here +/+ "CompiledSynthdefs/Forms";
outDir = thisProcess.argv[0] ?? {
	"/private/tmp/claude-501/-Users-santiagovilanova-PM-Dropbox-PLAYMODES-STUDIO-ESPECTRES-ESPECTRES-1920/05fa11cb-efea-4762-aac1-9bcc82c6e488/scratchpad/sc_tests"
};
File.mkdir(outDir);
kickDir = "/Users/santiagovilanova/Documents/OF/of_playmodes/openFrameworks/apps/Playmodes/OceanodeScroller/bin/data/Presets/ESPECTRES/6--ESPECTRES_2027_4/samples/KICKS/";
kickFile = kickDir ++ "KOAN_SOUND_kick_boom.wav";

opts = ServerOptions.new
	.numOutputBusChannels_(6).numInputBusChannels_(0)
	.sampleRate_(sr).memSize_(65536).maxSynthDefs_(4096)
	.numControlBusChannels_(16384).numAudioBusChannels_(1024)
	.numWireBufs_(4096).blockSize_(64);

// test-only helper defs (sent as bytes, never written to the Forms folder)
tapDuck = SynthDef(\formsTestDuckTap, { Out.ar(\out.kr(2), K2A.ar(In.kr(\duckbus.kr(0)))) });
testTone = SynthDef(\formsTestTone, { Out.ar(\out.kr(0), SinOsc.ar([220, 221], 0, \amp.kr(0.1))) });

// one FormsBank512: s_new + n_setn of the arrays. pitches/gl/gr/duckw: arrays of 512
mkBank = { |t, id, ampbus, out, pitches, gl, gr, duckw, duckbus = 4000, extra = #[]|
	[
		[t, [\s_new, \FormsBank512, id, 1, 0, \ampbus, ampbus, \out, out, \duckbus, duckbus] ++ extra],
		[t, [\n_setn, id, \pitch, n] ++ pitches],
		[t, [\n_setn, id, \gl, n] ++ gl],
		[t, [\n_setn, id, \gr, n] ++ gr],
		[t, [\n_setn, id, \duckw, n] ++ duckw]
	]
};

renders = List[];

// ---- 1. bank: 4 partials lit (MIDI 40, 60, 80, 100), amp 1, gl = gr = 1 ----
renders.add((name: "t1_bank4", dur: 2, score: {
	var pitches = Array.fill(n, { |i| 30 + (i * 70 / n) });
	var amps = 0 ! n, g = 0 ! n;
	var lit = [0, 146, 293, 439];   // indexes whose pitch is 40, 60, 80, 100
	lit.do { |i, k| pitches[i] = [40, 60, 80, 100][k]; amps[i] = 1; g[i] = 1 };
	[[0, [\d_loadDir, defDir]], [0, [\c_setn, 0, n] ++ amps], [0, [\c_set, 4000, 0]]]
	++ mkBank.(0, 1000, 0, 0, pitches, g, g, 0 ! n)
	++ [[2, [\c_set, 0, 0]]]
}));

// ---- 2. bank + duck: duck bus = 1 from t = 1 s; duckw = 1 on the two low partials ----
// also: a NaN amp on partial 10 at t = 0.5 and 1 frame later valid again (must not poison)
renders.add((name: "t2_bank4_duck", dur: 2, score: {
	var pitches = Array.fill(n, { |i| 30 + (i * 70 / n) });
	var amps = 0 ! n, g = 0 ! n, dw = 0 ! n;
	var lit = [0, 146, 293, 439];
	lit.do { |i, k| pitches[i] = [40, 60, 80, 100][k]; amps[i] = 1; g[i] = 1 };
	dw[0] = 1; dw[146] = 1;
	[[0, [\d_loadDir, defDir]], [0, [\c_setn, 0, n] ++ amps], [0, [\c_set, 4000, 0]]]
	++ mkBank.(0, 1000, 0, 0, pitches, g, g, dw)
	++ [[0.5, [\c_set, 10, nan]], [0.5 + (1/30), [\c_set, 10, 0]],
		[1.0, [\c_set, 4000, 1]], [2, [\c_set, 0, 0]]]
}));

// ---- 3. kick (real sample, 2 triggers) + duck tap + a bank ducked by the kick ----
renders.add((name: "t3_kick_duck", dur: 3, score: {
	var pitches = Array.fill(n, { |i| 30 + (i * 70 / n) });
	var amps = 0 ! n, g = 0 ! n, dw = 0 ! n;
	pitches[0] = 45; amps[0] = 1; g[0] = 1; dw[0] = 1;     // ducked
	pitches[1] = 90; amps[1] = 1; g[1] = 1; dw[1] = 0;     // not ducked
	[
		[0, [\d_loadDir, defDir]],
		[0, [\d_recv, tapDuck.asBytes]],
		[0, [\b_allocReadChannel, 0, kickFile, 0, 0, 0]],
		[0, [\b_allocReadChannel, 1, kickFile, 0, 0, 1]],
		[0, [\c_setn, 0, n] ++ amps],
		[0.01, [\s_new, \FormsKick, 900, 1, 0, \out, 0, \bufl, 0, \bufr, 1, \duckbus, 4000, \duckdecay, 1]],
		[0.01, [\s_new, \formsTestDuckTap, 901, 1, 0, \out, 2, \duckbus, 4000]]
	]
	++ mkBank.(0.01, 1000, 0, 3, pitches, g, g, dw)    // bank on outs 3,4
	++ [[0.25, [\n_set, 900, \t_trig, 1]], [1.5, [\n_set, 900, \t_trig, 1]], [3, [\c_set, 0, 0]]]
}));

// ---- 4. FX + Master fed by a test tone (0.1 amp, 220 Hz) + kick ----
renders.add((name: "t4_tone_fx_master", dur: 4, score: {
	[
		[0, [\d_loadDir, defDir]],
		[0, [\d_recv, testTone.asBytes]],
		[0, [\b_allocReadChannel, 0, kickFile, 0, 0, 0]],
		[0, [\b_allocReadChannel, 1, kickFile, 0, 0, 1]],
		[0.01, [\s_new, \formsTestTone, 899, 1, 0, \out, 100, \amp, 0.1]],
		[0.01, [\s_new, \FormsKick, 900, 1, 0, \out, 104, \bufl, 0, \bufr, 1, \duckbus, 4000]],
		[0.01, [\s_new, \FormsFX, 950, 1, 0, \in, 100, \wetin, 106, \out, 102]],
		[0.01, [\s_new, \FormsMaster, 960, 1, 0, \in, 102, \kickin, 104, \out, 0]],
		[1.0, [\n_set, 900, \t_trig, 1]], [3.0, [\n_set, 900, \t_trig, 1]],
		[4, [\c_set, 0, 0]]
	]
}));

// ---- 5. FX with comp = 1, a 0.5 reverb send, master: 220 Hz tone at 0.5 amp (hot input) ----
// (the send split as a bank does it: cos to the dry bus, sin to the wet bus)
renders.add((name: "t5_tone_fx_comp", dur: 3, score: {
	[
		[0, [\d_loadDir, defDir]],
		[0, [\d_recv, testTone.asBytes]],
		[0.01, [\s_new, \formsTestTone, 899, 1, 0, \out, 100, \amp, 0.5 * (0.25pi).cos]],
		[0.01, [\s_new, \formsTestTone, 898, 1, 0, \out, 106, \amp, 0.5 * (0.25pi).sin]],
		[0.01, [\s_new, \FormsFX, 950, 1, 0, \in, 100, \wetin, 106, \out, 102, \comp, 1]],
		[0.01, [\s_new, \FormsMaster, 960, 1, 0, \in, 102, \kickin, 104, \out, 0]],
		[3, [\c_set, 0, 0]]
	]
}));

// ---- 6. realistic: 3 banks (1080 partials lit, random amps), kick, FX, master ----
// also the CPU measure: wall time of the render vs its duration
renders.add((name: "t6_full_1080", dur: 20, timed: true, score: {
	var msgs = [[0, [\d_loadDir, defDir]],
		[0, [\b_allocReadChannel, 0, kickFile, 0, 0, 0]],
		[0, [\b_allocReadChannel, 1, kickFile, 0, 0, 1]],
		[0.01, [\s_new, \FormsKick, 900, 1, 0, \out, 104, \bufl, 0, \bufr, 1, \duckbus, 4000]]];
	var total = 1080;
	thisThread.randSeed = 1234;
	3.do { |b|
		var pitches = Array.fill(n, { |i| 24 + (((b * n) + i) * 96 / total).min(96) });
		var gl = Array.fill(n, { |i| if((b * n) + i < total) { 1.0.rand } { 0 } });
		var gr = Array.fill(n, { |i| if((b * n) + i < total) { 1.0.rand } { 0 } });
		var dw = Array.fill(n, { |i| (1 - (((b * n) + i) / total)).clip(0, 1) });
		msgs = msgs ++ mkBank.(0.01, 1000 + b, b * n, 100, pitches, gl, gr, dw);
	};
	// per-frame (30 fps) amp updates, like the C++ c_setn every frame
	(0, 1/30 .. 19.9).do { |t|
		3.do { |b|
			msgs = msgs.add([t, [\c_setn, b * n, n] ++ Array.fill(n, { |i|
				if((b * n) + i < total) { (0.5.rand ** 2) } { 0 } })]);
		};
	};
	msgs = msgs ++ [
		[0.01, [\s_new, \FormsFX, 950, 1, 0, \in, 100, \wetin, 106, \out, 102]],
		[0.01, [\s_new, \FormsMaster, 960, 1, 0, \in, 102, \kickin, 104, \out, 0]],
		[20, [\c_set, 0, 0]]];
	msgs = msgs ++ (0.5, 1.0 .. 19.5).collect { |t| [t, [\n_set, 900, \t_trig, 1]] };
	// stable sort by time (same-time messages keep their order: s_new before n_setn, kick first)
	msgs.collect { |m, i| [m, i] }.sort { |a, b|
		(a[0][0] < b[0][0]) or: { (a[0][0] == b[0][0]) and: { a[1] < b[1] } }
	}.collect { |p| p[0] };
}));

// ---- 7. CPU: 3 FormsBank512 alone (1536 partials x L/R = 3072 SinOsc), 120 s ----
renders.add((name: "t7_cpu_3banks", dur: 120, timed: true, score: {
	var msgs = [[0, [\d_loadDir, defDir]]];
	3.do { |b|
		msgs = msgs ++ mkBank.(0.01, 1000 + b, b * n, 0,
			Array.fill(n, { |i| 30 + (i * 0.13) }), 0.5 ! n, 0.5 ! n, 0 ! n);
	};
	msgs ++ [[0, [\c_fill, 0, 3 * n, 0.3]], [120, [\c_set, 0, 0]]];
}));

// ---- 8. CPU: 1 FormsBank512 alone, 120 s ----
renders.add((name: "t8_cpu_1bank", dur: 120, timed: true, score: {
	[[0, [\d_loadDir, defDir]], [0, [\c_fill, 0, n, 0.3]]]
	++ mkBank.(0.01, 1000, 0, 0, Array.fill(n, { |i| 30 + (i * 0.13) }), 0.5 ! n, 0.5 ! n, 0 ! n)
	++ [[120, [\c_set, 0, 0]]];
}));

// ---- 9. startup baseline: empty score, 120 s (scsynth start + plugin load + writing) ----
renders.add((name: "t9_empty", dur: 120, timed: true, score: { [[0, [\d_loadDir, defDir]], [120, [\c_set, 0, 0]]] }));

runNext = { |i = 0|
	if(i >= renders.size) {
		var cmd = "python3 -I " ++ (here +/+ "test_formsBox_levels.py").quote ++ " " ++ outDir.quote;
		"\n==== running level check ====".postln;
		cmd.systemCmd;
		0.exit;
	} {
		var r = renders[i], score, wav, osc, t0;
		score = Score(r[\score].value);
		wav = outDir +/+ r[\name] ++ ".wav";
		osc = outDir +/+ r[\name] ++ ".osc";
		("render % (% s)".format(r[\name], r[\dur])).postln;
		t0 = Main.elapsedTime;
		score.recordNRT(osc, wav, nil, sr, "WAV", "float", opts, "", r[\dur], {
			var wall = Main.elapsedTime - t0;
			("RENDERED % wall % s for % s audio (% x realtime)"
				.format(r[\name], wall.round(0.01), r[\dur], (wall / r[\dur]).round(0.001))).postln;
			File.use(outDir +/+ r[\name] ++ ".time", "w", { |f| f.write(wall.asString) });
			runNext.(i + 1);
		});
	};
};
runNext.(0);
)
