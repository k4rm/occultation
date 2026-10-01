#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>

// A compact polyphonic synth engine: oscillator -> filter -> ADSR per voice,
// mixed down and run through a small master effects chain. Lives entirely
// separate from PluginProcessor's image-to-MIDI logic — that logic still
// decides WHICH notes play and WHEN (via noteOn/noteOff below, called from
// the same place addMidiMessage already was), this only decides what they
// sound like. Kept as one file (mirroring SkyMapRenderer's one-file-per-
// subsystem shape already used in this codebase) rather than splitting
// oscillator/filter/voice into separate files.
class SynthEngine
{
public:
    // The "personality" knob: each type sets a real, distinct combination of
    // waveform, filter character and envelope shape — not just a label. Not
    // the "100 presets across genres" originally asked for (that's a real
    // sound-design content task, not something to fake with copy-pasted
    // values) — genuinely distinct, tuned starting points instead (the
    // first 6 generic, the rest named after well-known film-score sounds
    // as a quick, memorable way to convey character), meant to be grown
    // later rather than replaced.
    enum class SynthType
    {
        Pluck = 0, Ambient, House, Electro, Acid, Moog,
        BladeRunner, Drive, Halloween, ClockworkOrange, Terminator,
        Interstellar, Suspiria, Predator, Dune, Arrival,
        MrRobot, TwinPeaks, Sicario, AkiraPulse, NightDriver, Hackers,
        Moroder, BackToTheFuture, WaynesWorld,
        NumTypes
    };
    static const char* getTypeName (SynthType type);

    enum class FilterType { StateVariable = 0, MoogLadder };
    enum class Waveform { Sine = 0, Saw, Square, Triangle, Noise };

    // The full parameter set one SynthType applies at once — exposed
    // publicly (not just used internally by applyType()) so PluginEditor's
    // Type-change handling can read the exact same table to keep the other
    // knobs' on-screen values in sync, rather than maintaining a second,
    // hand-duplicated copy of every preset that could silently drift out
    // of sync with this one as more get added.
    struct TypePreset
    {
        Waveform waveform; float unisonDetuneCents;
        float attack, decay, sustain, release;
        FilterType filterType; float filterCutoffHz, filterResonance01, filterEnvAmount01;
        float distortionAmount01 = 0.0f, chorusAmount01 = 0.0f;
        float delayAmount01 = 0.0f, delayTimeSeconds = 0.25f, reverbAmount01 = 0.0f;
    };
    static const TypePreset& getTypePreset (SynthType type);

    SynthEngine();

    void prepare (double sampleRate, int samplesPerBlock);
    void reset();

    // Called from PluginProcessor at exactly the same points addMidiMessage
    // already is — the note-selection logic upstream is unchanged, only
    // what happens to a chosen note changes.
    void noteOn (int midiNote, float velocity01);
    void noteOff (int midiNote);

    // Renders `numSamples` of audio into `outBuffer` starting at 0, replacing
    // (not adding to) its contents — call once per processBlock.
    void renderNextBlock (juce::AudioBuffer<float>& outBuffer, int numSamples);

    // Applies a SynthType's whole parameter set at once (waveform, filter
    // character, envelope shape) — used when the Type dropdown changes, so
    // switching types is a single coherent jump rather than the knobs
    // fighting whatever was there before.
    void applyType (SynthType type);

    // --- Individual parameters, each independently tweakable after (or
    // instead of) picking a Type. All take normalized-feeling ranges
    // directly (not 0..1) since these are set from real APVTS parameters
    // with their own ranges, not raw knob positions. ---
    void setWaveform (Waveform w);
    void setUnisonDetuneCents (float cents);
    void setAttack (float seconds);
    void setDecay (float seconds);
    void setSustain (float level01);
    void setRelease (float seconds);
    void setFilterType (FilterType t);
    void setFilterCutoffHz (float hz);
    void setFilterResonance01 (float res);
    void setFilterEnvAmount01 (float amount);
    void setMasterVolume01 (float v);
    // Time for a new note to slide from the previous one, in seconds; 0 is
    // off. Glide is monophonic-by-nature — it slides from the last note
    // played, whichever voice takes the new one — which suits the way the
    // sequencer feeds this engine one note at a time.
    void setGlideSeconds (float seconds);

    // --- Effects (each a real, working DSP block, not a stub) ---
    void setDistortionAmount01 (float amt);
    void setChorusAmount01 (float amt);
    void setDelayAmount01 (float amt);
    void setDelayTimeSeconds (float t);
    void setReverbAmount01 (float amt);

    // --- One-shot SFX for Invader mode (see PluginProcessor's invader
    // state) — mixed into the same output as the voices, ahead of the
    // master effects chain, so they pick up whatever reverb/delay/chorus
    // the current preset is already using rather than sounding pasted on.
    // Neither is MIDI-driven (no note number makes sense for a laser zap
    // or a noise boom), so they're plain retriggerable state machines
    // rather than going through the Voice/ADSR machinery above.
    void triggerLaser (float startFrequencyHz);   // sweeps down from here — see PluginProcessor::fireBullet()
    void triggerExplosion (float intensity01);   // 0..1, scales loudness and how long the rumble rings out

private:
    // One-pole-cascade approximation of a transistor ladder filter (Moog
    // character) — not Steinberg/Moog's own patented topology, just the
    // well-known "4 cascaded one-poles plus a feedback tap" shape that
    // gives the same soft, resonant low-pass roll-off distinct from the
    // StateVariable filter's cleaner response. Runs per-voice, like the SVF.
    struct LadderFilter
    {
        void reset() { for (auto& s : stage) s = 0.0f; }
        float process (float input, float cutoff01, float resonance01)
        {
            // cutoff01/resonance01 already pre-warped by the caller.
            float g = cutoff01;
            float fb = resonance01 * 4.0f;
            float x = input - fb * stage[3];
            stage[0] += g * (std::tanh (x)        - stage[0]);
            stage[1] += g * (std::tanh (stage[0])  - stage[1]);
            stage[2] += g * (std::tanh (stage[1])  - stage[2]);
            stage[3] += g * (std::tanh (stage[2])  - stage[3]);
            return stage[3];
        }
        std::array<float, 4> stage { 0.0f, 0.0f, 0.0f, 0.0f };
    };

    struct Voice
    {
        bool active = false;
        int midiNote = -1;
        double phase = 0.0;
        double phaseIncrement = 0.0;
        // Portamento. The ramp runs in LOG pitch, not in Hz: a straight line
        // in Hz from C3 to C5 spends most of its time in the top octave and
        // arrives with a lurch, where a straight line in log pitch is even in
        // semitones, which is what a glide is supposed to sound like.
        // `logIncStep` is precomputed per note-on so the per-sample cost while
        // gliding is one add and one exp, and nothing at all once it lands.
        double logInc = 0.0, logIncTarget = 0.0, logIncStep = 0.0;
        int glideSamplesRemaining = 0;
        float velocity = 0.0f;
        juce::ADSR adsr;
        juce::dsp::StateVariableTPTFilter<float> svf;
        LadderFilter ladder;
        juce::Random noiseRng;

        float nextOscSample (Waveform wave, double sampleRate);
    };

    static constexpr int numVoices = 8;
    std::array<Voice, numVoices> voices;
    Voice* findVoiceToRetrigger (int midiNote);

    double currentSampleRate = 44100.0;
    juce::ADSR::Parameters adsrParams { 0.01f, 0.15f, 0.7f, 0.3f };

    Waveform waveform = Waveform::Saw;
    float unisonDetuneCents = 0.0f;
    float glideSeconds = 0.0f;
    // The note the glide starts from. 0 means "nothing has sounded yet", so
    // the very first note after a reset arrives at pitch instead of swooping
    // up from silence.
    double lastNoteFrequencyHz = 0.0;
    FilterType filterType = FilterType::StateVariable;
    float filterCutoffHz = 2000.0f;
    float filterResonance01 = 0.2f;
    float filterEnvAmount01 = 0.5f;
    float masterVolume01 = 0.7f;

    // --- Master effects chain (applied to the summed voice mix, in a fixed
    // order: distortion -> chorus -> delay -> reverb -> compressor) ---
    // masterVolume01/distortionAmount01/delayAmount01/delayTimeSeconds keep
    // a plain float as "the last value the setter was given" (applyType()
    // and getters, where anything reads them back, want that — not a
    // smoother mid-ramp), while renderNextBlock reads the matching
    // smoothedXxx member instead of these directly. Changing any of these
    // used to take effect on the very next block — at this app's small
    // block sizes (Standalone defaults to 16 samples) a knob drag or host
    // automation could change a value dozens of times a second, and nothing
    // smoothed the jump between blocks. delayTimeSeconds was the most
    // audible: a jump there moves the delay line's read point discontinuously,
    // which is an audible click/glitch on every single change.
    float distortionAmount01 = 0.0f;
    float chorusAmount01 = 0.0f;
    float delayAmount01 = 0.0f;
    float delayTimeSeconds = 0.25f;
    float reverbAmount01 = 0.0f;

    juce::SmoothedValue<float> smoothedMasterVolume01, smoothedDistortionAmount01;
    juce::SmoothedValue<float> smoothedDelayAmount01, smoothedDelayTimeSeconds;

    juce::dsp::Chorus<float> chorus;
    juce::dsp::Reverb reverb;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine { 1 << 17 };

    // Final-stage compressor + makeup gain: presets vary wildly in raw
    // loudness (a sustained Ambient pad vs. a short Acid pluck), so without
    // this some were barely audible next to others. Always on, fixed
    // settings — not a user-exposed knob, just output normalisation.
    juce::dsp::Compressor<float> compressor;

    // --- Invader-mode one-shot SFX — see triggerLaser()/triggerExplosion() ---
    // Two detuned square voices (for width) plus a fast vibrato and a
    // percussive onset click, rather than one bare swept tone. Each also
    // carries its own filter instance (ladder/svf below, run through
    // applySharedFilter()) — the SAME Filter Type/Cutoff/Resonance knobs
    // that shape the regular voices, so a laser/explosion isn't a fixed
    // timbre pasted on top of whatever preset is selected.
    struct LaserSfx {
        bool active = false;
        double phase = 0.0;
        double detunedPhase = 0.0;
        double t = 0.0;                          // seconds since triggered
        double startFrequencyHz = 3200.0;         // set by triggerLaser() — sweeps down ~20x from here
        juce::Random rng;                        // for the onset click transient
        LadderFilter ladder;
        juce::dsp::StateVariableTPTFilter<float> svf;
        // A fixed high-cut sitting AFTER the shared Filter knobs, always on
        // and not user-exposed. Two detuned squares starting as high as 8kHz
        // put a lot of energy in the top octaves, and the zap read as
        // piercingly shrill no matter where the preset's own cutoff sat. This
        // takes the shriek off the start of the sweep (12dB/oct from 1.5kHz)
        // while leaving the descending body of the "pew" untouched — the
        // sweep drops below the corner within the first few milliseconds.
        juce::dsp::StateVariableTPTFilter<float> highCut;
        static constexpr float highCutHz = 1500.0f;
        static constexpr double duration = 0.2;
    } laserSfx;

    // A crack (bright, fast) + rumble (dark, slow) noise layer plus a
    // sub-bass thump, rather than one short filtered-noise burst — reads as
    // a real "boom" with a tail instead of a snap. The combined layers pass
    // through the same shared filter as the laser above before mixing in.
    struct ExplosionSfx {
        bool active = false;
        double t = 0.0;
        double duration = 0.3;
        float intensity = 1.0f;
        float crackFilterState = 0.0f;
        float rumbleFilterState = 0.0f;
        double thumpPhase = 0.0;
        juce::Random rng;
        LadderFilter ladder;
        juce::dsp::StateVariableTPTFilter<float> svf;
    } explosionSfx;

    // Runs `input` through whichever filter (ladder or state-variable) the
    // Filter Type knob currently selects, at the current Cutoff/Resonance —
    // shared by triggerLaser()/triggerExplosion()'s render code so ship SFX
    // are shaped by the same knobs as the regular voices, instead of always
    // passing through untouched. No envelope modulation (unlike the voices'
    // own filterEnvAmount01) — these already have their own amplitude
    // envelopes; this only applies the static cutoff/resonance/type.
    float applySharedFilter (float input, LadderFilter& ladder, juce::dsp::StateVariableTPTFilter<float>& svf);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthEngine)
};
