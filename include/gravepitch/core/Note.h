#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace gravepitch {

constexpr double defaultA4Hz = 440.0;
constexpr int a4MidiNote = 69;

double frequencyForMidiNote(int midiNote, double a4Hz = defaultA4Hz);
int nearestMidiNote(double frequencyHz, double a4Hz = defaultA4Hz);
double centsDifference(double frequencyHz, int midiNote, double a4Hz = defaultA4Hz);
std::string noteNameForMidi(int midiNote, bool preferFlats = false);
std::optional<int> midiNoteFromName(std::string_view noteName);

} // namespace gravepitch

