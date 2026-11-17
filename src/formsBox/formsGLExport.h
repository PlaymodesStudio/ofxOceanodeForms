// Included inside formsGLBox: native scene export and its seed library.
    std::future<std::vector<forms::exportmedia::Layer>> exportRender;
    std::future<std::string> exportMedia;
    forms::json exportRecipe, recalledExport;
    std::string exportBase, exportLibraryBase, glExportStatus;
    char exportSeedText[32] = {};
    bool glExportAudio = false, glExportVideo = false, glExportBusy = false;
    bool glExportSeparate = false, glExportUseRecall = false;
    ofShortPixels glExportTiff;
    ofImage glExportPreview;
    std::vector<ofTexture> glExportTextures;
    std::vector<std::vector<formscover::Mask>> glExportMasks;
    std::vector<ofFbo> glExportFrames;
    formscover::Compositor glExportCovers;
    std::unique_ptr<forms::exportmedia::VideoWriter> glVideoWriter;
    forms::Post glExportPost;
    ofFbo glExportComposite, glExportCoverage, glExportBackground;
    ofShader glExportBgShader;
    int glVideoFrame = 0, glVideoFrames = 0;
    bool glVideoEnding = false, glExportCapturing = false;
    std::vector<forms::exportmedia::Layer> glAudioLayers;
    forms::LayerFx glExportLayerFx;
    ofFbo glExportLayerFrame;

    std::filesystem::path seedFolder() const { return engine.rendererFolder / "exports" / "seeds"; }
    static forms::json soundJson(const forms::SoundSettings& s){
        forms::json j;
#define FORMS_EXPORT_SOUND(k) j[#k] = s.k
        FORMS_EXPORT_SOUND(bankGain); FORMS_EXPORT_SOUND(detune); FORMS_EXPORT_SOUND(loudnessBlend);
        FORMS_EXPORT_SOUND(lag); FORMS_EXPORT_SOUND(spreadLow); FORMS_EXPORT_SOUND(spreadHigh);
        FORMS_EXPORT_SOUND(duckBelow); FORMS_EXPORT_SOUND(duckDepth); FORMS_EXPORT_SOUND(duckShape); FORMS_EXPORT_SOUND(duckFullBand);
        FORMS_EXPORT_SOUND(duckBeats); FORMS_EXPORT_SOUND(duckCurve); FORMS_EXPORT_SOUND(kickOn);
        FORMS_EXPORT_SOUND(kickThreshold); FORMS_EXPORT_SOUND(kickStrip); FORMS_EXPORT_SOUND(kickMinGap);
        FORMS_EXPORT_SOUND(kickTune); FORMS_EXPORT_SOUND(kickGain); FORMS_EXPORT_SOUND(volume);
        FORMS_EXPORT_SOUND(limit); FORMS_EXPORT_SOUND(lrBalance);
#undef FORMS_EXPORT_SOUND
        for(auto& l : s.layers) j["layers"].push_back({{"on", l.on}, {"level", l.level}, {"pan", l.pan}, {"spread", l.spread}, {"reverb", l.reverb}});
        return j;
    }
    static forms::SoundSettings readSound(const forms::json& j){
        forms::SoundSettings s;
#define FORMS_EXPORT_SOUND(k) s.k = j.value(#k, s.k)
        FORMS_EXPORT_SOUND(bankGain); FORMS_EXPORT_SOUND(detune); FORMS_EXPORT_SOUND(loudnessBlend);
        FORMS_EXPORT_SOUND(lag); FORMS_EXPORT_SOUND(spreadLow); FORMS_EXPORT_SOUND(spreadHigh);
        FORMS_EXPORT_SOUND(duckBelow); FORMS_EXPORT_SOUND(duckDepth); FORMS_EXPORT_SOUND(duckShape); FORMS_EXPORT_SOUND(duckFullBand);
        FORMS_EXPORT_SOUND(duckBeats); FORMS_EXPORT_SOUND(duckCurve); FORMS_EXPORT_SOUND(kickOn);
        FORMS_EXPORT_SOUND(kickThreshold); FORMS_EXPORT_SOUND(kickStrip); FORMS_EXPORT_SOUND(kickMinGap);
        FORMS_EXPORT_SOUND(kickTune); FORMS_EXPORT_SOUND(kickGain); FORMS_EXPORT_SOUND(volume);
        FORMS_EXPORT_SOUND(limit); FORMS_EXPORT_SOUND(lrBalance);
#undef FORMS_EXPORT_SOUND
        s.layers.resize(j.at("layers").size());
        for(int i = 0; i < (int)s.layers.size(); ++i){ auto& l = j.at("layers").at(i); auto& d = s.layers[i];
            d.on = l.at("on"); d.level = l.at("level"); d.pan = l.at("pan"); d.spread = l.at("spread"); d.reverb = l.at("reverb"); }
        return s;
    }
    void drawGLExportTab(){
        ImGui::TextWrapped("Render the complete scene under the playhead at the requested resolution. Still images omit the playhead and pixel stretch, and retain colour, background and feathers. In video, the playhead stays at one-quarter width; each layer scrolls at its own bar rate, with stretch and feathers as in Output. Video spans the common loop of all active layer bar counts.");
        ImGui::TextUnformatted("Still images: 8-bit PNG + 16-bit/channel TIFF");
        ImGui::BeginDisabled(glExportBusy);
        ImGui::InputInt("Width", &exportWidth); exportWidth = ofClamp(exportWidth, 16, 16384);
        ImGui::InputInt("Height", &exportHeight); exportHeight = ofClamp(exportHeight, 16, 16384);
        ImGui::Checkbox("Separate transparent layers", &glExportSeparate);
        ImGui::Checkbox("Sound (48 kHz WAV)", &glExportAudio);
        ImGui::Checkbox("Video with audio (FHD fit, 60 fps, high quality)", &glExportVideo);
        if(ImGui::Button("Render and save")){
            auto pick = ofSystemSaveDialog("FormsExport.png", "Save scene export");
            if(pick.bSuccess) startGLExport(pick.getPath());
        }
        ImGui::Separator();
        ImGui::InputText("Recall export seed", exportSeedText, sizeof(exportSeedText), ImGuiInputTextFlags_CharsDecimal);
        if(ImGui::Button("Recall exact image")){
            try {
                std::string seed(exportSeedText);
                if(seed.empty() || seed.find_first_not_of("0123456789") != std::string::npos) throw std::runtime_error("Enter an exported seed number.");
                auto base = seedFolder() / seed;
                std::ifstream in(base.string() + ".json"); forms::json recipe; in >> recipe;
                if(recipe.value("format", "") != "FormsGLExport-1") throw std::runtime_error("This seed is not in this Forms export library.");
                ofPixels preview;
                if(!ofLoadImage(preview,base.string()+".png")) throw std::runtime_error("The saved PNG for this seed is missing.");
                int pw=preview.getWidth(),ph=preview.getHeight(); preview.resize(std::min(pw,960),std::max(1,ph*std::min(pw,960)/pw));
                bool arb=ofGetUsingArbTex(); ofDisableArbTex(); glExportPreview.clear(); glExportPreview.setFromPixels(preview); if(arb) ofEnableArbTex();
                recalledExport = recipe; glExportUseRecall = true;
                exportWidth = recipe.at("size").at(0); exportHeight = recipe.at("size").at(1);
                glExportStatus = "Recalled seed " + seed + ". Preview is the original saved PNG; export uses its frozen recipe.";
            } catch(const std::exception& e){ glExportStatus = e.what(); }
        }
        if(!recalledExport.is_null()) ImGui::Checkbox("Export recalled recipe", &glExportUseRecall);
        ImGui::EndDisabled();
        if(!glExportStatus.empty()) ImGui::TextWrapped("%s", glExportStatus.c_str());
        if(glExportPreview.isAllocated()){
            float w = ImGui::GetContentRegionAvail().x;
            w = std::min(w, 900.f);
            ImGui::Image((ImTextureID)(uintptr_t)glExportPreview.getTexture().getTextureData().textureID,
                ImVec2(w, w * glExportPreview.getHeight() / glExportPreview.getWidth()));
        }
    }
    void startGLExport(const std::string& target){
        try {
            if(exportWidth > postMaxTextureSize() || exportHeight > postMaxTextureSize()) throw std::runtime_error("Export exceeds this GPU's maximum texture size.");
            forms::json j;
            if(glExportUseRecall && !recalledExport.is_null()) j = recalledExport;
            else {
                j = {{"format", "FormsGLExport-1"}, {"layers", forms::json::array()}};
                auto first = auditionOn ? auditionImg : engine.schedule->layers[0].header;
                if(!first) throw std::runtime_error("Wait for a scene image under the playhead.");
                auto scene = engine.scenes.find(first->scene);
                if(scene == engine.scenes.end()) throw std::runtime_error("The current scene is unavailable.");
                j["stackOrder"] = forms::stackOrder(scene->second.layers);
                j["bpm"] = std::max(1.f, bpmOut.get()); j["bars"] = forms::barsJson(std::max(1.0 / forms::kBarGrid, first->bars));
                j["performance"] = livePerf(first); j["scene"] = first->scene;
                j["kickGate"]=head.gate;
                j["imageMul"] = postCtl.enabled ? postCtl.val.get() * (postCtl.useOpacity ? std::max(.001f, head.opacity) : 1.f) : head.opacity;
                j["reverbMult"] = soundCtl.reverbMult.get();
                forms::SoundSettings settings; soundCtl.apply(settings); j["sound"] = soundJson(settings);
                j["scsynth"]=ofToDataPath("Supercollider/Scsynth/bin/scsynth",true);
                // the server plugins the SynthDefs need (FormsSparseBank): the app's Extensions folder, as the live server gets
                j["ugens"]=ofToDataPath("Supercollider/Extensions",true);
                std::vector<std::string> names;
                std::error_code kickError;
                if(std::filesystem::is_directory(settings.kickFolder,kickError)) for(auto& entry:std::filesystem::directory_iterator(settings.kickFolder))
                    if(entry.is_regular_file()){
                        auto ext=ofToLower(entry.path().extension().string());
                        if(ext==".wav" || ext==".aif" || ext==".aiff") names.push_back(entry.path().filename().string());
                    }
                std::sort(names.begin(),names.end());
                int ki = kickIndexFor(j["performance"],names,true);
                auto sampleScene=kickScene();
                auto embedded=sampleScene?engine.kickFile(*sampleScene):forms::fs::path();
                j["kickFile"]=!embedded.empty()?embedded.string():ki>=0?(std::filesystem::path(settings.kickFolder)/names[ki]).string():"";
                j["kickLayer"]=forms::kickLayer(scene->second.layers);
                j["fx"]={{"revtime",soundCtl.revTime.get()},{"lowdb",soundCtl.lowShelf.get()},{"midlowdb",soundCtl.midLow.get()},
                    {"middb",soundCtl.mid.get()},{"midhighdb",soundCtl.midHigh.get()},{"shelfdb",soundCtl.highShelf.get()},{"lpfpitch",soundCtl.lowpass.get()}};
                j["master"]={{"bcon",soundCtl.compOn ? 1.f : 0.f},{"bcthr",soundCtl.compThr.get()},{"bcratio",soundCtl.compRatio.get()},
                    {"bcknee",soundCtl.compKnee.get()},{"bcatk",soundCtl.compAtk.get()},{"bcrel",soundCtl.compRel.get()},{"bcmakeup",soundCtl.compMakeup.get()},
                    {"bcauto",soundCtl.compAuto ? 1.f : 0.f},{"bcrms",soundCtl.compRms.get()},{"bchpf",soundCtl.compHpf.get()},{"bcmix",soundCtl.compMix.get()}};
                j["performance"]["SpectrumGain"]=perfOutValue("SpectrumGain",forms::perfF(j["performance"],"SpectrumGain",2));
                j["post"] = {{"enabled", postCtl.enabled.get()}, {"hue", postCtl.hue.get()}, {"sat", postCtl.sat.get()}, {"val", postCtl.val.get()},
                    {"levelMin", postCtl.levelMin.get()}, {"levelMax", postCtl.levelMax.get()}, {"gamma", postCtl.gamma.get()}, {"low", postCtl.low.get()}, {"high", postCtl.high.get()}};
                j["opacity"] = head.opacity;
                j["post"]["stretch"] = postCtl.stretch.get(); j["post"]["stretchBlend"] = postCtl.stretchBlend.get(); j["post"]["line"] = postCtl.line.get();
                j["post"]["useOpacity"] = postCtl.useOpacity.get();
                j["post"]["lineWidth"] = postCtl.lineWidth.get() / std::max(1.f, (float)viewW);
                j["post"]["lineSoft"] = postCtl.lineSoft.get() / std::max(1.f, (float)viewW);
                j["post"]["lineSoftV"] = postCtl.lineSoftV.get() / std::max(1.f, (float)viewH);
                j["post"]["lineAlpha"] = postCtl.lineAlpha.get();
                j["post"]["lineColor"] = {postCtl.lineR.get(), postCtl.lineG.get(), postCtl.lineB.get()};
                j["post"]["background"] = {postCtl.bgR.get(), postCtl.bgG.get(), postCtl.bgB.get()};
                j["post"]["paper"] = postCtl.paper.get();
                for(int e=0;e<4;++e){ j["post"]["edgeWidth"].push_back(postCtl.featherW[e].get()); j["post"]["edgeCurve"].push_back(postCtl.featherK[e].get()); }
                for(int i : forms::stackOrder(scene->second.layers)){
                    if((auditionOn && i != 0) || !layerAudible(i)) continue;
                    auto im = i == 0 ? first : rings[i - 1].current;
                    if(!im || im->scene != first->scene) throw std::runtime_error("Wait until all scene layers reach the playhead.");
                    forms::json req;
                    if(i == 0 && auditionOn && auditionRecipe) req = *auditionRecipe;
                    else { auto r = recipes.find(im->id); if(r != recipes.end()) req = *r->second;
                        else { std::ifstream in(im->json); if(in) in >> req; } }
                    if(!req.is_object()) throw std::runtime_error("The recipe for layer " + std::to_string(i + 1) + " is unavailable.");
                    if(!auditionOn) req = forms::filterItems(req, currentFilter());
                    auto& ld = scene->second.layers[i]; auto fx = layerFxSettings(i, 4.0 * im->bars);
                    forms::json perlin={{"amount",fx.maskAmount},{"width",fx.maskWidth},{"height",fx.maskHeight},{"contrast",fx.maskContrast},{"threshold",fx.maskThreshold},{"blur",fx.maskBlur/std::max(1.f,(float)viewH)},{"invert",fx.maskInvert},{"seed",fx.maskSeed},{"speed",fx.maskSpeed}};
                    j["layers"].push_back({{"recipe", req}, {"index", i}, {"root", scene->second.root}, {"gain", ld.gain}, {"blend", ld.blend}, {"cover", ld.cover}, {"perlin",perlin},
                        {"echo", {{"beats", postCtl.fxOn ? postCtl.echoBeats[i].get() : 0.f}, {"feedback", fx.echoFeedback}, {"mix", fx.echoMix}, {"low", fx.echoLow}, {"high", fx.echoHigh}}},
                        {"fx", {fx.hBlur / std::max(1.f, (float)viewW), fx.vBlur / std::max(1.f, (float)viewH), postCtl.fxOn ? postCtl.trailBeats[i].get() : 0.f, fx.trailLevel}}});
                }
            }
            if(j.at("layers").empty()) throw std::runtime_error("No audible scene layers to export.");
            auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            std::string seed = std::to_string(stamp);
            while(std::filesystem::exists(seedFolder() / (seed + ".json"))) seed = std::to_string(++stamp);
            j["seed"] = seed; j["size"] = {exportWidth, exportHeight};
            std::vector<double> barCounts; for(auto& l : j.at("layers")) barCounts.push_back(l.at("recipe").value("bars", 8.0));
            j["loopBars"] = forms::exportmedia::loopBars(barCounts);
            j["duration"] = j.at("loopBars").get<double>() * 240.0 / j.at("bpm").get<double>();
            if((glExportAudio || glExportVideo) && j.at("duration").get<double>() * 48000 * 8 > 4294967240.0) throw std::runtime_error("The common loop exceeds the 4 GB WAV limit. Reduce the layer bar counts, or export PNG only.");
            j["imageFormats"]={{"pngBitsPerChannel",8},{"tiffBitsPerChannel",16},{"processing","RGBA32F"},{"generatorBitsPerChannel",8}};
            j["audio"] = glExportAudio; j["video"] = glExportVideo; j["separate"] = glExportSeparate;
            for(auto& l : j["layers"]){ auto& r = l["recipe"]; if(!r.contains("refSize")) r["refSize"] = r.at("size"); r["size"] = j["size"]; r.erase("result");
                // a phrase's render is exported whole, as one page of the phrase's length (not sliced)
                r.erase("viewWidth"); r.erase("slices"); r.erase("pageBars"); }
            std::filesystem::path path(target);
            exportBase = (path.parent_path() / (path.stem().string() + "_s" + seed)).string();
            exportLibraryBase = (seedFolder() / seed).string();
            exportRecipe = j;
            loadFont(); loadSchemas(); auto sch = schemas; auto glyphs = font;
            if(!sch) throw std::runtime_error("Renderer schemas unavailable.");
            glExportBusy = true; glExportStatus = "Regenerating scene at " + std::to_string(exportWidth) + " x " + std::to_string(exportHeight) + "...";
            exportRender = std::async(std::launch::async, [this, j, sch, glyphs]{
                std::unique_lock<std::mutex> largeLock(largeRenderMutex,std::defer_lock);
                if((size_t)j.at("size").at(0).get<int>()*j.at("size").at(1).get<int>() >= (size_t)64*1024*1024) largeLock.lock();
                std::vector<forms::exportmedia::Layer> layers;
                for(auto& l : j.at("layers")){
                    formsgl::Sketch sk(*sch); sk.font = glyphs;
                    auto r = sk.render(l.at("recipe")); if(!r.error.empty()) throw std::runtime_error(r.error);
                    forms::exportmedia::Layer out; out.recipe = l.at("recipe"); out.index = l.at("index"); out.root = l.at("root"); out.gain = l.at("gain");
                    out.w = r.w; out.h = r.h; out.rgba = std::move(r.rgba); out.covers = std::move(r.covers); layers.push_back(std::move(out));
                } return layers;
            });
        } catch(const std::exception& e){ glExportBusy = false; glExportStatus = e.what(); }
    }
    void releaseGLExportBuffers(){
        glExportTextures.clear(); glExportMasks.clear(); glExportFrames.clear(); glExportComposite.clear(); glExportCoverage.clear(); glExportBackground.clear(); glExportLayerFrame.clear();
        glExportTiff.clear(); glExportPost.fbo.clear(); glExportPost.featherFbo.clear(); glExportLayerFx=forms::LayerFx();
    }
    void launchGLExportAudio(){
        glExportCapturing=false; releaseGLExportBuffers();
        glExportStatus="Rendering offline audio and embedding it in the video...";
        auto j=exportRecipe; auto base=exportBase;
        auto defs=(std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"synthdefs"/"CompiledSynthdefs"/"Forms").string();
        exportMedia=std::async(std::launch::async,[j, base, defs, layers=std::move(glAudioLayers)]{
            return forms::exportmedia::render(j,layers,readSound(j.at("sound")),base,defs,j.value("kickFile",std::string()),j.at("audio"),j.at("video"));
        });
    }
    void pollGLExport(){
        try {
            if(exportMedia.valid() && exportMedia.wait_for(std::chrono::seconds(0))==std::future_status::ready){
                glExportStatus=exportMedia.get()+" Seed "+exportRecipe.at("seed").get<std::string>()+". "+exportBase; glExportBusy=false;
            }
            if(glExportCapturing){
                if(glVideoEnding){
                    if(!glVideoWriter || glVideoWriter->done()){
                        if(glVideoWriter){ auto error=glVideoWriter->result(); glVideoWriter.reset(); if(!error.empty()) throw std::runtime_error(error); }
                        launchGLExportAudio();
                    }
                }else if(!glVideoWriter || glVideoWriter->available()){
                    auto size=exportRecipe.at("size").get<std::vector<int>>(); auto wh=forms::exportmedia::videoSize(size[0],size[1]);
                    auto frame=renderGLExportFrame(wh.first,wh.second,glVideoFrame/60.0,true);
                    if(glVideoWriter) glVideoWriter->push(std::vector<uint8_t>(frame.getData(),frame.getData()+frame.size()));
                    ++glVideoFrame;
                    glExportStatus="Rendering synchronized loop: "+std::to_string(glVideoFrame)+" / "+std::to_string(glVideoFrames)+" frames";
                    if(glVideoFrame==glVideoFrames){ glVideoEnding=true; if(glVideoWriter) glVideoWriter->finish(); }
                }
            }
            if(!exportRender.valid() || exportRender.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
            auto layers=exportRender.get(); glExportTextures.clear(); glExportMasks.clear();
            auto size=exportRecipe.at("size").get<std::vector<int>>(); int w=size[0], h=size[1];
            ofFboSettings st; st.width=w; st.height=h; st.internalformat=GL_RGBA32F;
            std::filesystem::create_directories(seedFolder());
            if(!std::filesystem::path(exportBase).parent_path().empty()) std::filesystem::create_directories(std::filesystem::path(exportBase).parent_path());
            for(auto& l : layers){
                ofTexture tex; tex.allocate(w,h,GL_RGBA16); tex.loadData(l.rgba.data(),w,h,GL_RGBA);   // 16 bits per channel
                ofFbo fbo; fbo.allocate(st); fbo.begin(); ofClear(0,0,0,0);
                ofPushStyle(); ofEnableAlphaBlending(); glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
                layerGain.draw(tex,0,w,h,l.gain); ofPopStyle(); fbo.end();
                glExportTextures.push_back(fbo.getTexture());
                glExportMasks.push_back(std::move(l.covers));
                l.rgba.clear(); l.rgba.shrink_to_fit();
            }
            ofPixels image=renderGLExportFrame(w,h,0);
            if(!ofSaveImage(glExportTiff,exportLibraryBase+".tif")) throw std::runtime_error("Cannot save 16-bit TIFF.");
            std::filesystem::copy_file(exportLibraryBase+".tif",exportBase+".tif",std::filesystem::copy_options::overwrite_existing);
            if(!ofSaveImage(glExportTiff,exportLibraryBase+".png")) throw std::runtime_error("Cannot save export PNG.");   // 16 bits per channel too
            glExportTiff.clear();
            std::filesystem::copy_file(exportLibraryBase+".png",exportBase+".png",std::filesystem::copy_options::overwrite_existing);
            { std::ofstream out(exportLibraryBase+".json"); out<<exportRecipe.dump(2); if(!out) throw std::runtime_error("Cannot save seed recipe."); }
            std::filesystem::copy_file(exportLibraryBase+".json",exportBase+".json",std::filesystem::copy_options::overwrite_existing);
            ofPixels preview=image; preview.resize(std::min(w,960),std::max(1,h*std::min(w,960)/w));
            bool arb=ofGetUsingArbTex(); ofDisableArbTex(); glExportPreview.clear(); glExportPreview.setFromPixels(preview); if(arb) ofEnableArbTex();
            snprintf(exportSeedText,sizeof(exportSeedText),"%s",exportRecipe.at("seed").get<std::string>().c_str());
            if(exportRecipe.at("audio").get<bool>() || exportRecipe.at("video").get<bool>()){
                auto wh=forms::exportmedia::videoSize(w,h);
                for(auto& texture : glExportTextures){
                    ofFbo small; small.allocate(wh.first,wh.second,GL_RGBA16); small.begin(); ofClear(0,0,0,0);
                    ofPushStyle(); ofEnableBlendMode(OF_BLENDMODE_DISABLED); ofSetColor(255); texture.draw(0,0,wh.first,wh.second); ofPopStyle(); small.end(); texture=small.getTexture();
                }
                glAudioLayers=std::move(layers);
                for(auto& l:glAudioLayers){ l.w=wh.first; l.h=wh.second; }
                if(exportRecipe.at("video").get<bool>()) glVideoWriter=std::make_unique<forms::exportmedia::VideoWriter>(exportBase,wh.first,wh.second,exportRecipe.at("duration").get<double>());
                glVideoFrame=0; glVideoFrames=(int)std::ceil(exportRecipe.at("duration").get<double>()*60);
                glVideoEnding=false; glExportCapturing=true;
            }else { releaseGLExportBuffers(); glExportBusy=false; glExportStatus="Saved PNG, 16-bit TIFF and recipe. Seed "+std::string(exportSeedText)+". "+exportBase; }
        }catch(const std::exception& e){ glVideoWriter.reset(); releaseGLExportBuffers(); glAudioLayers.clear(); glExportCapturing=false; glExportBusy=false; glExportStatus=e.what(); }
    }
    // glGetTexImage makes the macOS driver copy the whole texture back into its own store first
    // (a stop was seen inside that copy; cause not established). Read through a framebuffer in strips;
    // row order matches ofTexture::readToPixels. Pack state is set and restored around it.
    template<class Pix> static void readTextureStrips(const ofTexture& tex, Pix& px, GLenum type){
        auto& td=tex.getTextureData(); int w=(int)td.width, h=(int)td.height;
        if(!tex.isAllocated() || w<1 || h<1) throw std::runtime_error("Cannot read export texture");
        px.allocate(w,h,OF_IMAGE_COLOR_ALPHA);
        GLint prevFb=0, prevPbo=0, pAlign=4, pRow=0, pSkipR=0, pSkipP=0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&prevFb); glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&prevPbo);
        glGetIntegerv(GL_PACK_ALIGNMENT,&pAlign); glGetIntegerv(GL_PACK_ROW_LENGTH,&pRow);
        glGetIntegerv(GL_PACK_SKIP_ROWS,&pSkipR); glGetIntegerv(GL_PACK_SKIP_PIXELS,&pSkipP);
        glBindBuffer(GL_PIXEL_PACK_BUFFER,0); glPixelStorei(GL_PACK_ALIGNMENT,1); glPixelStorei(GL_PACK_ROW_LENGTH,0);
        glPixelStorei(GL_PACK_SKIP_ROWS,0); glPixelStorei(GL_PACK_SKIP_PIXELS,0);
        GLuint fb=0; glGenFramebuffers(1,&fb); glBindFramebuffer(GL_READ_FRAMEBUFFER,fb);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,td.textureTarget,td.textureID,0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        bool ok=glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
        while(glGetError()!=GL_NO_ERROR){}
        int rows=std::max(1,4000000/w);
        for(int y=0;ok && y<h;y+=rows){
            glReadPixels(0,y,w,std::min(rows,h-y),GL_RGBA,type,px.getData()+(size_t)y*w*4);
            ok=glGetError()==GL_NO_ERROR;
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER,prevFb); glDeleteFramebuffers(1,&fb);
        glPixelStorei(GL_PACK_ALIGNMENT,pAlign); glPixelStorei(GL_PACK_ROW_LENGTH,pRow);
        glPixelStorei(GL_PACK_SKIP_ROWS,pSkipR); glPixelStorei(GL_PACK_SKIP_PIXELS,pSkipP); glBindBuffer(GL_PIXEL_PACK_BUFFER,prevPbo);
        if(!ok) throw std::runtime_error("Cannot read export image from the GPU");
    }
    static int postMaxTextureSize(){ GLint n=0; glGetIntegerv(GL_MAX_TEXTURE_SIZE,&n); return n; }
    ofPixels renderGLExportFrame(int w, int h, double t, bool capture=false){
        auto allocate=[&](ofFbo& f){ if(!f.isAllocated() || (int)f.getWidth()!=w || (int)f.getHeight()!=h || f.getTexture().getTextureData().glInternalFormat != (capture ? GL_RGBA16 : GL_RGBA32F)){
            ofFboSettings st; st.width=w; st.height=h; st.internalformat=capture ? GL_RGBA16 : GL_RGBA32F; f.allocate(st);
            if(!f.isAllocated()) throw std::runtime_error("Cannot allocate export framebuffer");
        }};
        glExportPost.internalFormat=capture ? GL_RGBA16 : GL_RGBA32F;
        glExportLayerFx.internalFormat=capture ? GL_RGBA16 : GL_RGBA32F;
        allocate(glExportComposite); allocate(glExportCoverage); allocate(glExportBackground);
        allocate(glExportLayerFrame);
        glExportComposite.begin(); ofClear(0,0,0,255); glExportComposite.end();
        glExportCoverage.begin(); ofClear(0,0,0,0); glExportCoverage.end();
        glExportFrames.resize(glExportTextures.size());
        std::vector<formscover::Compositor::Source> coverSources;
        std::vector<int> coverOrder;
        std::vector<ofFbo*> coverBuffers;
        for(auto& meta:exportRecipe.at("layers")) coverBuffers.resize(std::max(coverBuffers.size(),(size_t)meta.at("index").get<int>()+1),nullptr);
        for(size_t i=0;i<glExportTextures.size();++i){
            auto& glExportLayerFrame=glExportFrames[i]; allocate(glExportLayerFrame);
            auto& meta=exportRecipe.at("layers").at(i); auto& tex=glExportTextures[i];
            int layer=meta.at("index"); coverOrder.push_back(layer); coverBuffers[layer]=&glExportLayerFrame;
            double phase=std::fmod(t*exportRecipe.at("bpm").get<double>()/(240.0*meta.at("recipe").value("bars",8.0)),1.0);
            glExportLayerFrame.begin(); ofClear(0,0,0,0); ofPushStyle(); ofEnableBlendMode(OF_BLENDMODE_DISABLED); ofSetColor(255);
            tex.draw(-phase*w,0,w,h); tex.draw((1-phase)*w,0,w,h); ofPopStyle(); glExportLayerFrame.end();
            auto f=meta.at("fx").get<std::vector<float>>(); forms::LayerFxSettings fx;
            fx.hBlur=f[0]*w; fx.vBlur=f[1]*h; fx.trailPx=f[2]*w/(4.f*meta.at("recipe").value("bars",8.0)); fx.trailLevel=f[3];
            auto echo=meta.value("echo",forms::json::object());   // absent in recipes saved before the echo: none
            fx.echoPx=echo.value("beats",0.f)*w/(4.f*meta.at("recipe").value("bars",8.0)); fx.echoFeedback=echo.value("feedback",.5f); fx.echoMix=echo.value("mix",.5f); fx.echoLow=echo.value("low",1.f); fx.echoHigh=echo.value("high",0.f);
            auto perlin=meta.value("perlin",forms::json::object());
            fx.maskAmount=perlin.value("amount",0.f);fx.maskWidth=perlin.value("width",4.f);fx.maskHeight=perlin.value("height",.25f);
            fx.maskContrast=perlin.value("contrast",1.f);fx.maskThreshold=perlin.value("threshold",0.f);fx.maskBlur=perlin.value("blur",0.f)*h;fx.maskInvert=perlin.value("invert",false);fx.maskSeed=perlin.value("seed",1);
            fx.maskSpeed=perlin.value("speed",0.f);fx.maskBeat=t*exportRecipe.at("bpm").get<double>()/60.;fx.maskPhase=fx.maskBeat*fx.maskSpeed;
            fx.maskHeaderX=0;fx.maskPixelsPerBeat=w/(4.f*meta.at("recipe").value("bars",8.0));
            if(fx.any()) glExportLayerFx.process(glExportLayerFrame,fx);
            auto settings=formscover::read(meta.value("cover",forms::json::object()));
            float ppb=w/(4.f*meta.at("recipe").value("bars",8.0));
            if(settings.enabled) coverSources.push_back({settings,layer,ppb,true,[this,i](int mw,int mh){ofSetColor(255);glExportFrames[i].draw(0,0,mw,mh);}});
            for(size_t k=0;k<glExportMasks[i].size();++k){
                auto settings=glExportMasks[i][k].settings; float gain=meta.value("gain",1.f);
                coverSources.push_back({settings,layer,ppb,false,[this,i,k,phase,gain](int mw,int mh){
                    formscover::Compositor::drawMask(glExportMasks[i][k],-phase*mw,mw,mw,mh,gain);
                    formscover::Compositor::drawMask(glExportMasks[i][k],(1-phase)*mw,mw,mw,mh,gain);
                }});
            }
        }
        // Retain muted/missing stack positions: "immediately below" must not jump a muted layer.
        if(exportRecipe.contains("stackOrder")) coverOrder=exportRecipe.at("stackOrder").get<std::vector<int>>();
        glExportCovers.apply(coverBuffers,coverOrder,coverSources,w,h);
        for(size_t i=0;i<glExportTextures.size();++i){
            auto& meta=exportRecipe.at("layers").at(i); auto& glExportLayerFrame=glExportFrames[i];
            if(capture){
                auto& l=glAudioLayers[i]; std::vector<unsigned short> column;
                if(!readColumn(glExportLayerFrame,w/4,0,h,column)) throw std::runtime_error("Cannot read export sonification column");
                auto layout=forms::partialLayout(h,l.recipe.value("notegrid",100),l.recipe.at("size").at(1).get<int>());
                std::vector<float> amps; forms::columnAmps(column.data(),h,exportRecipe.value("imageMul",1.f),layout,amps);
                l.scanAmps.insert(l.scanAmps.end(),amps.begin(),amps.end());
                auto settings=readSound(exportRecipe.at("sound"));
                float stripH=settings.kickStrip;   // the scene's own strip (its recipe's performance), else the node's
                if(l.recipe.contains("performance")&&l.recipe["performance"].is_object()) stripH=l.recipe["performance"].value("kickStrip",stripH);
                int strip=std::clamp((int)std::lround(h*stripH),1,h); double sum=0;
                for(int y=h-strip;y<h;++y) for(int c=0;c<3;++c) sum+=column[y*4+c]/(3.0*65535*strip);
                l.kickLevels.push_back((float)sum);
            }else if(exportRecipe.at("separate").get<bool>()){
                ofShortPixels straight; readTextureStrips(glExportLayerFrame.getTexture(),straight,GL_UNSIGNED_SHORT); auto* p=straight.getData();
                for(size_t k=0;k<(size_t)w*h;++k) for(int c=0;c<3;++c){
                    uint64_t a=p[k*4+3];
                    p[k*4+c]=a ? (uint16_t)std::min<uint64_t>(65535,((uint64_t)p[k*4+c]*65535+a/2)/a) : 0;
                }
                std::string layerBase=exportBase+"_L"+std::to_string(meta.at("index").get<int>()+1);
                if(!ofSaveImage(straight,layerBase+".tif")) throw std::runtime_error("Cannot save 16-bit layer TIFF.");
                if(!ofSaveImage(straight,layerBase+".png")) throw std::runtime_error("Cannot save layer PNG.");
            }
            std::string blend=meta.at("blend");
            glExportComposite.begin(); ofPushStyle(); ofEnableBlendMode(OF_BLENDMODE_DISABLED); glEnable(GL_BLEND);
            glBlendEquation(blend=="lighten" ? GL_MAX : GL_FUNC_ADD); glBlendFunc(GL_ONE,blend=="normal" ? GL_ONE_MINUS_SRC_ALPHA : GL_ONE);
            ofSetColor(255); glExportLayerFrame.draw(0,0); glBlendEquation(GL_FUNC_ADD); ofPopStyle(); glExportComposite.end();
            glExportCoverage.begin(); ofPushStyle(); ofEnableBlendMode(OF_BLENDMODE_DISABLED); glEnable(GL_BLEND);
            glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA); glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_TRUE);
            ofSetColor(255); glExportLayerFrame.draw(0,0); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); ofPopStyle(); glExportCoverage.end();
        }
        auto& p=exportRecipe.at("post"); auto& look=glExportPost;
        look.enabled=p.at("enabled"); look.hue=p.at("hue"); look.sat=p.at("sat"); look.val=p.at("val");
        look.levelMin=p.at("levelMin"); look.levelMax=p.at("levelMax"); look.gamma=p.at("gamma"); look.low=p.at("low"); look.high=p.at("high");
        // Still exports show the complete image; playback overlays belong to video only.
        look.stretch=capture && p.at("stretch").get<bool>();
        look.stretchBlend=p.value("stretchBlend",0);
        look.line=capture && p.at("line").get<bool>();
        look.useOpacity=p.at("useOpacity");
        look.lineWidth=p.at("lineWidth").get<float>()*w; look.lineSoft=p.at("lineSoft").get<float>()*w; look.lineSoftV=p.at("lineSoftV").get<float>()*h;
        look.lineAlpha=p.at("lineAlpha"); look.lineColor=ofFloatColor(p.at("lineColor").at(0).get<float>(),p.at("lineColor").at(1).get<float>(),p.at("lineColor").at(2).get<float>());
        look.lineEase=0; look.lineOffset=0;
        for(int e=0;e<4;++e){ look.edges.width[e]=p.at("edgeWidth").at(e); look.edges.curve[e]=p.at("edgeCurve").at(e); }
        float header=w*.25f, opacity=exportRecipe.value("opacity",1.f);
        auto* final=look.process(glExportComposite.getTexture(),header,opacity,0);
        if(look.enabled && !look.ok) throw std::runtime_error("Export post-processing shader failed");
        auto bg=p.at("background").get<std::vector<float>>(); bool paper=p.at("paper");
        bool black=forms::backgroundIsBlack(bg[0],bg[1],bg[2]);
        if(paper && black) bg={1,1,1};
        if(paper || !black){
            if(!glExportBgShader.isLoaded()){
                std::string vert="#version 410\nuniform mat4 modelViewProjectionMatrix;\nlayout(location=0) in vec4 position;\nvoid main(){gl_Position=modelViewProjectionMatrix*position;}\n";
                if(!glExportBgShader.setupShaderFromSource(GL_VERTEX_SHADER,vert) || !glExportBgShader.setupShaderFromSource(GL_FRAGMENT_SHADER,forms::backgroundFragmentShader())) throw std::runtime_error("Export background shader failed");
                glExportBgShader.bindDefaults(); if(!glExportBgShader.linkProgram()) throw std::runtime_error("Export background shader failed");
            }
            glExportBackground.begin(); ofClear(0,0,0,255); ofPushStyle(); ofEnableBlendMode(OF_BLENDMODE_DISABLED);
            glExportBgShader.begin(); glExportBgShader.setUniformTexture("tImage",*final,0); glExportBgShader.setUniformTexture("tCover",glExportCoverage.getTexture(),1);
            glExportBgShader.setUniform2f("size",w,h); glExportBgShader.setUniform1f("stretchX",look.enabled && look.stretch ? header : -1.f);
            glExportBgShader.setUniform1i("stretchBlend",look.stretchBlend);
            glExportBgShader.setUniform1f("opacity",look.enabled ? (look.useOpacity ? std::max(.001f,opacity) : 1.f) : opacity);
            glExportBgShader.setUniform3f("bg",bg[0],bg[1],bg[2]); glExportBgShader.setUniform1i("paper",paper ? 1 : 0);
            ofSetColor(255); ofDrawRectangle(0,0,w,h); glExportBgShader.end(); ofPopStyle(); glExportBackground.end(); final=&glExportBackground.getTexture();
        }
        if(look.edges.active()) final=look.feather(*final,ofFloatColor(bg[0],bg[1],bg[2]));
        if(!capture) readTextureStrips(*final,glExportTiff,GL_UNSIGNED_SHORT);
        // 8 bits only here, at the very end: the preview and the frames handed to the video encoder
        ofPixels image; readTextureStrips(*final,image,GL_UNSIGNED_BYTE); return image;
    }
