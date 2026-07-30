#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gravepitch {

struct StringTarget {
    int stringNumber = 0;
    int midiNote = 0;
    std::string noteName;
    double frequencyHz = 0.0;
};

struct Tuning {
    std::string id;
    std::string name;
    std::vector<int> midiNotesLowToHigh;

    std::vector<StringTarget> targets(double a4Hz) const;
    std::optional<StringTarget> nearestTarget(double frequencyHz, double a4Hz) const;
};

using CustomTuningNoteChoices = std::array<std::array<int, 12>, 6>;

std::vector<Tuning> builtInTunings();
const CustomTuningNoteChoices& customTuningNoteChoices();
std::optional<Tuning> tuningById(std::string_view id);
std::optional<Tuning> tuningFromNoteNames(std::string id, std::string name, const std::vector<std::string>& notesLowToHigh);
std::string serializeCustomTuning(const Tuning& tuning);
std::optional<Tuning> deserializeCustomTuning(std::string_view serialized);

} // namespace gravepitch
