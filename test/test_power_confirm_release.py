#!/usr/bin/env python3
"""Run real scripted HalGPIO releases through the firmware's Confirm classifier."""
from pathlib import Path
import shlex,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[1]
SIM=Path(sys.argv[1])
sys.path.insert(0,str(ROOT/'scripts/tests'))
from test_reading_ui_regressions import method
classifier=method((ROOT/'src/MappedInputManager.cpp').read_text(),'bool MappedInputManager::wasPowerConfirmClick()')
code=r'''
#include <HalGPIO.h>
#include <GfxRenderer.h>
#include <SimulatorLifecycle.h>
#include <SDL.h>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <vector>
GfxRenderer renderer;std::atomic<bool> quitRequested{false};
namespace SimulatorLifecycle {
WakeReason consumeWakeReason(){return WakeReason::None;}
[[noreturn]] void rebootAsPowerWake(){std::abort();}
}
struct CrossPointSettings {
 enum SHORT_PWRBTN {PWR_CONFIRM};SHORT_PWRBTN shortPwrBtn=PWR_CONFIRM;
 bool doubleClickPwrLight=false;
 unsigned long getPowerButtonDuration()const{return 400;}
} settings;
#define SETTINGS settings
class MappedInputManager {
 HalGPIO& gpio;bool powerConfirmClickFrame=false;
 public:explicit MappedInputManager(HalGPIO& g):gpio(g){}
 bool wasPowerConfirmClick()const;
};
@CLASSIFIER@
int main(){
 assert(SDL_Init(SDL_INIT_EVENTS|SDL_INIT_TIMER)==0);
 setenv("CROSSPOINT_SIM_INPUT_SCRIPT","10:POWER:80;150:POWER:1200;1450:POWER:80",1);
 gpio.begin();MappedInputManager input(gpio);
 std::vector<unsigned long> durations;std::vector<bool> confirms;
 while(millis()<1600){
  gpio.beginFrame();gpio.update();
  if(gpio.wasReleased(HalGPIO::BTN_POWER)){
   durations.push_back(gpio.getPowerButtonHeldTime());confirms.push_back(input.wasPowerConfirmClick());
  }
  SDL_Delay(1);
 }
 assert((confirms==std::vector<bool>{true,false,true}));
 // SDL and the script clock can round edges differently; assert the classification margins under scheduler load.
 assert(durations.size()==3&&durations[0]<200&&durations[1]>=1000&&durations[1]<1400&&durations[2]<200);
 SDL_Quit();
}
'''.replace('@CLASSIFIER@',classifier)
with tempfile.TemporaryDirectory(prefix='power-confirm-') as directory:
    cpp=Path(directory)/'test.cpp';binary=Path(directory)/'test';cpp.write_text(code)
    subprocess.run(['c++','-std=c++20','-DSIMULATOR_DEVICE_EEGO_A4','-I'+str(SIM/'tests/native-stubs'),'-I'+str(SIM/'src'),str(cpp),str(SIM/'src/HalGPIO.cpp'),*shlex.split(subprocess.check_output(['sdl2-config','--cflags','--libs'],text=True)),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('Real scripted Power short/long/release/next contact through MappedInputManager passed')
