#pragma warning(disable: 6031)
#pragma warning(disable: 6387)
#pragma warning(disable: 6029)

#ifndef EIM_UTILS_H
#define EIM_UTILS_H

#include <io.h>
#include <fcntl.h>
#include <cstdio>
#include <juce_audio_utils/juce_audio_utils.h>

namespace eim {
    juce::ArgumentList* args;
    namespace utils {
    };

    namespace streams {
        class output_stream {
        public:
            output_stream() {
                setvbuf(stderr, nullptr, _IOFBF, 4096);
                freopen(nullptr, "wb", stderr);
#ifdef _WIN32
                _setmode(_fileno(stderr), _O_BINARY);
#endif
            }

            void write(bool var) { write((char)var); }
            template <typename T> void write(T var) { std::fwrite(&var, sizeof(T), 1, stderr); }
            void write(juce::Array<juce::String> var) {
                writeVarInt(var.size());
                for (auto& str : var) write(str);
            }
            template <typename T> void writeArray(T* var, int len) { std::fwrite(var, sizeof(T), len, stderr); }
            template <typename T> output_stream& operator<<(T var) { write(var); return *this; }
            output_stream& operator<<(bool var) { write(var); return *this; }
            output_stream& operator<<(juce::String var) { write(var); return *this; }
            output_stream& operator<<(juce::Array<juce::String> var) { write(var); return *this; }

            void writeVarInt(juce::int32 var) {
                while (var >= 0x80) {
                    write((char)(var & 0x7F | 0x80));
                    var >>= 7;
                }
                write((char)var);
            }
            void writeVarLong(juce::int64 var) {
                while (var >= 0x80) {
                    write((char)(var & 0x7F | 0x80));
                    var >>= 7;
                }
                write((char)var);
            }
            void write(juce::String str) {
                auto raw = str.toRawUTF8();
                auto len = (int)strlen(raw);
                writeVarInt(len);
                std::fwrite(raw, sizeof(char), len, stderr);
            }
            void writeAction(juce::int8 action) { write(action); }
            void writeByteOrderMessage() {
                write((short)0x0102);
                flush();
            }
            void writeError(juce::String str) {
                write((char)127);
                write(str);
                flush();
                std::cout << str << '\n';
            }

            void flush() { std::fflush(stderr); }
        };

        class input_stream {
        public:
            input_stream() {
                setvbuf(stdin, nullptr, _IOFBF, 4096);
                freopen(nullptr, "rb", stdin);
#ifdef _WIN32
                _setmode(_fileno(stdin), _O_BINARY);
#endif
            }

            bool readBool() {
                char var;
                std::fread(&var, 1, 1, stdin);
                return var != 0;
            }
            template <typename T> size_t read(T& var) { return std::fread(&var, sizeof(T), 1, stdin); }
            template <typename T> void readArray(T* var, int len) { std::fread(var, sizeof(T), len, stdin); }
            template <typename T> input_stream& operator>>(T& var) { read(var); return *this; }
            input_stream& operator>>(bool& var) { var = readBool(); return *this; }
            input_stream& operator>>(std::string& var) { var = readString(); return *this; }

            void readVarInt(juce::int32& var) {
                var = 0;
                for (int i = 0; i < 32; i += 7) {
                    char b;
                    read(b);
                    var |= (juce::int32)(b & 0x7F) << i;
                    if ((b & 0x80) == 0) return;
                }
            }
            void readVarLong(juce::int64& var) {
                var = 0;
                for (int i = 0; i < 64; i += 7) {
                    char b;
                    read(b);
                    var |= (juce::int64)(b & 0x7F) << i;
                    if ((b & 0x80) == 0) return;
                }
            }
            std::string readString() {
                int len;
                read(len);
                char* str = new char[len + 1];
                std::fread(str, sizeof(char), len, stdin);
                str[len] = '\0';
                return str;
            }
        };

        output_stream out;
        input_stream in;
    };
};

#endif
