#include <juce_audio_devices/juce_audio_devices.h>
#include <jshm.h>
#include "utils.h"

namespace eim {
    class audio_output : public juce::AudioIODeviceCallback {
    public:
        audio_output(juce::AudioDeviceManager& deviceManager, juce::AudioDeviceManager::AudioDeviceSetup& setup,
            juce::String shmName, int memorySize) : deviceManager(deviceManager), setup(setup) {
            if (shmName.isNotEmpty()) {
                shm.reset(jshm::shared_memory::open(shmName.toRawUTF8(), memorySize));
                if (!shm) exit();
            }
        }
        ~audio_output() {
            shm.reset();
        }

        void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputChannelData, int, int, const juce::AudioIODeviceCallbackContext&) override {
            streams::out.writeAction(0);
            streams::out.flush();
            juce::int8 id;
            if (streams::in.read(id) != 1) {
                exit();
                return;
            }
            switch (id) {
            case 0: {
                juce::int8 numOutputChannels;
                streams::in >> numOutputChannels;
                if (shm) {
                    auto inData = reinterpret_cast<float*>(shm->address());
                    for (int i = 0; i < numOutputChannels; i++)
                        std::memcpy(outputChannelData[i], inData + i * setup.bufferSize, setup.bufferSize * sizeof(float));
                } else for (int i = 0; i < numOutputChannels; i++) streams::in.readArray(outputChannelData[i], setup.bufferSize);
                break;
            }
            case 1:
                openControlPanel();
                break;
            case 2: {
                isRestarting = true;
                deviceManager.closeAudioDevice();
                std::thread restartingThread([this] {
                    juce::int8 id2;
                    do {
                        if (streams::in.read(id2) != 1) {
                            exit();
                            return;
                        }
                        deviceManager.restartLastAudioDevice();
                    } while (id2 != 3);
                });
                restartingThread.detach();
                break;
            }
            default: exit();
            }
        }

        void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
            auto bufSize = device->getCurrentBufferSizeSamples();
            auto bufferSizes = device->getAvailableBufferSizes();
            auto sampleRates = device->getAvailableSampleRates();
            streams::out.writeAction(1);
            streams::out << ("[" + device->getTypeName() + "] " + device->getName());
            streams::out.writeVarInt(device->getInputLatencyInSamples());
            streams::out.writeVarInt(device->getOutputLatencyInSamples());
            streams::out.writeVarInt((int)device->getCurrentSampleRate());
            streams::out.writeVarInt(bufSize);
            streams::out.writeVarInt(sampleRates.size());
            for (auto it : sampleRates) streams::out.writeVarInt((int)it);
            streams::out.writeVarInt(bufferSizes.size());
            for (int it : bufferSizes) streams::out.writeVarInt(it);
            streams::out << device->hasControlPanel();
            streams::out.flush();
            int outBufferSize;
            streams::in >> outBufferSize;
            if (setup.bufferSize != bufSize) setup.bufferSize = bufSize;
            if (shm && outBufferSize) shm.reset(jshm::shared_memory::open(shm->name(), outBufferSize));
        }

        void audioDeviceStopped() override {
            if (isRestarting) isRestarting = false;
            else exit();
        }

        void audioDeviceError(const juce::String& errorMessage) override {
            std::cout << errorMessage << '\n';
            isErrorExit = true;
            exit();
        }

        void openControlPanel() {
            juce::MessageManager::callAsync([this] {
                auto device = deviceManager.getCurrentAudioDevice();
                if (device && device->hasControlPanel()) {
                    juce::Component modalWindow;
                    modalWindow.setOpaque(true);
                    modalWindow.addToDesktop(0);
                    modalWindow.enterModalState();
                    modalWindow.toFront(true);
                    if (device->showControlPanel()) {
                        isRestarting = true;
                        deviceManager.closeAudioDevice();
                        deviceManager.restartLastAudioDevice();
                    }
                }
            });
        }

        int getExitCode() { return isErrorExit; }

        void exit() {
            juce::MessageManager::getInstance()->stopDispatchLoop();
        }

    private:
        std::unique_ptr<jshm::shared_memory> shm;
        bool isErrorExit = false, isRestarting = false;
        juce::AudioDeviceManager& deviceManager;
        juce::AudioDeviceManager::AudioDeviceSetup& setup;
    };
}