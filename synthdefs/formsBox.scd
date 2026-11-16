// Forms Box (ESPECTRES 2027) synth defs: the image column turned into sound.
// Compile:  ./compile.sh   (the addon's bundled sclang with the Oceanode classes and FormsSparseBank;
//           writes CompiledSynthdefs/Forms/*.scsyndef next to this file)
// The server needs FormsSparseBank.scx (ugens/FormsSparseBank) in its Extensions folder.
// See README.md for the controls. All controls lower case. Stereo throughout.
//
// Faithful ports of the Oceanode patch chain (OceanAudio/.../SynthdefSources):
//   Additive1080_ -> FormsBank512, BasicSampler + duck env -> FormsKick,
//   LongReverb + eq13 + CompressorB + EQ5 + filter13 -> FormsFX, polymixer/output -> FormsMaster.
(
var dir, t0;
dir = thisProcess.nowExecutingPath.dirname +/+ "CompiledSynthdefs" +/+ "Forms";
File.mkdir(dir);

// ---------------------------------------------------------------------------
// FormsBank512: 512 partials, a left and a right sine each, in one FormsSparseBank unit.
// amp of partial i = Lag(In.kr(ampbus)[i], lag) * (1 - duckw[i] * duck)
// L = sum SinOsc((pitch+bend).midicps) * amp * gl ; R = same at pitch+bend+detune * gr
// out = [L, R] * gain * level, split equal-power by send (this layer's reverb amount, 0..1):
// cos to out (dry), sin to wetout (FormsFX reverberates it) -- the same as the old crossfade
// XFade2(dry, reverb(dry), send * 2 - 1), the reverb being linear
// ---------------------------------------------------------------------------
t0 = Main.elapsedTime;
SynthDef(\FormsBank512, {
	var n = 512;
	var amps, pitch, gl, gr, duckw, duck, sig, send;
	amps   = In.kr(\ampbus.kr(0), n);
	pitch  = \pitch.kr(60 ! n);
	gl     = \gl.kr(0 ! n);
	gr     = \gr.kr(0 ! n);
	duckw  = \duckw.kr(0 ! n);
	duck   = In.kr(\duckbus.kr(0), 1);

	// FormsSparseBank (ugens/FormsSparseBank): the 1024 sines, the amplitude Lag, the duck and the
	// NaN / range guards in one unit that only runs the partials that sound. Same maths as the
	// SinOsc graph it replaces (random start phases, gains ramped across the block).
	// sharp (0 / 1): the bank's "sharp attacks" mode (amplitudes several times per frame, a line
	// between them over `ramp` s, `attack` s out of silence; phases free); 0 = the bank as it was.
	sig = FormsSparseBank.ar(amps, pitch, gl, gr, duckw, \lag.kr(0.05), \bend.kr(0), \detune.kr(0.0746), duck,
		\sharp.kr(0), \ramp.kr(0.004), \attack.kr(0.001));

	sig = Sanitize.ar(sig * \gain.kr(0.0216) * \level.kr(1));
	send = \send.kr(0, 1/30).clip(0, 1) * 0.5pi;
	Out.ar(\out.kr(0), sig * send.cos);
	Out.ar(\wetout.kr(0), sig * send.sin);
}).writeDefFile(dir);
("FormsBank512 built+written in % s".format((Main.elapsedTime - t0).round(0.01))).postln;

// ---------------------------------------------------------------------------
// FormsKick: stereo one-shot (two mono buffers) + the kick duck envelope.
// ---------------------------------------------------------------------------
SynthDef(\FormsKick, {
	var trig, rate, bl, br, sig, env, time, attack, bodyEnv, frequency, phase, noise, body, drive;
	trig = \t_trig.tr(0);
	rate = \rate.kr(1);
	bl = \bufl.kr(0);
	br = \bufr.kr(1);
	sig = [
		PlayBuf.ar(1, bl, rate * BufRateScale.kr(bl), trig, 0, 0, 0),
		PlayBuf.ar(1, br, rate * BufRateScale.kr(br), trig, 0, 0, 0)
	];
	// V2-style synthetic kick: falling sine body, short click, filtered noise and drive.
	// Runs on the server at audio rate and needs no sample or rendered image.
	time = Sweep.ar(trig, 1);
	attack = (time / 0.00025).clip(0, 1);
	bodyEnv = attack * exp(-6.9 * time / \synthdecay.kr(0.45).clip(0.02, 3));
	frequency = (\synthend.kr(48).clip(20, 300) +
		((\synthstart.kr(160).clip(20, 1000) - \synthend.kr(48)) * exp(time / -0.035))) * rate;
	phase = Phasor.ar(trig, frequency * SampleDur.ir, 0, 1);
	noise = WhiteNoise.ar;
	body = ((1 - \synthnoise.kr(0).clip(0, 1)) * sin(phase * 2pi) * bodyEnv)
		+ (\synthnoise.kr(0).clip(0, 1) * BPF.ar(noise, 1500, 0.7) * 2 * bodyEnv)
		+ (\synthclick.kr(0.3).clip(0, 1) * attack * exp(time / -0.001) * (0.5 + (0.5 * noise)));
	drive = 1 + (\synthdrive.kr(0.1).clip(0, 1) * 8);
	body = Select.ar(\synthdrive.kr(0.1) > 0, [body, tanh(body * drive) / tanh(drive)]);
	sig = Select.ar(\source.kr(0).clip(0, 1), [sig, [body, body]]);
	// 1 right after a trigger, then a fast-then-slow decay to 0 over duckdecay s
	env = EnvGen.kr(Env([0, 1, 0], [0, \duckdecay.kr(1)], \duckcurve.kr(-4)), trig);
	Out.kr(\duckbus.kr(0), env);
	// PlayBuf starts playing when the synth is created: stay silent until the first trigger
	sig = sig * (PulseCount.kr(trig) > 0);
	// The same performance gate as image sonification, applied to the running
	// sample/synth output (not only to new triggers). A 1 ms edge avoids clicks.
	sig = sig * Lag.ar(K2A.ar(\gate.kr(1).clip(0, 1)), 0.001);
	Out.ar(\out.kr(0), Sanitize.ar(sig * \amp.kr(0.15)));
}).writeDefFile(dir);

// ---------------------------------------------------------------------------
// FormsFX: dry (in) + MicroSpace reverb of what the layers send (wetin) -> eq13 -> CompressorB
//          crossfade -> EQ5 high shelf -> filter13 (RLPF) -> * gain
// ---------------------------------------------------------------------------
SynthDef(\FormsFX, {
	var dry, temp, wet, sig, src, thresh, ratio, attack, release, knee, makeup,
	    amplitudeDb, over, xk, gainDb, comp, revtime, send;

	dry = Sanitize.ar(In.ar(\in.kr(0), 2));   // before LeakDC: a NaN would stay in its state
	dry = LeakDC.ar(dry);

	// the reverb on what the layers send (each its own amount: FormsBank512's send)
	revtime = \revtime.kr(4);
	send = LeakDC.ar(Sanitize.ar(In.ar(\wetin.kr(0), 2)));
	// MicroSpace (OceanodeMicroSpace.box, the Box synths' reverb), all wet: the dry part of the
	// layers is already on `in`. revtime is its decay (RT60, s); predelay in seconds as before;
	// revlowpass / revhipass (midi) are the tone after it; the rest are the Box reverb's controls.
	// Matched to the reverb it replaces on the same send (measured, NRT): decay = revtime / 2 gives
	// about the same tail length for a given Reverb Time, revgain 0.65 the same wet level.
	temp = OceanodeMicroSpace.box(send,
		\revsize.kr(30, 0.05), (revtime * 0.5).max(0.05),
		\predelay.kr(0, 0.05) * 1000, \revlowpass.kr(123.2, 0.05).midicps.clip(20, 20000),
		\revposition.kr(0.8, 0.05).clip(0, 1), \revspread.kr(0.15, 0.05).clip(-1, 1),
		1, \revhighdamp.kr(6, 0.05), \revlowdamp.kr(0, 0.05),
		\revmodfreq.kr(0.2, 0.05), \revspin.kr(1, 0.05), \revwander.kr(0.25, 0.05), 1);
	wet = HPF.ar(temp, \revhipass.kr(17).midicps) * \revgain.kr(0.65, 0.05);
	sig = dry + wet;

	// eq13 (rs 1; the original's `wet.tanh;` is a no-op statement, so no tanh here)
	sig = BLowShelf.ar(sig, \lowf.kr(80), 1, \lowdb.kr(10));
	sig = BPeakEQ.ar(sig, \midlowf.kr(270), 1, \midlowdb.kr(3.69));
	sig = BPeakEQ.ar(sig, \midf.kr(800), 1, \middb.kr(2.69));
	sig = BPeakEQ.ar(sig, \midhighf.kr(2000), 1, \midhighdb.kr(1.34));
	sig = BHiShelf.ar(sig, \highf.kr(7623), 1, \highdb.kr(2.69));

	// CompressorB math (send 1, knee 0 -> 0.01), crossfaded: dry*(1-comp) + compressed*comp
	src = sig;
	thresh  = \compthr.kr(-24.18);
	ratio   = \compratio.kr(15.22).max(1);
	attack  = \compatk.kr(0.0045);
	release = \comprel.kr(0.01);
	knee    = 0.01;
	makeup  = \compmakeup.kr(1);
	amplitudeDb = Amplitude.ar(src, attack, release).max(-120.dbamp).ampdb;
	over   = amplitudeDb - thresh;
	xk     = (over + (knee * 0.5)).clip(0, knee);
	gainDb = (1 - ratio.reciprocal) * ((xk.squared / (2 * knee)) + (over - (knee * 0.5)).max(0));
	comp = src * gainDb.neg.dbamp * makeup;
	sig = LinXFade2.ar(src, comp, \comp.kr(0, 1/30).clip(0, 1) * 2 - 1);

	// EQ5: only the high shelf is used (3328 Hz, -6.72 dB, width 1)
	sig = BHiShelf.ar(sig, \shelff.kr(3328), 1, \shelfdb.kr(-6.72));

	// filter13 type 0: RLPF at midi 120.36, rq 1
	sig = RLPF.ar(sig, \lpfpitch.kr(120.36, 0.05).midicps.clip(20, 20000), \lpfrq.kr(1));

	sig = sig * \gain.kr(1.5, 1/30);
	Out.ar(\out.kr(0), Sanitize.ar(sig));
}).writeDefFile(dir);

// ---------------------------------------------------------------------------
// FormsMaster: (fx + kick * kickgain) -> bus compressor -> * fade * [lgain, rgain] -> Limiter -> * volume
// The bus compressor is MixerPro's (ofxOceanodeSuperCollider mixerpro.scd, mixerProComp, copied
// here): log-domain soft knee, peak/RMS detector with a sidechain HPF, linked over L/R, smooth
// decoupled ballistics, optional auto makeup. Off by default (bcon 0). Its gain reduction (dB)
// goes to the control bus `grbus` (the Sonification tab's meter).
// ---------------------------------------------------------------------------
SynthDef(\FormsMaster, {
	var sig, key, pk, rms, lvl, over, slope, xk, gr, grs, gain;
	var thr, ratio, knee, att, rel, makeup, auto, rmsAmt, mix, on, range = 40;
	// Duck the image mix AFTER its reverb, BEFORE adding the kick. The kick is never ducked.
	sig = In.ar(\in.kr(0), 2) * (1 - (Sanitize.kr(In.kr(\duckbus.kr(0))).clip(0, 1) * \duckdepth.kr(0).clip(0, 1)));
	sig = sig + (In.ar(\kickin.kr(0), 2) * \kickgain.kr(1.5, 1/30));
	sig = Sanitize.ar(sig);

	thr    = \bcthr.kr(-18, 0.05);
	ratio  = \bcratio.kr(3, 0.05).max(1);
	knee   = \bcknee.kr(6, 0.05).max(0.01);
	att    = \bcatk.kr(10).max(0.01) * 0.001;
	rel    = \bcrel.kr(150).max(1) * 0.001;
	makeup = \bcmakeup.kr(0, 0.05);
	auto   = \bcauto.kr(0);
	rmsAmt = \bcrms.kr(0, 0.05);
	mix    = \bcmix.kr(1, 0.05);
	on     = \bcon.kr(0, 0.03);
	key = HPF.ar(sig, \bchpf.kr(20).clip(10, 2000));
	pk  = key.abs;
	rms = OnePole.ar(key.squared, exp(-1 / (0.01 * SampleRate.ir))).sqrt * 2.sqrt;
	lvl = (pk + ((rms - pk) * rmsAmt)).max(-120.dbamp).reduce(\max).ampdb;
	over  = lvl - thr;
	slope = 1 - ratio.reciprocal;
	xk    = (over + (knee * 0.5)).clip(0, knee);
	gr    = (slope * ((xk.squared / (2 * knee)) + (over - (knee * 0.5)).max(0))).min(range);
	grs   = Lag.ar(LagUD.ar(gr, 0, rel * 6.9078), att * 6.9078);
	xk   = (thr.neg + (knee * 0.5)).clip(0, knee);
	gain = makeup + (auto * 0.5 * (slope * ((xk.squared / (2 * knee)) + (thr.neg - (knee * 0.5)).max(0))).min(range));
	gain = (gain - grs).dbamp;
	// no bus (-1): an index past every control bus, which Out skips (a negative index is cast to
	// an unsigned one: on arm64 -1 becomes 0 and would overwrite control bus 0)
	Out.kr(Select.kr(\grbus.kr(-1) >= 0, [1e9, \grbus.kr(-1)]), A2K.kr(grs * (on * mix > 0.001)));
	sig = LinXFade2.ar(sig, sig * gain, (mix * on).clip(0, 1) * 2 - 1);

	sig = sig * \fade.kr(1, 1/30) * [\lgain.kr(1, 1/30), \rgain.kr(1, 1/30)];
	sig = Sanitize.ar(sig);
	sig = Select.ar(\limit.kr(1) > 0.5, [sig, Limiter.ar(sig, 0.99, 0.01)]);
	sig = sig * \volume.kr(1, 1/30);
	Out.ar(\out.kr(0), sig);
}).writeDefFile(dir);

("Forms synthdefs written to " ++ dir).postln;
PathName(dir).files.do { |f| (f.fileName ++ "  " ++ File.fileSize(f.fullPath) ++ " bytes").postln };
0.exit;
)
