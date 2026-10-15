#pragma once
// Tools › Flashlight: the whole panel lit at full brightness, kept on (no
// auto-off) until a key. Not on e-ink, which gives no light. Leaving it, by
// a key or anything that changes the screen, puts the brightness back
// (UITask::setCurrScreen).
// Included by UITask.cpp.

class FlashlightScreen : public UIScreen {
  UITask* _task;

public:
  FlashlightScreen(UITask* task) : _task(task) {}

  int render(DisplayDriver& display) override {
    display.setBrightness(4);
    display.setColor(UIColor::primary_txt);
    display.fillRect(0, 0, display.width(), display.height());
    return 1000;
  }

  bool handleInput(char c) override {
    (void)c;
    _task->leaveTool();
    return true;
  }
};
