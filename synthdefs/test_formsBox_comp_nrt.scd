// Offline (NRT) test of FormsMaster's bus compressor (MixerPro's). Never plays audio.
// Run:  perl -e 'alarm shift; exec @ARGV' 300 sclang -D test_formsBox_comp_nrt.scd [outdir]
// A 220 Hz sine at -6 dBFS peak through FormsMaster (limiter off): compressor off, then on
// (threshold -18 dB, ratio 4, knee 0.01): about (18 - 6) x (1 - 1/4) = 9 dB less, and the
// gain-reduction bus (written to output channel 2 by a tap) shows it. Mix 0.5: in between.
(
var here, defDir, outDir, opts, tone, tap, renders, runNext, sr = 48000;
here = PathName(thisProcess.nowExecutingPath).pathOnly.standardizePath;
if(here.isEmpty) { here = File.getcwd };
defDir = here +/+ "CompiledSynthdefs/Forms";
outDir = thisProcess.argv[0] ?? { "/tmp/forms_comp_tests" };
File.mkdir(outDir);
opts = ServerOptions.new.numOutputBusChannels_(3).numInputBusChannels_(0).sampleRate_(sr)
	.numControlBusChannels_(4096).numAudioBusChannels_(256).blockSize_(64);
tone = SynthDef(\formsTestTone2, { Out.ar(\out.kr(0), SinOsc.ar([220, 220], 0, \amp.kr(0.5))) });
tap = SynthDef(\formsTestGrTap, { Out.ar(\out.kr(2), K2A.ar(In.kr(\bus.kr(0)))) });
renders = [
	(name: "c_off", args: [\bcon, 0]),
	(name: "c_on", args: [\bcon, 1, \bcthr, -18, \bcratio, 4, \bcknee, 0.01]),
	(name: "c_mix", args: [\bcon, 1, \bcthr, -18, \bcratio, 4, \bcknee, 0.01, \bcmix, 0.5])
];
runNext = { |i = 0|
	if(i >= renders.size) {
		("python3 -I " ++ (here +/+ "test_formsBox_comp.py").quote ++ " " ++ outDir.quote).systemCmd;
		0.exit;
	} {
		var r = renders[i];
		var msgs = [
			[0, [\d_loadDir, defDir]], [0, [\d_recv, tone.asBytes]], [0, [\d_recv, tap.asBytes]],
			[0.01, [\s_new, \formsTestTone2, 900, 0, 0, \out, 100, \amp, 0.5]],
			[0.01, [\s_new, \FormsMaster, 960, 3, 900, \in, 100, \kickin, 104, \out, 0, \limit, 0, \grbus, 10] ++ r[\args]],
			[0.01, [\s_new, \formsTestGrTap, 970, 3, 960, \out, 2, \bus, 10]],
			[2, [\c_set, 0, 0]]];
		Score(msgs).recordNRT(outDir +/+ r[\name] ++ ".osc", outDir +/+ r[\name] ++ ".wav", nil, sr, "WAV", "float", opts, "", 2, {
			("RENDERED " ++ r[\name]).postln; runNext.(i + 1) });
	};
};
runNext.(0);
)
