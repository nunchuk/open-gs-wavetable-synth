// SPDX-License-Identifier: MIT
// The Microsoft GS Wavetable synth engine as Windows 10 / Server 2022 runs it for a 32-bit program:
// the DirectMusic synth core linked into SysWOW64\wdmaud.drv 10.0.20348.2849 (CSynth,
// CControlLogic, CVoice, CVoiceEG, CVoiceLFO, CDigitalAudio, CInstManager and the Vista+ DLS
// loader), reimplemented from its disassembly with the public PDB (wdmaud.pdb
// 4FF5DE9B-94AA-0B8E-D202-A2248503E978 age 1). It is integer arithmetic
// throughout except the tables and the articulation conversions, and it mixes the way the x86
// build does on a CPU with MMX: spans of more than 8 frames through CDigitalAudio::Mix16X's MMX
// arithmetic, shorter ones through Mix16. Given the same events at the same sample times and the
// same Render calls, it renders the same samples as the Windows synth (verified against
// recordings of the Windows synth's inputs and outputs; README.md).
// Reverb is off and there is no chorus, as the synth is opened by LoadDLSFile; the DLS2 filter is
// never enabled by a DLS1 bank such as gm.dls and is not implemented.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace gswavetable {

using STime = std::int64_t; // sample time, the synth's STIME

inline constexpr std::uint32_t engine_rate = 22050; // CUserModeSynth::Open(NULL): 0x5622

// The engine's tables as CVoice::Init, CVoiceEG::Init, CVoiceLFO::Init and CDigitalAudio::Init
// compute them (wdmaud.drv RVAs 0x2b4f9, 0x2b579, 0x2b5e1, 0x109a9).
struct Tables {
    std::array<std::int32_t, 1001> vfract;    // [vrel/10 + 1000]: 4095 * 10^(vrel/2000)
    std::array<std::int32_t, 201> pitch_fine; // [cents + 100]: 4096 * 2^(cents/1200)
    std::array<std::int32_t, 97> pitch_semi;  // [semitones + 48]: 4096 * 2^(st/12)
    std::array<std::int16_t, 201> attack;     // m_snAttackTable
    std::array<std::int16_t, 256> sine;       // m_snSineTable
    std::array<std::int32_t, 128> pan;        // m_svrPanToVREL
};
const Tables &tables();

// CDigitalAudio::PRELToPFRACT: cents to a 4.12 pitch ratio.
std::int32_t prel_to_pfract(std::int32_t cents);
// CDigitalAudio::VRELToVFRACT: 1/100 dB to a 0..4095 amplitude.
std::int32_t vrel_to_vfract(std::int32_t vrel);

struct Bank; // gm.dls parsed and downloaded as CDLSCollection and CInstManager hold it

// RIFFLoad<CDLSCollection>, PostLoadProcessing and CDLSCollection::Download into a fresh
// CInstManager. Throws std::runtime_error when the engine would reject the file. The result is
// immutable: every synth opened on it gets the same instruments.
std::shared_ptr<const Bank> load_dls(std::span<const std::uint8_t> dls);

// One synth as CMicrosoftGSWavetableSynth opens it (CSynth::Open with one channel group,
// 32 voices plus 8 extra, no reverb, stereo) on a loaded bank.
class Synth {
  public:
    explicit Synth(std::shared_ptr<const Bank> bank);
    ~Synth();
    Synth(const Synth &) = delete;
    Synth &operator=(const Synth &) = delete;

    // CSynth::PlayBuffer for one event already converted to a sample time (the sink's
    // RefTimeToSample). A message of 1-3 bytes goes to CControlLogic::RecordMIDI, a longer one
    // (a complete F0..F7 SysEx) to RecordSysEx.
    void play(STime time, std::span<const std::uint8_t> message);
    // CSynth::Mix for `frames` stereo frames at `position` into `stereo` (overwritten: the synth
    // sink hands Render a zeroed buffer).
    void render(std::int16_t *stereo, std::uint32_t frames, STime position);
    [[nodiscard]] bool voices_active() const noexcept; // a voice is in use
    [[nodiscard]] STime last_time() const noexcept;    // CSynth +0x18: the end of the last mix
    struct Statistics {
        std::uint32_t most_voices = 0;    // voices in use after a mix, at most
        std::uint64_t reserve_stops = 0;  // StealNotes quick-stopping the oldest to keep 8 free
        std::uint64_t stolen = 0;         // StealVoice cutting a voice for a note
        std::uint64_t notes = 0;          // note-ons that found an instrument
    };
    [[nodiscard]] const Statistics &statistics() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gswavetable
