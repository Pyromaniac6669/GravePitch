#include "gravepitch/core/Tuning.h"

#include "gravepitch/core/Note.h"

#include <cmath>
#include <limits>
#include <sstream>
#include <utility>

namespace gravepitch {
namespace {

Tuning makeTuning(const std::string& id, const std::string& name, const std::vector<std::string>& notes)
{
    std::vector<int> midiNotes;
    midiNotes.reserve(notes.size());

    for (const auto& note : notes) {
        const auto midi = midiNoteFromName(note);
        if (midi) {
            midiNotes.push_back(*midi);
        }
    }

    return {id, name, midiNotes};
}

CustomTuningNoteChoices makeCustomTuningNoteChoices()
{
    constexpr std::array<int, 6> firstMidiNotes {35, 40, 45, 50, 54, 59};
    CustomTuningNoteChoices result {};

    for (std::size_t stringIndex = 0; stringIndex < result.size(); ++stringIndex) {
        for (std::size_t choiceIndex = 0; choiceIndex < result[stringIndex].size(); ++choiceIndex) {
            result[stringIndex][choiceIndex]
                = firstMidiNotes[stringIndex] + static_cast<int>(choiceIndex);
        }
    }

    return result;
}

std::vector<std::string> split(std::string_view value, char separator)
{
    std::vector<std::string> parts;
    std::string current;

    for (char ch : value) {
        if (ch == separator) {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }

    parts.push_back(current);
    return parts;
}

} // namespace

std::vector<StringTarget> Tuning::targets(double a4Hz) const
{
    std::vector<StringTarget> result;
    result.reserve(midiNotesLowToHigh.size());

    for (std::size_t index = 0; index < midiNotesLowToHigh.size(); ++index) {
        const int midiNote = midiNotesLowToHigh[index];
        result.push_back({
            static_cast<int>(midiNotesLowToHigh.size() - index),
            midiNote,
            noteNameForMidi(midiNote),
            frequencyForMidiNote(midiNote, a4Hz)
        });
    }

    return result;
}

std::optional<StringTarget> Tuning::nearestTarget(double frequencyHz, double a4Hz) const
{
    if (frequencyHz <= 0.0 || midiNotesLowToHigh.empty()) {
        return std::nullopt;
    }

    auto allTargets = targets(a4Hz);
    double bestDistance = std::numeric_limits<double>::max();
    std::optional<StringTarget> bestTarget;

    for (const auto& target : allTargets) {
        const double distance = std::abs(centsDifference(frequencyHz, target.midiNote, a4Hz));
        if (distance < bestDistance) {
            bestDistance = distance;
            bestTarget = target;
        }
    }

    return bestTarget;
}

std::vector<Tuning> builtInTunings()
{
    return {
        makeTuning("standard", "Standard", {"E2", "A2", "D3", "G3", "B3", "E4"}),
        makeTuning("eb_standard", "Eb Standard", {"Eb2", "Ab2", "Db3", "Gb3", "Bb3", "Eb4"}),
        makeTuning("d_standard", "D Standard", {"D2", "G2", "C3", "F3", "A3", "D4"}),
        makeTuning("csharp_standard", "C# Standard", {"C#2", "F#2", "B2", "E3", "G#3", "C#4"}),
        makeTuning("c_standard", "C Standard", {"C2", "F2", "Bb2", "Eb3", "G3", "C4"}),
        makeTuning("b_standard", "B Standard", {"B1", "E2", "A2", "D3", "F#3", "B3"}),
        makeTuning("drop_d", "Drop D", {"D2", "A2", "D3", "G3", "B3", "E4"}),
        makeTuning("drop_csharp", "Drop C#", {"C#2", "G#2", "C#3", "F#3", "A#3", "D#4"}),
        makeTuning("drop_c", "Drop C", {"C2", "G2", "C3", "F3", "A3", "D4"}),
        makeTuning("drop_b", "Drop B", {"B1", "F#2", "B2", "E3", "G#3", "C#4"}),
        makeTuning("double_drop_d", "Double Drop D", {"D2", "A2", "D3", "G3", "B3", "D4"}),
        makeTuning("open_g", "Open G", {"D2", "G2", "D3", "G3", "B3", "D4"})
    };
}

const CustomTuningNoteChoices& customTuningNoteChoices()
{
    static const auto choices = makeCustomTuningNoteChoices();
    return choices;
}

std::optional<Tuning> tuningById(std::string_view id)
{
    for (auto tuning : builtInTunings()) {
        if (tuning.id == id) {
            return tuning;
        }
    }

    return std::nullopt;
}

std::optional<Tuning> tuningFromNoteNames(std::string id, std::string name, const std::vector<std::string>& notesLowToHigh)
{
    if (notesLowToHigh.size() != 6) {
        return std::nullopt;
    }

    std::vector<int> midiNotes;
    midiNotes.reserve(notesLowToHigh.size());

    for (const auto& noteName : notesLowToHigh) {
        const auto midiNote = midiNoteFromName(noteName);
        if (!midiNote) {
            return std::nullopt;
        }
        midiNotes.push_back(*midiNote);
    }

    return Tuning {std::move(id), std::move(name), std::move(midiNotes)};
}

std::string serializeCustomTuning(const Tuning& tuning)
{
    std::ostringstream output;
    output << "gravepitch-tuning-v1|" << tuning.id << '|' << tuning.name << '|';

    for (std::size_t index = 0; index < tuning.midiNotesLowToHigh.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << noteNameForMidi(tuning.midiNotesLowToHigh[index]);
    }

    return output.str();
}

std::optional<Tuning> deserializeCustomTuning(std::string_view serialized)
{
    const auto parts = split(serialized, '|');

    if (parts.size() != 4 || parts[0] != "gravepitch-tuning-v1") {
        return std::nullopt;
    }

    return tuningFromNoteNames(parts[1], parts[2], split(parts[3], ','));
}

} // namespace gravepitch
