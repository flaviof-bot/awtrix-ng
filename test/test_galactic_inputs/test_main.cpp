#include <unity.h>
#include <string>
#include <vector>
#include "../EngineFakes.h"
#include "system/GalacticUnicornControls.h"
#include "core/launcher/Launcher.h"
#include "core/script/ScriptService.h"
#include "core/sensing/BrightnessPolicy.h"
using namespace awtrix;
using namespace awtrix::galactic;
void setUp() {} void tearDown() {}
struct Scripts : IScriptService {
  DispatchResult setScript(const std::string&, const std::string&, DispatchDetail&) override { return DispatchResult::Ok; }
  void removeScript(const std::string&) override {}
  bool scriptIsOnDemand(const std::string&) override { return true; }
};
struct Fixture {
  sound::AudioRouter audio; test::NullDisplay display; test::NullSystem system;
  CoreEngine engine{audio, display, system, {"Time","Date"}};
  DeviceConfig cfg; launcher::Launcher menu{engine}; GalacticUnicornControls controls;
  std::array<bool,9> keys{}; int64_t now=0; int settingsEvents=0,powerEvents=0,buttonEvents=0;
  Fixture() {
    controls.begin(engine,cfg,&menu);
    engine.state().subscribe([this](StateEvent e) {
      if(e==StateEvent::SettingsChanged) ++settingsEvents;
      if(e==StateEvent::PowerChanged) ++powerEvents;
      if(e==StateEvent::ButtonsChanged) ++buttonEvents;
    });
  }
  void sample() { controls.tick(engine,keys,now); engine.tick(now); }
  void down(size_t i) { keys[i]=true; sample(); now+=35; sample(); }
  void up(size_t i) { keys[i]=false; ++now; sample(); now+=35; sample(); }
  void press(size_t i) { down(i); up(i); }
};
void debounce_press_hold_bounce() {
  DebouncedButton b;
  TEST_ASSERT_FALSE(b.update(true,10)); TEST_ASSERT_FALSE(b.update(false,20));
  TEST_ASSERT_FALSE(b.update(true,30)); TEST_ASSERT_FALSE(b.update(true,64));
  TEST_ASSERT_TRUE(b.update(true,65)); TEST_ASSERT_TRUE(b.pressed());
  TEST_ASSERT_FALSE(b.update(true,10000));
  TEST_ASSERT_FALSE(b.update(false,10001)); TEST_ASSERT_FALSE(b.update(true,10010));
  TEST_ASSERT_FALSE(b.update(true,10050)); TEST_ASSERT_TRUE(b.pressed());
  b.update(false,10060); b.update(false,10095); TEST_ASSERT_FALSE(b.pressed());
  b.update(true,10100); TEST_ASSERT_TRUE(b.update(true,10135));
}
void map_and_clamp() {
  const int pins[]={0,1,3,6,27,7,8,21,26};
  const InputAction actions[]={InputAction::Left,InputAction::Select,InputAction::Right,InputAction::None,
    InputAction::Power,InputAction::VolumeUp,InputAction::VolumeDown,InputAction::BrightnessUp,InputAction::BrightnessDown};
  for(size_t i=0;i<Inputs.size();++i) { TEST_ASSERT_EQUAL(pins[i],Inputs[i].pin); TEST_ASSERT_TRUE(actions[i]==Inputs[i].action); }
  TEST_ASSERT_EQUAL(0,stepClamped(2,-10,255)); TEST_ASSERT_EQUAL(255,stepClamped(250,10,255));
  TEST_ASSERT_EQUAL(0,stepClamped(0,-5,100)); TEST_ASSERT_EQUAL(100,stepClamped(100,5,100));
}
void extras_use_dispatcher_events() {
  Fixture f; auto& s=f.engine.state().settings();
  s.brightness=250; s.autoBrightness=true; s.volume=98;
  f.press(7); TEST_ASSERT_EQUAL(255,s.brightness); TEST_ASSERT_FALSE(s.autoBrightness);
  f.press(8); TEST_ASSERT_EQUAL(245,s.brightness);
  f.press(5); TEST_ASSERT_EQUAL(100,s.volume); f.press(6); TEST_ASSERT_EQUAL(95,s.volume);
  s.brightness=2; s.volume=2;
  f.press(8); f.press(6); TEST_ASSERT_EQUAL(0,s.brightness); TEST_ASSERT_EQUAL(0,s.volume);
  TEST_ASSERT_EQUAL(6,f.settingsEvents);
  f.press(4); TEST_ASSERT_TRUE(f.engine.state().runtime().matrixOff);
  f.press(4); TEST_ASSERT_FALSE(f.engine.state().runtime().matrixOff); TEST_ASSERT_EQUAL(2,f.powerEvents);
  f.press(3); TEST_ASSERT_EQUAL(6,f.settingsEvents); TEST_ASSERT_EQUAL(2,f.powerEvents);
  f.down(7); const int events=f.settingsEvents; f.now+=5000; f.sample();
  TEST_ASSERT_EQUAL(events,f.settingsEvents); TEST_ASSERT_EQUAL(10,s.brightness);
}
void navigation_events_and_light() {
  Fixture f; auto& rt=f.engine.state().runtime();
  f.engine.state().settings().blockNavigation=true;
  f.keys[0]=true; f.sample(); f.now=34; f.sample(); TEST_ASSERT_EQUAL(0,f.buttonEvents);
  f.now=35; f.sample(); TEST_ASSERT_TRUE(rt.buttons[0]);
  f.now=200; f.sample(); TEST_ASSERT_EQUAL(1,f.buttonEvents);
  f.up(0); TEST_ASSERT_FALSE(rt.buttons[0]);
  f.down(1); TEST_ASSERT_TRUE(rt.buttons[1]); f.down(2); TEST_ASSERT_TRUE(rt.buttons[2]);
  f.engine.setLightSensorAvailable(true);
  f.engine.state().settings().autoBrightness=true; f.cfg.brightnessSmoothing=0;
  BrightnessPolicy policy;
  for(int raw : {4095,0}) {
    for(int i=0;i<6;++i) { f.now+=100; f.controls.sampleLight(f.engine,raw,f.now); }
    TEST_ASSERT_EQUAL(raw,rt.ldrRaw);
    TEST_ASSERT_EQUAL(raw?f.cfg.maxBrightness:f.cfg.minBrightness,
      policy.resolve(f.engine.state().settings(),rt,f.cfg.lightConfig(),f.now));
  }
  f.engine.state().settings().autoBrightness=false; f.engine.state().settings().brightness=123;
  TEST_ASSERT_EQUAL(123,policy.resolve(f.engine.state().settings(),rt,f.cfg.lightConfig(),f.now+100));
}
void navigation_matches_commands_and_select_double_press() {
  for(bool rotate : {false,true}) for(bool swapped : {false,true}) for(int key : {0,2}) {
    Fixture f,api; f.cfg.rotate=rotate; f.cfg.swapButtons=swapped;
    f.sample(); api.sample(); f.down(key);
    const bool next=(key==2)!=(rotate!=swapped);
    api.engine.submit(Command(next?CommandType::NextApp:CommandType::PreviousApp));
    api.now=35; api.sample(); f.now=api.now=1000; f.sample(); api.sample();
    TEST_ASSERT_EQUAL_STRING(api.engine.currentAppId().c_str(),f.engine.currentAppId().c_str());
  }
  Fixture f; Command notification(CommandType::Notify); notification.payload=R"({"text":"test","hold":true})";
  TEST_ASSERT_TRUE(f.engine.execute(notification)==DispatchResult::Ok); TEST_ASSERT_TRUE(f.engine.hasNotification());
  f.down(1); TEST_ASSERT_FALSE(f.engine.hasNotification()); TEST_ASSERT_FALSE(f.engine.state().runtime().matrixOff);
  f.up(1); f.down(1); TEST_ASSERT_TRUE(f.engine.state().runtime().matrixOff);
  f.now=1000; f.sample(); TEST_ASSERT_EQUAL(1,f.powerEvents);
}
void b_hold_opens_launcher() {
  Fixture f; f.down(1); f.now+=499; f.sample(); TEST_ASSERT_FALSE(f.menu.isOpen());
  ++f.now; f.sample(); TEST_ASSERT_TRUE(f.menu.isOpen());
  f.up(1); TEST_ASSERT_TRUE(f.menu.isOpen());
}
void ac_step_in_launcher() {
  Fixture f; Scripts scripts; f.engine.setScriptService(&scripts);
  f.engine.syncScriptApp("One"); f.engine.syncScriptApp("Two"); f.sample();
  f.down(1); f.now+=500; f.sample(); f.up(1);
  TEST_ASSERT_TRUE(f.menu.isOpen());
  f.press(1); // Enter Scripts via a released select.
  TEST_ASSERT_TRUE(f.menu.page()==launcher::Page::Scripts);
  TEST_ASSERT_EQUAL(2,f.menu.entries().size());
  TEST_ASSERT_EQUAL(0,f.menu.selected()); f.press(2); TEST_ASSERT_EQUAL(1,f.menu.selected());
  f.press(0); TEST_ASSERT_EQUAL(0,f.menu.selected());
}
std::vector<std::string> g_posted;
void recordPost(const std::string& url, int button, bool pressed) {
  g_posted.push_back(url + " " + std::to_string(button) + (pressed ? " down" : " up"));
}
void abc_call_the_button_webhook() {
  g_posted.clear();
  Fixture f; f.controls.setButtonPost(recordPost);
  f.down(0); f.up(0); TEST_ASSERT_EQUAL(0, g_posted.size()); // off while buttonCallback is empty
  f.cfg.buttonCallback = "http://hook";
  f.down(0); f.up(0); f.down(2); f.up(2); f.down(4); f.up(4);
  TEST_ASSERT_EQUAL(4, g_posted.size()); // brightness and the other extras never call it
  TEST_ASSERT_EQUAL_STRING("http://hook 0 down", g_posted[0].c_str());
  TEST_ASSERT_EQUAL_STRING("http://hook 0 up", g_posted[1].c_str());
  TEST_ASSERT_EQUAL_STRING("http://hook 2 down", g_posted[2].c_str());
  TEST_ASSERT_EQUAL_STRING("http://hook 2 up", g_posted[3].c_str());
}
int main() {
  UNITY_BEGIN(); RUN_TEST(debounce_press_hold_bounce); RUN_TEST(map_and_clamp);
  RUN_TEST(extras_use_dispatcher_events); RUN_TEST(navigation_events_and_light);
  RUN_TEST(navigation_matches_commands_and_select_double_press);
  RUN_TEST(abc_call_the_button_webhook);
  RUN_TEST(b_hold_opens_launcher); RUN_TEST(ac_step_in_launcher); return UNITY_END();
}
