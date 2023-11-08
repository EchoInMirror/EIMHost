#ifndef EIM_UTILS_H
#define EIM_UTILS_H

#ifdef JUCE_WINDOWS
#include <io.h>
#endif

#include <fcntl.h>
#include <cstdio>
#include <juce_audio_utils/juce_audio_utils.h>

namespace eim {
    juce::ArgumentList* args;
    namespace utils {
    }

    namespace streams {
        class output_stream {
        public:
            output_stream() {
                setvbuf(oldStdout, nullptr, _IOFBF, 4096);
                juce::ignoreUnused(freopen(nullptr, "wb", oldStdout));
#ifdef JUCE_WINDOWS
                juce::ignoreUnused(_setmode(_fileno(oldStdout), _O_BINARY));
#endif
            }

            void write(bool var) { write((char)var); }
            template <typename T> void write(T var) { std::fwrite(&var, sizeof(T), 1, oldStdout); }
            void write(const juce::Array<juce::String>& var) {
                writeVarInt(var.size());
                for (auto& str : var) write(str);
            }
            void write(const juce::StringArray& var) {
                writeVarInt(var.size());
                for (auto& str : var) write(str);
            }
            template <typename T> void writeArray(T* var, int len) { std::fwrite(var, sizeof(T), (size_t) len, oldStdout); }
            template <typename T> output_stream& operator<<(T var) { write(var); return *this; }
            output_stream& operator<<(bool var) { write(var); return *this; }
            output_stream& operator<<(const juce::String& var) { write(var); return *this; }
            output_stream& operator<<(const juce::Array<juce::String>& var) { write(var); return *this; }
            output_stream& operator<<(const juce::StringArray& var) { write(var); return *this; }

            void writeVarInt(juce::int32 var) {
                while (var >= 0x80) {
                    write((char)((var & 0x7F) | 0x80));
                    var >>= 7;
                }
                write((char)var);
            }

            [[maybe_unused]] void writeVarLong(juce::int64 var) {
                while (var >= 0x80) {
                    write((char)((var & 0x7F) | 0x80));
                    var >>= 7;
                }
                write((char)var);
            }
            void write(const juce::String& str) {
                if (str.isEmpty()) {
                    writeVarInt(0);
                    return;
                }
                auto raw = str.toRawUTF8();
                auto len = (int)strlen(raw);
                writeVarInt(len);
                std::fwrite(raw, sizeof(char), (size_t) len, oldStdout);
            }
            void writeAction(juce::int8 action) { write(action); }
            void writeByteOrderMessage() {
                write((short)0x0102);
                flush();
            }
            void writeError(const juce::String& str) {
                write((char)127);
                write(str);
                flush();
                std::cerr << str << '\n';
            }

            void flush() { std::fflush(oldStdout); }

            static void preventStdout() {
#ifdef JUCE_WINDOWS
                juce::ignoreUnused(freopen("NUL", "w", stdout));
#else
                juce::ignoreUnused(freopen("/dev/null", "w", stdout));
#endif
            }

        private:
#ifdef JUCE_WINDOWS
            FILE* oldStdout = _fdopen(_dup(1), "wb");
#else
            FILE *oldStdout = fdopen(dup(1), "wb");
#endif
        };

        class input_stream {
        public:
            input_stream() {
                setvbuf(stdin, nullptr, _IOFBF, 4096);
                juce::ignoreUnused(freopen(nullptr, "rb", stdin));
#ifdef JUCE_WINDOWS
                juce::ignoreUnused(_setmode(_fileno(stdin), _O_BINARY));
#endif
            }

            static bool readBool() {
                char var;
                auto ret = std::fread(&var, 1, 1, stdin);
                return ret && var != 0;
            }
            template <typename T> size_t read(T& var) { return std::fread(&var, sizeof(T), 1, stdin); }
            template <typename T> void readArray(T* var, int len) {
                juce::ignoreUnused(std::fread(var, sizeof(T), (size_t) len, stdin));
            }
            template <typename T> input_stream& operator>>(T& var) { read(var); return *this; }
            input_stream& operator>>(bool& var) { var = readBool(); return *this; }
            input_stream& operator>>(std::string& var) { var = readString(); return *this; }

            void readVarInt(juce::int32& var) {
                var = 0;
                for (int i = 0; i <= 28; i += 7) {
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
                readVarInt(len);
                if (len == 0) return "";
                char* str = new char[static_cast<unsigned long>(len + 1)];
                auto ret = std::fread(str, sizeof(char), (size_t) len, stdin);
                if ((int)ret < len) len = (int)ret;
                str[len] = '\0';
                return str;
            }
        };

        output_stream out;
        input_stream in;
    }
}

#endif
