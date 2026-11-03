"""Exercise the keyboard's logical Up handler with the real Waveshare gesture decoder."""
import os
from pathlib import Path
import unittest

from test_reading_ui_regressions import ROOT, run_cpp


class KeyboardUpHoldTest(unittest.TestCase):
    def test_logical_hold_starts_after_physical_gesture(self):
        source = (ROOT / 'src/activities/util/KeyboardEntryActivity.cpp').read_text()
        start = source.index('  if (!cursorMode && mappedInput.wasPressed(MappedInputManager::Button::Up))')
        end = source.index('  if (mappedInput.wasPressed(MappedInputManager::Button::Down))', start)
        sdk = Path(os.environ.get('FREEINK_SDK_ROOT', ROOT / 'freeink-sdk'))
        run_cpp(r'''
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <FunctionButtonGesture.h>
using Gesture=freeink::input::FunctionButtonGesture;
unsigned long now=0;
unsigned long millis(){return now;}
struct MappedInputManager {
 enum class Button {Up};
 Gesture::State state;
 bool wasPressed(Button) const {return state.pressed & Gesture::UP;}
 bool wasReleased(Button) const {return state.released & Gesture::UP;}
 bool isPressed(Button) const {return state.down & Gesture::UP;}
 unsigned long getHeldTime() const {return now-state.startedMs;}
};
struct KeyboardEntryActivity {
 MappedInputManager mappedInput;
 bool cursorMode=false,upHeld=false,upLongHandled=false,hintVisible=false;
 unsigned long upPressStart=0,hintShowTime=0;
 int rows=0,updates=0;
 static constexpr unsigned long LONG_PRESS_MS=500;
 void moveSelectionRow(int delta){rows+=delta;}
 void requestUpdate(){++updates;}
 void step(){
''' + source[start:end] + r'''
 }
};
void tick(KeyboardEntryActivity& keyboard,Gesture& gesture,uint8_t raw,unsigned long time){
 now=time;keyboard.mappedInput.state=gesture.update(raw,uint32_t(now));keyboard.step();
}
void pressUp(KeyboardEntryActivity& keyboard,unsigned long time){
 now=time;keyboard.mappedInput.state={Gesture::UP,Gesture::UP,0,0,0};keyboard.step();
}
void holdUp(KeyboardEntryActivity& keyboard,unsigned long time){
 now=time;keyboard.mappedInput.state={Gesture::UP,0,0,0,0};keyboard.step();
}
void releaseUp(KeyboardEntryActivity& keyboard,unsigned long time){
 now=time;keyboard.mappedInput.state={0,0,Gesture::UP,0,0};keyboard.step();
}
int main(){
 KeyboardEntryActivity keyboard;Gesture gesture;
 tick(keyboard,gesture,Gesture::LEFT,100);
 tick(keyboard,gesture,Gesture::LEFT,105);
 tick(keyboard,gesture,Gesture::LEFT,754);
 assert(!keyboard.upHeld && keyboard.rows==0);
 tick(keyboard,gesture,Gesture::LEFT,755);
 assert(keyboard.mappedInput.getHeldTime()==650);
 assert(keyboard.upHeld && !keyboard.cursorMode && keyboard.upPressStart==755);
 tick(keyboard,gesture,0,900);tick(keyboard,gesture,0,905);
 assert(keyboard.rows==-1 && !keyboard.cursorMode && !keyboard.upHeld);
 // A second physical hold can still enter cursor mode after 500 logical ms.
 tick(keyboard,gesture,Gesture::LEFT,1000);tick(keyboard,gesture,Gesture::LEFT,1005);
 tick(keyboard,gesture,Gesture::LEFT,1655);tick(keyboard,gesture,Gesture::LEFT,2155);
 assert(!keyboard.cursorMode);
 tick(keyboard,gesture,Gesture::LEFT,2156);
 assert(keyboard.cursorMode && keyboard.upLongHandled);
 int updates=keyboard.updates;
 tick(keyboard,gesture,Gesture::LEFT,2256);
 assert(keyboard.updates==updates);
 tick(keyboard,gesture,0,2300);tick(keyboard,gesture,0,2305);
 assert(keyboard.rows==-1 && !keyboard.upHeld && !keyboard.upLongHandled);
 // Ordinary buttons, repeated presses and the timer wrap keep the same contract.
 for(unsigned long start:{3000ul,std::numeric_limits<unsigned long>::max()-250ul}){
  KeyboardEntryActivity normal;
  pressUp(normal,start);holdUp(normal,start+500);
  assert(!normal.cursorMode);releaseUp(normal,start+500);
  assert(normal.rows==-1);
  pressUp(normal,start+1000);holdUp(normal,start+1501);
  assert(normal.cursorMode);releaseUp(normal,start+1600);
  assert(normal.rows==-1);
 }
 // A click synthesized in one frame still moves exactly one row.
 KeyboardEntryActivity click;now=42;
 click.mappedInput.state={0,Gesture::UP,Gesture::UP,0,0};click.step();
 assert(click.rows==-1 && !click.cursorMode && !click.upHeld);
}
''', include_dirs=(sdk / 'libs/hardware/InputManager/include',))


if __name__ == '__main__':
    unittest.main()
