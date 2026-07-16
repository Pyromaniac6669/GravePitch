#include "gravepitch/core/Note.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>

namespace gravepitch {
namespace {

constexpr std::array<const char*, 12> sharpNames {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

constexpr std::array<const char*, 12> flatNames {
    "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"
};

int positiveModulo(int value, int divisor)
{
    const int result = value % divisor;
    return result < 0 ? result + divisor : result;
}

std::string trimAndUpperFirst(std::string_view value)
{
    std::string result;
    result.reserve(value.size());

    for (char ch : value) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            result.push_back(ch);
        }
    }

    if (!result.empty()) {
        result[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(result[0])));
    }

    return result;
}

std::optional<int> pitchClassFromName(std::string_view noteName)
{
    if (noteName.empty()) {
        return std::nullopt;
    }

    switch (noteName[0]) {
    case 'C': return 0;
    case 'D': return 2;
    case 'E': return 4;
    case 'F': return 5;
    case 'G': return 7;
    case 'A': return 9;
    case 'B': return 11;
    default: return std::nullopt;
    }
}

} // namespace

double frequencyForMidiNote(int midiNote, double a4Hz)
{
    return a4Hz * std::pow(2.0, static_cast<double>(midiNote - a4MidiNote) / 12.0);
}

int nearestMidiNote(double frequencyHz, double a4Hz)
{
    if (frequencyHz <= 0.0 || a4Hz <= 0.0) {
        return a4MidiNote;
    }

    return static_cast<int>(std::lround(12.0 * std::log2(frequencyHz / a4Hz) + a4MidiNote));
}

double centsDifference(double frequencyHz, int midiNote, double a4Hz)
{
    const double targetFrequency = frequencyForMidiNote(midiNote, a4Hz);

    if (frequencyHz <= 0.0 || targetFrequency <= 0.0) {
        return 0.0;
    }

    return 1200.0 * std::log2(frequencyHz / targetFrequency);
}

std::string noteNameForMidi(int midiNote, bool preferFlats)
{
    const int pitchClass = positiveModulo(midiNote, 12);
    const int octave = (midiNote / 12) - 1;
    const auto& names = preferFlats ? flatNames : sharpNames;
    return std::string(names[static_cast<std::size_t>(pitchClass)]) + std::to_string(octave);
}

std::optional<int> midiNoteFromName(std::string_view noteName)
{
    const auto normalized = trimAndUpperFirst(noteName);

    if (normalized.size() < 2) {
        return std::nullopt;
    }

    auto pitchClass = pitchClassFromName(normalized);
    if (!pitchClass) {
        return std::nullopt;
    }

    std::size_t octaveStart = 1;

    if (normalized.size() > 2 && (normalized[1] == '#' || normalized[1] == 'b' || normalized[1] == 'B')) {
        *pitchClass += normalized[1] == '#' ? 1 : -1;
        octaveStart = 2;
    }

    if (octaveStart >= normalized.size()) {
        return std::nullopt;
    }

    bool negative = false;
    std::size_t digitStart = octaveStart;

    if (normalized[digitStart] == '-') {
        negative = true;
        ++digitStart;
    }

    if (digitStart >= normalized.size()) {
        return std::nullopt;
    }

    int octave = 0;
    for (std::size_t i = digitStart; i < normalized.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(normalized[i]))) {
            return std::nullopt;
        }
        octave = octave * 10 + (normalized[i] - '0');
    }

    if (negative) {
        octave = -octave;
    }

    return (octave + 1) * 12 + positiveModulo(*pitchClass, 12);
}

} // namespace gravepitch

