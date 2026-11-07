// Scene banks, headless, on a COPY of the renderer folder: the legacy scenes stay in scenes/,
// new ones go to the current bank's folder, a scene moves between banks (its file follows),
// rename / revert / delete work inside a bank, and a fresh engine reads it all back.
#include "formsEngine.h"
#include <cstdio>
using namespace forms;
namespace fs = std::filesystem;
static int fails = 0;
#define CHECK(c, ...) do{ if(!(c)){ fails++; printf("FAIL "); printf(__VA_ARGS__); printf("\n"); } }while(0)

int main(int, char** argv){
    std::string folder = argv[1];
    fs::path sd = fs::path(folder) / "scenes";
    int legacyFiles = 0; for(auto& e : fs::directory_iterator(sd)) if(e.path().extension() == ".json") legacyFiles++;
    Engine e; e.rendererFolder = folder; e.loadAll();
    int legacy = 0; for(auto& [n, s] : e.scenes) if(s.bank.empty()) legacy++;
    CHECK(legacy == legacyFiles, "legacy scenes in the Legacy bank (%d of %d)", legacy, legacyFiles);
    // a bank, new scenes in it
    CHECK(e.createBank("Tour 2027") == "Tour 2027", "bank created");
    e.currentBank = "Tour 2027";
    std::string a = e.createScene("BANK TEST");
    auto file = [&](const std::string& bank, const std::string& name){ Scene x; x.name = name; x.bank = bank; return e.scenePath(x); };
    CHECK(fs::exists(file("Tour 2027", a)) && file("Tour 2027", a).parent_path() == sd / "Tour 2027", "new scene saved in the bank's folder");
    CHECK(!fs::exists(file("", a)), "not in the legacy folder");
    std::string d = e.duplicateScene(e.scenes.begin()->first);   // a legacy scene's copy goes to the bank
    CHECK(e.scenes[d].bank == "Tour 2027" && fs::exists(e.scenePath(e.scenes[d])), "a duplicate goes to the current bank");
    // move a scene: its file follows
    e.createBank("Other");
    CHECK(e.moveSceneToBank(a, "Other"), "moved");
    CHECK(fs::exists(file("Other", a)) && !fs::exists(file("Tour 2027", a)), "the file moved");
    // rename inside a bank
    CHECK(e.renameScene(a, "BANK TEST 2"), "renamed");
    CHECK(fs::exists(file("Other", "BANK TEST 2")) && !fs::exists(file("Other", a)), "renamed in its bank");
    // edit + revert from the bank's file
    e.scenes["BANK TEST 2"].bpm = 33; e.reloadScene("BANK TEST 2");
    CHECK(e.scenes["BANK TEST 2"].bpm == 95 && e.scenes["BANK TEST 2"].bank == "Other", "revert reads the bank's file");
    // a fresh engine
    Engine f; f.rendererFolder = folder; f.loadAll();
    CHECK(f.scenes.count("BANK TEST 2") && f.scenes["BANK TEST 2"].bank == "Other", "read back in its bank");
    CHECK(f.scenes.count(d) && f.scenes[d].bank == "Tour 2027", "the duplicate read back in its bank");
    auto banks = f.banks();
    CHECK(std::find(banks.begin(), banks.end(), "") != banks.end() && std::find(banks.begin(), banks.end(), "Tour 2027") != banks.end() && std::find(banks.begin(), banks.end(), "Other") != banks.end(), "banks listed");
    // delete inside a bank
    f.deleteScene("BANK TEST 2");
    CHECK(!fs::exists(file("Other", "BANK TEST 2")), "deleted from its bank");
    int legacyAfter = 0; for(auto& en : fs::directory_iterator(sd)) if(en.path().extension() == ".json") legacyAfter++;
    CHECK(legacyAfter == legacyFiles, "the legacy folder untouched (%d files)", legacyAfter);
    printf("%d legacy scenes, banks:", legacy); for(auto& b : banks) printf(" [%s]", Engine::bankLabel(b).c_str()); printf("\n%d failures\n", fails);
    return fails ? 1 : 0;
}
