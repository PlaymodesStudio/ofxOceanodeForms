// Offline tests only. Args: changed SynthDef directory, shipped bank directory, output directory.
(
var changed=thisProcess.argv[0], banks=thisProcess.argv[1], out=thisProcess.argv[2];
var opts=ServerOptions.new.numOutputBusChannels_(2).numInputBusChannels_(0).sampleRate_(48000).memSize_(65536).numControlBusChannels_(2048).numWireBufs_(4096);
var jobs=List[], next, sampleFile=out+/+"sample_input.wav", audioFile;
File.mkdir(out);
audioFile=SoundFile.new.headerFormat_("WAV").sampleFormat_("float").numChannels_(1).sampleRate_(48000);
audioFile.openWrite(sampleFile);
audioFile.writeData(FloatArray.fill(48000,{|i|sin(2pi * 60 * i / 48000)*exp(i.neg / 24000)}));audioFile.close;
[
    ["kick",1,0,0.3,0.45,160,48],
    ["kick_duck",1,0.9,0.3,0.45,160,48],
    ["kick_gated",1,0,0.3,0.45,160,48],
    ["sample",0,0,0.3,0.45,160,48],
    ["sample_gated",0,0,0.3,0.45,160,48],
    ["short_kick",1,0,0.3,0.1,300,80],
    ["baseline",1,0,0,0.45,160,48],
    ["ducked",1,0.9,0,0.45,160,48],
    ["no_sample_duck",0,0.9,0,0.45,160,48]
].do{|config|
    var name=config[0], source=config[1], depth=config[2], amp=config[3], decay=config[4], start=config[5], end=config[6];
    var score=List[], pitches=84!512, gains=0!512, amps=0!512;
    gains[0]=1;amps[0]=0.1;
    score.add([0,[\d_load,changed+/+"FormsKick.scsyndef"]]);
    score.add([0,[\d_load,changed+/+"FormsMaster.scsyndef"]]);
    score.add([0,[\d_load,banks+/+"FormsBank512.scsyndef"]]);
    if(name.beginsWith("sample")){
        score.add([0,[\b_allocReadChannel,0,sampleFile,0,0,0]]);
    }{score.add([0,[\b_alloc,0,1,1]]);};
    score.add([0,[\c_set,1024,0]]);
    score.add([0.02,[\s_new,\FormsKick,900,1,0,\out,104,\bufl,0,\bufr,0,\duckbus,1024,\duckdecay,0.5,\duckcurve,0,
        \amp,amp,\source,source,\synthstart,start,\synthend,end,\synthdecay,decay,\synthclick,0,\synthnoise,0,\synthdrive,0]]);
    if(amp==0){
        score.add([0.02,[\s_new,\FormsBank512,901,1,0,\ampbus,0,\out,100,\wetout,106,\duckbus,1024,\gain,1,\detune,0,\lag,0]]);
        score.add([0.02,[\n_setn,901,\pitch,512]++pitches]);
        score.add([0.02,[\n_setn,901,\gl,512]++gains]);score.add([0.02,[\n_setn,901,\gr,512]++gains]);
        score.add([0.02,[\c_setn,0,512]++amps]);
    };
    score.add([0.02,[\s_new,\FormsMaster,902,1,0,\in,100,\kickin,104,\out,0,\duckbus,1024,\duckdepth,depth,
        \kickgain,1,\limit,0,\volume,1,\bcon,0]]);
    score.add([0.3,[\n_set,900,\t_trig,1]]);score.add([1.3,[\c_set,1024,0]]);
    if(name.endsWith("gated")){
        score.add([0.36,[\n_set,900,\gate,0]]);score.add([0.45,[\n_set,900,\gate,1]]);
    };
    jobs.add([name,score.asArray]);
};
next={
    if(jobs.isEmpty){"KICK_DUCK_TESTS_RENDERED".postln;0.exit;}{
        var job=jobs.removeAt(0);
        Score(job[1]).recordNRT(outputFilePath:out+/+(job[0]++".wav"),sampleRate:48000,headerFormat:"WAV",sampleFormat:"float",options:opts,duration:1.3,
            action:{next.value;});
    };
};next.value;
)
