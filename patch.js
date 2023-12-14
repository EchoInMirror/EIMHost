const fs = require('fs').promises

const patchs = {
  'JUCE/modules/juce_gui_basics/native/juce_Windowing_Windows.cpp': { // Allow make window be child of another window
    from: 'hwnd = CreateWindowEx',
    to: 'hwnd = parentToAddTo != nullptr && (styleFlags & (1 << 28)) ? CreateWindow(WindowClassHolder::getInstance()->getWindowClassName(), L"",\
WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION | WS_MINIMIZEBOX | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | ((styleFlags & windowIsResizable) ? WS_MAXIMIZEBOX | WS_THICKFRAME : 0),\
0, 0, 0, 0, parentToAddTo, nullptr, (HINSTANCE)Process::getCurrentModuleInstanceHandle(), nullptr) : CreateWindowEx'
  },
  'JUCE/modules/juce_gui_basics/native/juce_NSViewComponentPeer_mac.mm': [ // Custom title bar
    {
      from: '[window setRestorable: NO];',
        to: 'if ((windowStyleFlags & (1 << 28)) != 0){[window setTitlebarAppearsTransparent:YES];[window setTitleVisibility:NSWindowTitleHidden];}[window setRestorable: NO];'
    },
    {
      from: 'if ((flags & windowHasMinimiseButton) != 0)',
      to: 'if ((flags & (1 << 28)) != 0) style |= NSWindowStyleMaskFullSizeContentView;if ((flags & windowHasMinimiseButton) != 0)',
    }
  ],
  'JUCE/modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.cpp': { // Skip useless codes
    from: 'inputLevelGetter->updateLevel',
    to: 'if (callbacks.size() > 0) { callbacks.getUnchecked(0)->audioDeviceIOCallbackWithContext(inputChannelData, numInputChannels,\
      outputChannelData, numOutputChannels, numSamples, context); return; }\n    inputLevelGetter->updateLevel'
  },
}

Object.entries(patchs).forEach(async ([file, obj]) => {
  let data = await fs.readFile(file, 'utf8')
  const arr = Array.isArray(obj) ? obj : [obj]
  let changed = false
  for (const { from, to } of arr) {
    if (data.includes(to)) continue
    const newData = data.replace(from, to)
    if (data === newData) throw new Error('Patch failed')
    data = newData
    changed = true
  }
  if (changed) await fs.writeFile(file, data)
  console.log('Patched:', file)
})
