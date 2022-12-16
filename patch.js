const fs = require('fs')

const FILE = 'JUCE/modules/juce_gui_basics/native/juce_win32_Windowing.cpp'
const DATA = 'hwnd = parentToAddTo != nullptr && (styleFlags & (1 << 28)) ? CreateWindow(WindowClassHolder::getInstance()->getWindowClassName(), L"",\
WS_POPUP | WS_SYSMENU | WS_CAPTION | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,\
0, 0, 0, 0, parentToAddTo, nullptr, (HINSTANCE)Process::getCurrentModuleInstanceHandle(), nullptr) : CreateWindowEx'

const data = fs.readFileSync(FILE, 'utf8')
if (data.includes(DATA)) throw new Error('Already patched')

fs.writeFileSync(FILE, data.replace('hwnd = CreateWindowEx', DATA))
