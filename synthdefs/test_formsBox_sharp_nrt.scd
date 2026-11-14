// Offline (NRT) test of FormsBank512's "sharp attacks" mode (FormsSparseBank mode 1). Never plays audio.
// Run:  perl -e 'alarm shift; exec @ARGV' 600 sclang -D test_formsBox_sharp_nrt.scd <outdir> <plugin dir> <scsynth folder: has bin/scsynth and bin/plugins> [old plugin dir]
// Renders, with the plugin in <plugin dir>:
//   c_hit  classic: one partial 0 -> 1 at 0.5 s (one message), -> 0 at 0.7 s, lag 1/30
//   s_hit  sharp:   the same messages, sharp 1 (ramp 1/240, attack 1 ms); s_soft: attack 20 ms; s_chord: 4 partials
//   c_fade classic: a fade in 0 -> 1 over 1 s sent 60 times per second
//   s_fade sharp:   the same fade sent 240 times per second
//   c_old  (if an old plugin dir is given) c_hit with that plugin: must equal c_hit sample for sample
// then test_formsBox_sharp.py checks them.
(
var here, defDir, outDir, plug, oldPlug, mkOpts, renders, runNext, mkBank, n = 512, sr = 48000;
here = PathName(thisProcess.nowExecutingPath).pathOnly.standardizePath;
defDir = here +/+ "CompiledSynthdefs/Forms";
outDir = thisProcess.argv[0]; plug = thisProcess.argv[1]; oldPlug = thisProcess.argv[3];
Server.program = (thisProcess.argv[2] +/+ "bin/scsynth").quote; Score.program = Server.program;
File.mkdir(outDir);
mkOpts = { |p| ServerOptions.new.numOutputBusChannels_(2).numInputBusChannels_(0).sampleRate_(sr)
	.maxSynthDefs_(4096).numControlBusChannels_(16384).numAudioBusChannels_(1024).numWireBufs_(4096).blockSize_(64)
	.memSize_(262144).ugenPluginsPath_([p, thisProcess.argv[2] +/+ "bin/plugins"]) };
mkBank = { |t, id, extra = #[]|
	var pitches = Array.fill(n, { |i| 30 + (i * 70 / n) }), g = 0 ! n;
	[0, 146, 293, 439].do { |i, k| pitches[i] = [40, 60, 80, 100][k]; g[i] = 1 };
	[
		[t, [\s_new, \FormsBank512, id, 1, 0, \ampbus, 0, \out, 0, \duckbus, 4000, \gain, 0.2, \lag, 1/30] ++ extra],
		[t, [\n_setn, id, \pitch, n] ++ pitches], [t, [\n_setn, id, \gl, n] ++ g], [t, [\n_setn, id, \gr, n] ++ g], [t, [\n_setn, id, \duckw, n] ++ (0 ! n)]
	]
};
renders = List[];
// the hits: one high partial (index 439, MIDI 100, 2637 Hz: its envelope reads directly off the wave);
// s_chord: the four partials together (they must start aligned, at zero)
[["c_hit", [], plug, [439]], ["s_hit", [\sharp, 1, \ramp, 1/240, \attack, 0.001], plug, [439]], ["c_old", [], oldPlug, [439]],
 ["s_chord", [\sharp, 1, \ramp, 1/240, \attack, 0.001], plug, [0, 146, 293, 439]], ["s_soft", [\sharp, 1, \ramp, 1/240, \attack, 0.02], plug, [439]]].do { |r|
	if(r[2].notNil) { renders.add((name: r[0], dur: 1.2, plug: r[2], score: {
		var amps = 0 ! n; r[3].do { |i| amps[i] = 1 };
		[[0, [\d_loadDir, defDir]], [0, [\c_setn, 0, n] ++ (0 ! n)], [0, [\c_set, 4000, 0]]] ++ mkBank.(0, 1000, r[1])
		++ [[0.5, [\c_setn, 0, n] ++ amps], [0.7, [\c_setn, 0, n] ++ (0 ! n)], [1.2, [\c_set, 4000, 0]]]
	})) }
};
[["c_fade", [], 60], ["s_fade", [\sharp, 1, \ramp, 1/240, \attack, 0.001], 240]].do { |r|
	renders.add((name: r[0], dur: 1.6, plug: plug, score: {
		var rate = r[2], msgs = List[];
		// a fade of the 1 kHz-ish partial (index 293, MIDI 80) alone: 0.01 -> 1 between 0.2 and 1.2 s
		(rate + 1).do { |k| var a = 0 ! n; a[293] = 0.01 + (0.99 * k / rate); msgs.add([0.2 + (k / rate), [\c_setn, 0, n] ++ a]) };
		[[0, [\d_loadDir, defDir]], [0, [\c_setn, 0, n] ++ (0 ! n)], [0, [\c_set, 4000, 0]]] ++ mkBank.(0, 1000, r[1]) ++ msgs.asArray ++ [[1.6, [\c_set, 4000, 0]]]
	}))
};
runNext = { |i|
	if(i >= renders.size) {
		("python3 " ++ (here +/+ "test_formsBox_sharp.py").quote ++ " " ++ outDir.quote).unixCmd({ |res| 0.exit }, true);
	} {
		var r = renders[i], sc = Score(r.score.value);
		sc.recordNRT(outDir +/+ r.name ++ ".osc", outDir +/+ r.name ++ ".wav", sampleRate: sr, headerFormat: "WAV", sampleFormat: "float",
			options: mkOpts.(r.plug), duration: r.dur, action: { runNext.(i + 1) });
	}
};
runNext.(0);
)
