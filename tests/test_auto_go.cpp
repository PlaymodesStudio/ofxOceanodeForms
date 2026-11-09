// clang++ -std=c++17 -I src/formsBox -I ../../libs/json/include tests/test_auto_go.cpp -o /tmp/test_auto_go
#include "formsClock.h"
#include <cassert>
#include <cstdio>

int main(){
    forms::Transport t;
    t.bars = 1;
    t.maxMinutes = 0.05f;
    t.autoGo = false;
    for(int i = 1; i <= 100; ++i) t.step(i * 0.1, 0.1f);
    assert(t.goCount == 0); // includes wraps, session end and its delayed final request
    t.newPng(10);
    t.step(10.3, 0.1f);
    assert(t.goCount == 1); // explicit New PNG remains available while off

    forms::Transport wraps;
    wraps.bars = 1;
    wraps.phase = 0.99f;
    wraps.step(0.1, 0.1f); // starts an automatic request's 200 ms delay
    wraps.autoGo = false;
    wraps.step(0.4, 0.1f);
    assert(wraps.goCount == 0); // switching off cancels that pending automatic request
    wraps.autoGo = true;
    for(int i = 5; i <= 50; ++i) wraps.step(i * 0.1, 0.1f);
    assert(wraps.goCount > 0); // switching back on resumes generation
    std::puts("Auto GO: all checks passed");
}
