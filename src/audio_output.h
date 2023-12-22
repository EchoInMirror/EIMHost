#include <juce_audio_devices/juce_audio_devices.h>
#include <jshm.h>
#include "utils.h"

namespace eim {
    class audio_output : public juce::AudioIODeviceCallback {
    public:
        audio_output(juce::AudioDeviceManager& _deviceManager, juce::AudioDeviceManager::AudioDeviceSetup& _setup,
            const juce::String& shmName, int memorySize) : deviceManager(_deviceManager), setup(_setup) {
            if (shmName.isNotEmpty()) {
                shm.reset(jshm::shared_memory::open(shmName.toRawUTF8(), memorySize));
                if (!shm) exit();
            }
        }
        ~audio_output() override {
            shm.reset();
        }

        void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputChannelData, int, int, const juce::AudioIODeviceCallbackContext&) override {
            streams::output().writeAction(0);
            streams::output().flush();
            juce::int8 id;
            if (streams::input().read(id) != 1) {
                exit();
                return;
            }
            switch (id) {
            case 0: {
                juce::int8 numOutputChannels;
                streams::input() >> numOutputChannels;
                if (shm) {
                    auto inData = reinterpret_cast<float*>(shm->address());
                    for (int i = 0; i < numOutputChannels; i++)
                        std::memcpy(outputChannelData[i], inData + i * setup.bufferSize, (size_t) setup.bufferSize * sizeof(float));
                } else for (int i = 0; i < numOutputChannels; i++) streams::input().readArray(outputChannelData[i], setup.bufferSize);
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
                        if (streams::input().read(id2) != 1) {
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
            streams::output().writeAction(1);
            streams::output() << ("[" + device->getTypeName() + "] " + device->getName());
            streams::output().writeVarInt(device->getInputLatencyInSamples());
            streams::output().writeVarInt(device->getOutputLatencyInSamples());
            streams::output().writeVarInt((int)device->getCurrentSampleRate());
            streams::output().writeVarInt(bufSize);
            streams::output().writeVarInt(sampleRates.size());
            for (auto it : sampleRates) streams::output().writeVarInt((int)it);
            streams::output().writeVarInt(bufferSizes.size());
            for (int it : bufferSizes) streams::output().writeVarInt(it);
            streams::output() << device->hasControlPanel();
            streams::output().flush();
            int outBufferSize;
            streams::input() >> outBufferSize;
            if (setup.bufferSize != bufSize) setup.bufferSize = bufSize;
            if (shm && outBufferSize) shm.reset(jshm::shared_memory::open(shm->name(), outBufferSize));
        }

        void audioDeviceStopped() override {
            if (isRestarting) isRestarting = false;
            else exit();
        }

        void audioDeviceError(const juce::String& errorMessage) override {
            std::cerr << errorMessage << '\n';
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

        [[nodiscard]] int getExitCode() const { return isErrorExit; }

        static void exit() {
            juce::MessageManager::getInstance()->stopDispatchLoop();
        }

    private:
        std::unique_ptr<jshm::shared_memory> shm;
        bool isErrorExit = false, isRestarting = false;
        juce::AudioDeviceManager& deviceManager;
        juce::AudioDeviceManager::AudioDeviceSetup& setup;
    };
}