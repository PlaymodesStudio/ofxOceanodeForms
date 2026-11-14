// Offline (NRT) test of the per-layer reverb send. Never plays audio.
// Run:  perl -e 'alarm shift; exec @ARGV' 300 sclang -D test_formsBox_send_nrt.scd [outdir]
// Two FormsBank512 (one partial each: A at MIDI 69 = 440 Hz, B at MIDI 76 = 659 Hz) play for 1 s
// into FormsFX + FormsMaster, as the engine wires them (dry bus 100, wet bus 106). The reverb tail
// after they stop must hold only what was sent. Then test_formsBox_send.py checks the WAVs.
(
var here, defDir, outDir, opts, renders, runNext, bank, n = 512, sr = 48000;
here = PathName(thisProcess.nowExecutingPath).pathOnly.standardizePath;
if(here.isEmpty) { here = File.getcwd };
defDir = here +/+ "CompiledSynthdefs/Forms";
outDir = thisProcess.argv[0] ?? { "/tmp/forms_send_tests" };
File.mkdir(outDir);
opts = ServerOptions.new
	.numOutputBusChannels_(2).numInputBusChannels_(0)
	.sampleRate_(sr).memSize_(65536)
	.numControlBusChannels_(4096).numAudioBusChannels_(256).blockSize_(64)
	// FormsBank512 needs the FormsSparseBank plugin (the app's Extensions folder)
	.memSize_(262144).ugenPluginsPath_([(here +/+ "../../../apps/Playmodes/OceanodeScroller/bin/data/Supercollider/Extensions/OceanodeForms").standardizePath, Platform.resourceDir +/+ "plugins", Platform.userExtensionDir]);

// a bank whose partial 0 is at `pitch`, amp from control bus `ampbus` (partial 0 only), send `send`
bank = { |id, ampbus, pitch, send|
	var p = Array.fill(n, { |i| 20 + (i * 0.1) }), g = 0 ! n;
	p[0] = pitch; g[0] = 1;
	[
		[0.01, [\s_new, \FormsBank512, id, 1, 0, \ampbus, ampbus, \out, 100, \wetout, 106, \duckbus, 4000, \send, send]],
		[0.01, [\n_setn, id, \pitch, n] ++ p],
		[0.01, [\n_setn, id, \gl, n] ++ g],
		[0.01, [\n_setn, id, \gr, n] ++ g],
		[0.01, [\n_setn, id, \duckw, n] ++ (0 ! n)],
		[0, [\c_set, ampbus, 1]],
		[1.0, [\c_set, ampbus, 0]]
	]
};
renders = [
	(name: "s_dry", banks: [[1000, 0, 69, 0]]),
	(name: "s_wet", banks: [[1000, 0, 69, 1]]),
	(name: "s_half", banks: [[1000, 0, 69, 0.5]]),
	(name: "s_mixed", banks: [[1000, 0, 69, 0], [1001, 512, 76, 1]])
];
runNext = { |i = 0|
	if(i >= renders.size) {
		("python3 -I " ++ (here +/+ "test_formsBox_send.py").quote ++ " " ++ outDir.quote).systemCmd;
		0.exit;
	} {
		var r = renders[i], msgs;
		msgs = [[0, [\d_loadDir, defDir]], [0, [\c_set, 4000, 0]]];
		r[\banks].do { |b| msgs = msgs ++ bank.(*b) };
		msgs = msgs ++ [
			[0.01, [\s_new, \FormsFX, 950, 1, 0, \in, 100, \wetin, 106, \out, 102, \revtime, 4]],
			[0.01, [\s_new, \FormsMaster, 960, 1, 0, \in, 102, \kickin, 104, \out, 0, \limit, 0]],
			[3, [\c_set, 0, 0]]];
		msgs = msgs.collect { |m, k| [m, k] }.sort { |a, b|
			(a[0][0] < b[0][0]) or: { (a[0][0] == b[0][0]) and: { a[1] < b[1] } } }.collect { |p| p[0] };
		Score(msgs).recordNRT(outDir +/+ r[\name] ++ ".osc", outDir +/+ r[\name] ++ ".wav", nil, sr, "WAV", "float", opts, "", 3, {
			("RENDERED " ++ r[\name]).postln; runNext.(i + 1) });
	};
};
runNext.(0);
)
