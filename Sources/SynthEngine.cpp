#include "SynthEngine.h"
#include <cmath>

const char* SynthEngine::getTypeName (SynthType type)
{
    static const char* names[] = {
        "Pluck", "Ambient", "House", "Electro", "Acid", "Moog",
        "Blade Runner", "Drive", "Halloween", "Clockwork Orange", "Terminator",
        "Interstellar", "Suspiria", "Predator", "Dune", "Arrival",
        "Mr. Robot", "Twin Peaks", "Sicario", "Akira Pulse", "Night Driver", "Hackers",
        "Moroder", "Back to the Future", "Wayne's World",
    };
    int index = (int) type;
    return (index >= 0 && index < (int) std::size (names)) ? names[index] : "?";
}

// One entry per SynthType, in enum declaration order — the single source
// of truth both applyType() (below) and PluginEditor's Type-change knob
// sync read from, so the two can't drift apart as more types are added.
// { waveform, detuneCents, attack, decay, sustain, release, filterType,
//   cutoffHz, resonance01, envAmount01, distortion01, chorus01, delay01,
//   delayTimeSeconds, reverb01 }
const SynthEngine::TypePreset& SynthEngine::getTypePreset (SynthType type)
{
    using W = Waveform; using F = FilterType;
    static const TypePreset presets[] = {
        /* Pluck          */ { W::Triangle, 0.0f,  0.002f, 0.25f, 0.0f,  0.15f, F::StateVariable, 3000.0f, 0.15f, 0.6f },
        /* Ambient        */ { W::Sine,     6.0f,  1.2f,   0.8f,  0.8f,  2.5f,  F::StateVariable, 1200.0f, 0.1f,  0.2f, 0.0f, 0.4f, 0.0f, 0.25f, 0.6f },
        /* House          */ { W::Saw,      8.0f,  0.005f, 0.12f, 0.6f,  0.2f,  F::StateVariable, 2500.0f, 0.3f,  0.4f },
        /* Electro        */ { W::Square,   12.0f, 0.001f, 0.08f, 0.4f,  0.1f,  F::MoogLadder,    1800.0f, 0.5f,  0.7f, 0.25f },
        /* Acid           */ { W::Saw,      0.0f,  0.001f, 0.18f, 0.0f,  0.05f, F::MoogLadder,    900.0f,  0.85f, 0.9f },
        /* Moog           */ { W::Saw,      4.0f,  0.01f,  0.3f,  0.5f,  0.4f,  F::MoogLadder,    1500.0f, 0.4f,  0.5f },
        /* BladeRunner    */ { W::Sine,     10.0f, 1.5f,   1.0f,  0.7f,  3.0f,  F::StateVariable, 900.0f,  0.15f, 0.15f, 0.0f, 0.5f, 0.0f, 0.25f, 0.7f },
        /* Drive          */ { W::Saw,      9.0f,  0.8f,   0.6f,  0.75f, 1.8f,  F::StateVariable, 1400.0f, 0.2f,  0.25f, 0.0f, 0.6f, 0.0f, 0.25f, 0.5f },
        /* Halloween      */ { W::Sine,     0.0f,  0.001f, 0.3f,  0.0f,  0.1f,  F::StateVariable, 1600.0f, 0.1f,  0.3f },
        /* ClockworkOrange*/ { W::Saw,      2.0f,  0.001f, 0.15f, 0.3f,  0.1f,  F::MoogLadder,    2200.0f, 0.3f,  0.6f },
        /* Terminator     */ { W::Square,   15.0f, 0.01f,  0.4f,  0.6f,  0.5f,  F::MoogLadder,    500.0f,  0.6f,  0.4f,  0.4f },
        /* Interstellar   */ { W::Sine,     4.0f,  2.0f,   1.5f,  0.9f,  3.5f,  F::StateVariable, 700.0f,  0.05f, 0.1f,  0.0f, 0.3f, 0.0f, 0.25f, 0.8f },
        /* Suspiria       */ { W::Triangle, 20.0f, 0.5f,   0.8f,  0.6f,  1.5f,  F::MoogLadder,    800.0f,  0.7f,  0.6f,  0.15f },
        /* Predator       */ { W::Square,   6.0f,  0.02f,  0.5f,  0.5f,  0.6f,  F::MoogLadder,    400.0f,  0.5f,  0.3f },
        /* Dune           */ { W::Saw,      8.0f,  1.0f,   1.2f,  0.8f,  2.5f,  F::MoogLadder,    300.0f,  0.3f,  0.2f,  0.2f, 0.0f, 0.0f, 0.25f, 0.4f },
        /* Arrival        */ { W::Saw,      25.0f, 1.8f,   1.0f,  0.7f,  2.0f,  F::StateVariable, 1000.0f, 0.4f,  0.3f,  0.0f, 0.0f, 0.0f, 0.25f, 0.6f },
        /* MrRobot        */ { W::Square,   0.0f,  0.001f, 0.2f,  0.4f,  0.1f,  F::MoogLadder,    1100.0f, 0.55f, 0.5f },
        /* TwinPeaks      */ { W::Sine,     7.0f,  0.6f,   0.9f,  0.65f, 2.2f,  F::StateVariable, 1300.0f, 0.1f,  0.2f,  0.0f, 0.5f, 0.0f, 0.25f, 0.6f },
        /* Sicario        */ { W::Saw,      3.0f,  0.8f,   1.0f,  0.75f, 1.8f,  F::MoogLadder,    250.0f,  0.35f, 0.15f, 0.25f },
        /* AkiraPulse     */ { W::Square,   0.0f,  0.001f, 0.08f, 0.0f,  0.05f, F::MoogLadder,    1800.0f, 0.45f, 0.8f,  0.3f },
        /* NightDriver    */ { W::Saw,      6.0f,  0.01f,  0.3f,  0.6f,  0.4f,  F::StateVariable, 1900.0f, 0.25f, 0.4f,  0.0f, 0.4f, 0.3f, 0.375f, 0.0f },
        /* Hackers        */ { W::Square,   12.0f, 0.005f, 0.15f, 0.5f,  0.2f,  F::MoogLadder,    1500.0f, 0.65f, 0.6f,  0.35f },
        /* Moroder        */ { W::Saw,      10.0f, 0.004f, 0.15f, 0.3f,  0.08f, F::MoogLadder,    1200.0f, 0.5f,  0.75f, 0.15f, 0.35f, 0.35f, 0.125f, 0.3f },
        /* BackToTheFuture*/ { W::Saw,      8.0f,  0.03f,  0.3f,  0.65f, 0.6f,  F::StateVariable, 2200.0f, 0.25f, 0.4f,  0.0f, 0.3f, 0.15f, 0.3f,  0.5f },
        /* WaynesWorld    */ { W::Square,   5.0f,  0.008f, 0.15f, 0.55f, 0.2f,  F::MoogLadder,    2000.0f, 0.4f,  0.5f,  0.35f, 0.15f },
    };
    int index = (int) type;
    jassert (index >= 0 && index < (int) std::size (presets));
    return presets[juce::jlimit (0, (int) std::size (presets) - 1, index)];
}

float SynthEngine::Voice::nextOscSample (Waveform wave, double sampleRate)
{
    float sample = 0.0f;
    switch (wave) {
        case Waveform::Sine:
            sample = (float) std::sin (phase * juce::MathConstants<double>::twoPi);
            break;
        case Waveform::Saw:
            sample = (float) (2.0 * phase - 1.0);
            break;
        case Waveform::Square:
            sample = phase < 0.5 ? 1.0f : -1.0f;
            break;
        case Waveform::Triangle:
            sample = (float) (4.0 * std::abs (phase - 0.5) - 1.0);
            break;
        case Waveform::Noise:
            sample = noiseRng.nextFloat() * 2.0f - 1.0f;
            break;
    }
    phase += phaseIncrement;
    if (phase >= 1.0) phase -= 1.0;
    juce::ignoreUnused (sampleRate);
    return sample;
}

SynthEngine::SynthEngine()
{
    applyType (SynthType::Pluck);
}

void SynthEngine::prepare (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };

    for (auto& v : voices) {
        v.adsr.setSampleRate (sampleRate);
        v.svf.prepare (spec);
        v.svf.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        v.ladder.reset();
    }

    laserSfx.svf.prepare (spec);
    laserSfx.svf.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    laserSfx.highCut.prepare (spec);
    laserSfx.highCut.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    laserSfx.highCut.setCutoffFrequency (LaserSfx::highCutHz);
    laserSfx.highCut.setResonance (0.5f);        // flat — this is tone shaping, not a voice
    laserSfx.ladder.reset();
    explosionSfx.svf.prepare (spec);
    explosionSfx.svf.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    explosionSfx.ladder.reset();

    chorus.prepare (spec);
    reverb.prepare (spec);
    delayLine.prepare (spec);
    delayLine.setMaximumDelayInSamples ((int) (sampleRate * 2.0));

    compressor.prepare (spec);
    compressor.setThreshold (-18.0f);
    compressor.setRatio (4.0f);
    compressor.setAttack (5.0f);
    compressor.setRelease (80.0f);

    // 30ms is short enough to feel instant on a knob turn but long enough
    // (many blocks, even at this app's small buffer sizes) to smooth away
    // the click/zipper a same-block jump would otherwise cause. Delay time
    // gets a little longer — its jump is the most audible of the four, and
    // a touch more glide there is inaudible as anything but "smooth".
    smoothedMasterVolume01.reset (sampleRate, 0.03);
    smoothedDistortionAmount01.reset (sampleRate, 0.03);
    smoothedDelayAmount01.reset (sampleRate, 0.03);
    smoothedDelayTimeSeconds.reset (sampleRate, 0.05);
    smoothedMasterVolume01.setCurrentAndTargetValue (masterVolume01);
    smoothedDistortionAmount01.setCurrentAndTargetValue (distortionAmount01);
    smoothedDelayAmount01.setCurrentAndTargetValue (delayAmount01);
    smoothedDelayTimeSeconds.setCurrentAndTargetValue (delayTimeSeconds);
}

void SynthEngine::reset()
{
    for (auto& v : voices) {
        v.active = false;
        v.midiNote = -1;
        v.phase = 0.0;
        v.adsr.reset();
        v.svf.reset();
        v.ladder.reset();
        v.glideSamplesRemaining = 0;
    }
    // Nothing to glide from after a reset — see lastNoteFrequencyHz.
    lastNoteFrequencyHz = 0.0;
    chorus.reset();
    reverb.reset();
    delayLine.reset();
    compressor.reset();
    laserSfx.active = false;
    explosionSfx.active = false;
}

void SynthEngine::triggerLaser (float startFrequencyHz)
{
    laserSfx.active = true;
    laserSfx.t = 0.0;
    laserSfx.phase = 0.0;
    laserSfx.detunedPhase = 0.0;
    laserSfx.startFrequencyHz = juce::jlimit (80.0, 8000.0, (double) startFrequencyHz);
    laserSfx.ladder.reset();
    laserSfx.svf.reset();
    laserSfx.highCut.reset();
}

void SynthEngine::triggerExplosion (float intensity01)
{
    explosionSfx.intensity = juce::jlimit (0.15f, 1.0f, intensity01);
    explosionSfx.active = true;
    explosionSfx.t = 0.0;
    explosionSfx.crackFilterState = 0.0f;
    explosionSfx.rumbleFilterState = 0.0f;
    explosionSfx.thumpPhase = 0.0;
    explosionSfx.ladder.reset();
    explosionSfx.svf.reset();
    // A genuinely big, cinematic boom — long enough for the low rumble to
    // ring all the way out, longer still for a bigger hit.
    explosionSfx.duration = 0.7 + 0.9 * (double) explosionSfx.intensity;
}

SynthEngine::Voice* SynthEngine::findVoiceToRetrigger (int midiNote)
{
    // Prefer an already-inactive voice; otherwise steal whichever voice is
    // currently quietest (closest to silence), which reads as far less
    // jarring than always stealing voice 0 or the oldest one.
    for (auto& v : voices)
        if (! v.active) return &v;

    Voice* quietest = &voices[0];
    float quietestLevel = 1.0e9f;
    for (auto& v : voices) {
        float level = v.adsr.getNextSample();   // peek-ish; harmless extra sample, envelopes are smooth
        if (level < quietestLevel) { quietestLevel = level; quietest = &v; }
    }
    juce::ignoreUnused (midiNote);
    return quietest;
}

void SynthEngine::noteOn (int midiNote, float velocity01)
{
    Voice* v = findVoiceToRetrigger (midiNote);
    v->active = true;
    v->midiNote = midiNote;
    v->velocity = juce::jlimit (0.0f, 1.0f, velocity01);
    v->phase = 0.0;

    double freq = 440.0 * std::pow (2.0, (midiNote - 69) / 12.0);
    // A touch of unison-style detune applied as a phase-increment offset
    // rather than a second oscillator (keeps this at one oscillator per
    // voice, i.e. cheap), still audible as the classic "wider" synth sound.
    double detuneRatio = std::pow (2.0, (unisonDetuneCents / 100.0) / 12.0);
    const double targetIncrement = (freq * detuneRatio) / currentSampleRate;

    // Glide: start this voice at the PREVIOUS note's pitch and ramp to its
    // own, so consecutive notes slide into each other. Straight to pitch when
    // glide is off, or when nothing has sounded yet — a first note swooping up
    // out of silence from an arbitrary starting pitch is just a glitch.
    // The repeated-note case matters here: the sequencer retriggers the same
    // pitch constantly, and without this guard each of those would run a
    // full-length ramp of exp() calls to slide from a note to itself.
    const int glideSamples = (int) (glideSeconds * currentSampleRate);
    const bool pitchChanged = std::abs (lastNoteFrequencyHz - freq) > 0.01;
    if (glideSamples > 0 && lastNoteFrequencyHz > 0.0 && pitchChanged) {
        v->phaseIncrement = (lastNoteFrequencyHz * detuneRatio) / currentSampleRate;
        v->logInc = std::log (v->phaseIncrement);
        v->logIncTarget = std::log (targetIncrement);
        v->logIncStep = (v->logIncTarget - v->logInc) / (double) glideSamples;
        v->glideSamplesRemaining = glideSamples;
    } else {
        v->phaseIncrement = targetIncrement;
        v->glideSamplesRemaining = 0;
    }
    lastNoteFrequencyHz = freq;

    v->adsr.setParameters (adsrParams);
    v->adsr.noteOn();
    v->svf.reset();
    v->ladder.reset();
}

void SynthEngine::noteOff (int midiNote)
{
    for (auto& v : voices)
        if (v.active && v.midiNote == midiNote)
            v.adsr.noteOff();
}

void SynthEngine::applyType (SynthType type)
{
    const auto& p = getTypePreset (type);
    waveform = p.waveform;
    unisonDetuneCents = p.unisonDetuneCents;
    adsrParams = { p.attack, p.decay, p.sustain, p.release };
    filterType = p.filterType;
    filterCutoffHz = p.filterCutoffHz;
    filterResonance01 = p.filterResonance01;
    filterEnvAmount01 = p.filterEnvAmount01;
    chorusAmount01 = p.chorusAmount01;
    reverbAmount01 = p.reverbAmount01;
    // Through the setters (not direct assignment) so a Type switch also
    // ramps smoothly rather than jumping — same reasoning as a live knob
    // move, just triggered from a preset instead.
    setDistortionAmount01 (p.distortionAmount01);
    setDelayAmount01 (p.delayAmount01);
    setDelayTimeSeconds (p.delayTimeSeconds);
}

void SynthEngine::setWaveform (Waveform w) { waveform = w; }
void SynthEngine::setUnisonDetuneCents (float cents) { unisonDetuneCents = cents; }
void SynthEngine::setAttack (float seconds) { adsrParams.attack = seconds; }
void SynthEngine::setDecay (float seconds) { adsrParams.decay = seconds; }
void SynthEngine::setSustain (float level01) { adsrParams.sustain = level01; }
void SynthEngine::setRelease (float seconds) { adsrParams.release = seconds; }
void SynthEngine::setFilterType (FilterType t) { filterType = t; }
void SynthEngine::setFilterCutoffHz (float hz) { filterCutoffHz = hz; }
void SynthEngine::setFilterResonance01 (float res) { filterResonance01 = juce::jlimit (0.0f, 1.0f, res); }
void SynthEngine::setFilterEnvAmount01 (float amount) { filterEnvAmount01 = juce::jlimit (0.0f, 1.0f, amount); }
void SynthEngine::setMasterVolume01 (float v) {
    masterVolume01 = juce::jlimit (0.0f, 1.0f, v);
    smoothedMasterVolume01.setTargetValue (masterVolume01);
}
void SynthEngine::setGlideSeconds (float seconds) {
    glideSeconds = juce::jlimit (0.0f, 2.0f, seconds);
}
void SynthEngine::setDistortionAmount01 (float amt) {
    distortionAmount01 = juce::jlimit (0.0f, 1.0f, amt);
    smoothedDistortionAmount01.setTargetValue (distortionAmount01);
}
void SynthEngine::setChorusAmount01 (float amt) { chorusAmount01 = juce::jlimit (0.0f, 1.0f, amt); }
void SynthEngine::setDelayAmount01 (float amt) {
    delayAmount01 = juce::jlimit (0.0f, 1.0f, amt);
    smoothedDelayAmount01.setTargetValue (delayAmount01);
}
void SynthEngine::setDelayTimeSeconds (float t) {
    delayTimeSeconds = t;
    smoothedDelayTimeSeconds.setTargetValue (t);
}
void SynthEngine::setReverbAmount01 (float amt) { reverbAmount01 = juce::jlimit (0.0f, 1.0f, amt); }

float SynthEngine::applySharedFilter (float input, LadderFilter& ladder, juce::dsp::StateVariableTPTFilter<float>& svf)
{
    if (filterType == FilterType::MoogLadder) {
        float g = juce::jlimit (0.01f, 0.99f, filterCutoffHz / (float) (currentSampleRate * 0.5));
        return ladder.process (input, g, filterResonance01);
    }
    svf.setCutoffFrequency (filterCutoffHz);
    svf.setResonance (0.1f + filterResonance01 * 0.9f);
    return svf.processSample (0, input);
}

void SynthEngine::renderNextBlock (juce::AudioBuffer<float>& outBuffer, int numSamples)
{
    outBuffer.clear();

    for (auto& v : voices) {
        if (! v.active) continue;

        for (int i = 0; i < numSamples; ++i) {
            // Advance the glide, if this voice is still on its way. Costs
            // nothing once it has arrived, which is almost always.
            if (v.glideSamplesRemaining > 0) {
                v.logInc += v.logIncStep;
                if (--v.glideSamplesRemaining == 0) v.logInc = v.logIncTarget;
                v.phaseIncrement = std::exp (v.logInc);
            }

            float osc = v.nextOscSample (waveform, currentSampleRate);
            float env = v.adsr.getNextSample();

            // Cutoff is modulated by the envelope so a note's timbre opens
            // and closes with its own volume envelope, rather than a
            // constant filter colour regardless of how loud/decayed the
            // note currently is — the single most important thing that
            // makes a subtractive synth actually sound alive.
            float modulatedCutoff = juce::jlimit (20.0f, 18000.0f,
                                                  filterCutoffHz * (1.0f + filterEnvAmount01 * env * 3.0f));

            float filtered;
            if (filterType == FilterType::MoogLadder) {
                float g = juce::jlimit (0.01f, 0.99f, modulatedCutoff / (float) (currentSampleRate * 0.5));
                filtered = v.ladder.process (osc, g, filterResonance01);
            } else {
                v.svf.setCutoffFrequency (modulatedCutoff);
                v.svf.setResonance (0.1f + filterResonance01 * 0.9f);
                filtered = v.svf.processSample (0, osc);
            }

            float sampleOut = filtered * env * v.velocity * 0.25f;   // 0.25: headroom across up to 8 voices
            for (int ch = 0; ch < outBuffer.getNumChannels(); ++ch)
                outBuffer.addSample (ch, i, sampleOut);
        }

        if (! v.adsr.isActive())
            v.active = false;
    }

    // --- Invader-mode one-shot SFX — mixed in alongside the voices, ahead
    // of the effects chain below, so they pick up the current preset's
    // reverb/delay/chorus rather than sounding pasted on top. ---
    if (laserSfx.active) {
        for (int i = 0; i < numSamples; ++i) {
            double tNorm = laserSfx.t / LaserSfx::duration;
            if (tNorm >= 1.0) { laserSfx.active = false; break; }

            // A fast downward pitch sweep — ~20x down from wherever it
            // started (that ratio is what gives the "pew" its shape; the
            // start itself is a random note from the current scale, picked
            // in PluginProcessor::fireBullet()) — with a fast vibrato
            // riding on top for that "electric" sci-fi wobble rather than
            // a bare clean sweep.
            double vibrato = 1.0 + 0.03 * std::sin (laserSfx.t * juce::MathConstants<double>::twoPi * 45.0);
            double freq = laserSfx.startFrequencyHz * std::pow (0.05, tNorm) * vibrato;

            // A second, slightly-detuned voice stacked on top for width —
            // one bare square reads as a thin retro blip; two, a real zap.
            laserSfx.phase += freq / currentSampleRate;
            if (laserSfx.phase >= 1.0) laserSfx.phase -= 1.0;
            laserSfx.detunedPhase += (freq * 1.015) / currentSampleRate;
            if (laserSfx.detunedPhase >= 1.0) laserSfx.detunedPhase -= 1.0;

            float amp = (float) std::exp (-tNorm * 5.0) * 0.32f;
            float osc1 = (laserSfx.phase < 0.5 ? 1.0f : -1.0f);
            float osc2 = (laserSfx.detunedPhase < 0.5 ? 1.0f : -1.0f);
            float sample = (osc1 + osc2 * 0.6f) * amp;

            // A tiny noise click right at the onset — the percussive
            // "spark" of the shot firing — gone within ~2ms.
            if (tNorm < 0.02) {
                float click = (laserSfx.rng.nextFloat() * 2.0f - 1.0f) * (float) (1.0 - tNorm / 0.02) * 0.25f;
                sample += click;
            }

            sample = std::tanh (sample * 1.6f) * 0.7f;   // brighter, punchier edge

            // Through the same Filter Type/Cutoff/Resonance the regular
            // voices use — the Filter knobs used to have no audible effect
            // on ship SFX at all, since this synthesis path never touched
            // any filter before.
            sample = applySharedFilter (sample, laserSfx.ladder, laserSfx.svf);

            // Then the always-on high cut (see LaserSfx::highCutHz) — after
            // the shared filter, so no preset can open the cutoff back up and
            // bring the shrillness with it.
            sample = laserSfx.highCut.processSample (0, sample);

            for (int ch = 0; ch < outBuffer.getNumChannels(); ++ch)
                outBuffer.addSample (ch, i, sample);
            laserSfx.t += 1.0 / currentSampleRate;
        }
    }

    if (explosionSfx.active) {
        for (int i = 0; i < numSamples; ++i) {
            double tNorm = explosionSfx.t / explosionSfx.duration;
            if (tNorm >= 1.0) { explosionSfx.active = false; break; }

            float noise = explosionSfx.rng.nextFloat() * 2.0f - 1.0f;

            // Crack: a bright, fast-decaying burst — the sharp initial
            // impact, over within roughly the first tenth of the effect.
            float crackCutoff = juce::jlimit (0.05f, 0.9f, 0.9f * (float) std::exp (-tNorm * 18.0));
            explosionSfx.crackFilterState += crackCutoff * (noise - explosionSfx.crackFilterState);
            float crackAmp = (float) std::exp (-tNorm * 14.0);

            // Rumble: a slow, heavily-filtered tail ringing out for the
            // rest of the (now much longer) duration — the actual "boom"
            // that makes this read as big rather than a firecracker snap.
            float rumbleCutoff = juce::jlimit (0.01f, 0.12f, 0.12f * (float) std::exp (-tNorm * 1.2));
            explosionSfx.rumbleFilterState += rumbleCutoff * (noise - explosionSfx.rumbleFilterState);
            float rumbleAmp = (float) std::exp (-tNorm * 2.2);

            // Thump: a short sub-bass hit right at the start, for weight —
            // the part of a big explosion you feel as much as hear.
            double thumpFreq = 65.0 * std::exp (-tNorm * 10.0) + 35.0;
            explosionSfx.thumpPhase += thumpFreq / currentSampleRate;
            if (explosionSfx.thumpPhase >= 1.0) explosionSfx.thumpPhase -= 1.0;
            float thumpAmp = (float) std::exp (-tNorm * 16.0);
            float thump = (float) std::sin (explosionSfx.thumpPhase * juce::MathConstants<double>::twoPi) * thumpAmp;

            float sample = (explosionSfx.crackFilterState * crackAmp * 0.5f
                           + explosionSfx.rumbleFilterState * rumbleAmp * 0.8f
                           + thump * 0.6f) * explosionSfx.intensity;
            // Soft clip — the layered sum can otherwise clip hard right at
            // the initial crack+thump transient.
            sample = std::tanh (sample * 1.3f) * 0.8f;

            // Through the same Filter Type/Cutoff/Resonance the regular
            // voices use, same as the laser above — on top of (not instead
            // of) the crack/rumble layers' own internal shaping, which is
            // sound-design character, not the shared Filter knob.
            sample = applySharedFilter (sample, explosionSfx.ladder, explosionSfx.svf);

            for (int ch = 0; ch < outBuffer.getNumChannels(); ++ch)
                outBuffer.addSample (ch, i, sample);
            explosionSfx.t += 1.0 / currentSampleRate;
        }
    }

    // --- Master effects chain: distortion -> chorus -> delay -> reverb ---
    juce::dsp::AudioBlock<float> block (outBuffer);

    // Sample-outer, channel-inner below (not the reverse) so each smoothed
    // parameter's getNextValue() is called exactly once per real sample —
    // calling it once per (channel, sample) pair would advance a stereo
    // signal's ramp twice as fast as intended.
    if (distortionAmount01 > 0.001f || smoothedDistortionAmount01.isSmoothing()) {
        int numCh = outBuffer.getNumChannels();
        for (int i = 0; i < numSamples; ++i) {
            float amt = smoothedDistortionAmount01.getNextValue();
            float drive = 1.0f + amt * 9.0f;
            float tanhDrive = std::tanh (drive);
            for (int ch = 0; ch < numCh; ++ch) {
                auto* data = outBuffer.getWritePointer (ch);
                data[i] = std::tanh (data[i] * drive) / tanhDrive;
            }
        }
    }

    if (chorusAmount01 > 0.001f) {
        chorus.setMix (chorusAmount01);
        juce::dsp::ProcessContextReplacing<float> chorusContext (block);
        chorus.process (chorusContext);
    }

    if (delayAmount01 > 0.001f || smoothedDelayAmount01.isSmoothing() || smoothedDelayTimeSeconds.isSmoothing()) {
        int numCh = outBuffer.getNumChannels();
        for (int i = 0; i < numSamples; ++i) {
            float amt = smoothedDelayAmount01.getNextValue();
            float timeSeconds = smoothedDelayTimeSeconds.getNextValue();
            float delaySamples = timeSeconds * (float) currentSampleRate;
            for (int ch = 0; ch < numCh; ++ch) {
                auto* data = outBuffer.getWritePointer (ch);
                float in = data[i];
                float wet = delayLine.popSample (ch, delaySamples);
                delayLine.pushSample (ch, in + wet * 0.35f);   // feedback
                data[i] = in + wet * amt;
            }
        }
    }

    if (reverbAmount01 > 0.001f && outBuffer.getNumChannels() >= 2) {
        juce::Reverb::Parameters params;
        params.roomSize = 0.6f;
        params.wetLevel = reverbAmount01;
        params.dryLevel = 1.0f - reverbAmount01 * 0.5f;
        params.damping = 0.5f;
        reverb.setParameters (params);
        juce::dsp::ProcessContextReplacing<float> context (block);
        reverb.process (context);
    }

    for (int i = 0; i < numSamples; ++i) {
        float g = smoothedMasterVolume01.getNextValue();
        for (int ch = 0; ch < outBuffer.getNumChannels(); ++ch)
            outBuffer.getWritePointer (ch)[i] *= g;
    }

    // Final stage, after everything else including the volume knob: squash
    // the varying loudness of different presets/notes down to one
    // consistent level, then make up the gain the compressor took away so
    // the result is actually audible rather than just quieter-but-even.
    // A tanh soft-clip catches anything the makeup gain pushes over 0dBFS.
    juce::dsp::ProcessContextReplacing<float> compressorContext (block);
    compressor.process (compressorContext);
    outBuffer.applyGain (2.8f);   // ~+9dB makeup gain
    for (int ch = 0; ch < outBuffer.getNumChannels(); ++ch) {
        auto* data = outBuffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            data[i] = std::tanh (data[i]);
    }
}
