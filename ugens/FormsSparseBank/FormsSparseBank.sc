// Forms Box's oscillator bank as one UGen: see FormsSparseBank.cpp.
// amps, pitch, gl, gr, duckw: arrays of the same size (control rate); returns [left, right].
FormsSparseBank : MultiOutUGen {
    // mode 1 = sharp attacks (ramp: seconds between two amplitude arrivals; attack: seconds out of silence)
    *ar { arg amps, pitch, gl, gr, duckw, lag = 0.05, bend = 0.0, detune = 0.0, duck = 0.0, mode = 0.0, ramp = 0.004, attack = 0.001;
        var n = amps.size;
        if((pitch.size != n) or: { gl.size != n } or: { gr.size != n } or: { duckw.size != n }) {
            Error("FormsSparseBank: amps, pitch, gl, gr and duckw must have the same size").throw;
        };
        ^this.multiNewList(['audio', n, lag, bend, detune, duck] ++ amps ++ pitch ++ gl ++ gr ++ duckw ++ [mode, ramp, attack])
    }
    init { arg ... theInputs;
        inputs = theInputs;
        ^this.initOutputs(2, rate)
    }
}
