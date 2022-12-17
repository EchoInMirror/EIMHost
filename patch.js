const fs = require('fs').promises

const patchs = {
  'JUCE/modules/juce_gui_basics/native/juce_win32_Windowing.cpp': {
    from: 'hwnd = CreateWindowEx',
    to: 'hwnd = parentToAddTo != nullptr && (styleFlags & (1 << 28)) ? CreateWindow(WindowClassHolder::getInstance()->getWindowClassName(), L"",\
WS_POPUP | WS_SYSMENU | WS_CAPTION | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,\
0, 0, 0, 0, parentToAddTo, nullptr, (HINSTANCE)Process::getCurrentModuleInstanceHandle(), nullptr) : CreateWindowEx'
  },
  'JUCE/modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.cpp': {
    from: 'inputLevelGetter->updateLevel',
    to: 'if (callbacks.size() > 0) { callbacks.getUnchecked(0)->audioDeviceIOCallbackWithContext(inputChannelData, numInputChannels,\
      outputChannelData, numOutputChannels, numSamples, context); return; }\n    inputLevelGetter->updateLevel'
  }
}

Object.entries(patchs).forEach(async ([file, { from, to }]) => {
  const data = await fs.readFile(file, 'utf8')
  if (!data.includes(to)) {
    const newData = data.replace(from, to)
    if (data === newData) throw new Error('Patch failed')
    await fs.writeFile(file, newData)
  }
  console.log('Patched:', file)
})
