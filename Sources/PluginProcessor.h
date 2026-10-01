#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_data_structures/juce_data_structures.h>
#include <opencv2/opencv.hpp>
#include <thread>
#include <memory>
#include <vector>
#include "SkyMapRenderer.h"
#include "SynthEngine.h"

enum class MusicalMode {
    Lydian = 0, MajorIonian, MinorAeolian, Dorian, Mixolydian,
    MajorPentatonic, MinorPentatonic, Chromatic, Locrian, Phrygian,
    HarmonicMinor, MelodicMinor, WholeTone, BluesScale, DoubleHarmonic,
    Enigmatic, HungarianMinor, NeapolitanMajor, NeapolitanMinor,
    Prometheus, TritoneScale, InSen, Hirajoshi, Iwato, Kumoi, Pelog,
    SpanishGypsy, Balinese, Byzantine, OvertoneScale, AlteredScale,
    LydianAugmented, LydianDominant, Locrian6th, SuperLocrianBb7
};

// What's drawn over the currently-playing note's position (see run()'s
// ember-circle overlay and SkyMapRenderer-style procedural drawing in
// PluginProcessor.cpp) — a plain ring by default, or a small stylised
// icon of the Sun, Moon, or one of the nine planets.
enum class OccultingObjectType {
    Circle = 0,
    Sun, Moon,
    Mercury, Venus, Earth, Mars, Jupiter, Saturn, Uranus, Neptune, Pluto
};

// Owns the camera/video capture thread and the whole vision-to-MIDI engine,
// so it keeps running for as long as the plugin instance exists — a host
// closing the editor window must not stop MIDI generation, and previously
// did, because all of this lived on a thread the *editor* started and
// stopped. The editor is now just a view onto this state.
class VisionMidiProcessor : public juce::AudioProcessor,
                            private juce::Thread
{
public:
    // Source ids that aren't a camera index: the procedural star chart, and
    // "whatever file/stream lastFilePath points at". Cameras are 1..n.
    static constexpr int skyMapSourceId = 997;
    static constexpr int customPathSourceId = 999;

    // Bundled sample astrophotographs, offered straight from the Source menu.
    // Their sourceIds run from firstSampleImageSourceId upwards, in the order
    // sampleImages() lists them - picked well clear of the camera indices
    // (2-4) and of the 997/999/1000/1001 specials.
    static constexpr int firstSampleImageSourceId = 900;
    struct SampleImage { const char* label; const char* data; int size; };
    static const std::vector<SampleImage>& sampleImages();

    VisionMidiProcessor();
    ~VisionMidiProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void addMidiMessage (uint8_t note, uint8_t velocity, bool isNoteOn);

    // --- Audio synth engine (see SynthEngine.h) ---
    // Occultation's image-driven note SELECTION is unchanged — this only
    // decides what those notes sound like, run through the AudioProcessorValueTreeState
    // parameters below so the host can automate/save them and the editor's
    // knobs have something standard to attach to.
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts { *this, nullptr, "PARAMETERS", createParameterLayout() };

    // --- Source selection ---
    // Driven by the editor's UI, but persisted and acted on here so capture
    // keeps running regardless of whether the editor exists.
    void adoptSource (const juce::String& path);   // a local file path or a stream URL
    void selectBuiltInSource (int sourceId);        // "Source..." placeholder / Cam 0-2
    juce::String getLastFilePath() const;
    int getLastSourceId() const;

    // Clears the rolling piano-roll history and rewinds PPQ tracking. Called
    // from the editor's Play/Stop control and directly from an incoming MIDI
    // Stop in processBlock, so it works whether or not the UI is open.
    void resetPlaybackState();

    // --- Sky map controls ---
    void panSkyMap (float dRaDegrees, float dDecDegrees);  // Pan by delta
    void setSkyMapZoom (float zoomDegPerPixel);            // Set zoom level
    void setSkyMapView (float raDegrees, float decDegrees, float zoomDegPerPixel);  // Set exact view

    // Defaults to Cassiopeia centred (gamma Cas, RA 0.9451h/Dec 60.7167deg)
    // rather than RA 0/Dec 0, so the sky map opens on a recognisable,
    // labelled constellation instead of an arbitrary equatorial point.
    std::atomic<float> skyMapRaDegrees { 0.9451f * 15.0f };
    std::atomic<float> skyMapDecDegrees { 60.7167f };
    // Degrees of sky per rendered pixel. The frame is rendered 1920 wide, so
    // this opens on roughly a 96-degree field — wide enough to hold whole
    // constellations, where the old 1.0 default spanned 1920 degrees and
    // crushed the entire sky into a knot in the middle of the frame.
    std::atomic<float> skyMapZoomDegPerPixel { 0.05f };

    static constexpr int skyMapFrameWidth  = 1920;
    static constexpr int skyMapFrameHeight = 1080;

    // --- Pixel-rectangle zoom/pan (all modes, sky map included) ---
    // Crops whatever frame the active source produced to a rectangle
    // centred at (viewCenterX, viewCenterY) — normalized 0..1 of that
    // frame — sized to 1/viewZoom of its width/height. Sequencer/detection
    // sample this cropped view (see run()), and it's exactly what's
    // displayed, so what triggers a note always matches what's on screen.
    void setViewZoom (float zoom);                       // absolute, clamped to [1, 8]
    void panView (float dNormalizedX, float dNormalizedY);  // pan by a delta in normalized frame units
    void rotateViewBy (float deltaDegrees);               // relative, wraps to (-180, 180]

    std::atomic<float> viewZoom { 1.0f };
    std::atomic<float> viewCenterX { 0.5f };
    std::atomic<float> viewCenterY { 0.5f };
    std::atomic<float> viewRotationDegrees { 0.0f };

    // --- Synth presets ---
    // The APVTS holds exactly the synth's own parameters (type, waveform,
    // ADSR, filter, effects, volume) and nothing source- or sequencer-
    // related, so a preset is simply its state serialised to an XML file
    // per preset in getPresetDirectory().
    juce::File getPresetDirectory() const;
    juce::StringArray getPresetNames() const;       // user presets, from disk
    bool savePreset (const juce::String& name);
    bool loadPreset (const juce::String& name);
    bool deletePreset (const juce::String& name);
    juce::String getCurrentPresetName() const { return currentPresetName; }
    static constexpr const char* presetFileExtension = ".occpreset";

    // The built-in voices (what used to be the "Synth Type" dropdown) are
    // exposed as read-only factory presets instead: same table, same names,
    // but reached through the one preset mechanism rather than a parallel
    // control that fought it — selecting a Type used to stamp its whole
    // parameter set over anything a preset had just loaded.
    juce::StringArray getFactoryPresetNames() const;
    bool isFactoryPreset (const juce::String& name) const;
    juce::StringArray getAllPresetNames() const;    // factory first, then user

    // --- Invader mode: a small ship, thrust-controlled by the editor's
    // arrow keys, that flies over whatever source is currently showing and
    // fires bullets at bright pixels/stars. An alternative way to play
    // with the same source/threshold pipeline the sequencer already uses,
    // not a source of its own — run() keeps ticking the sequencer
    // alongside it. Ship state (and the methods below, called from the
    // editor's message thread) has to be processor state rather than
    // editor state so run() — which owns frame drawing regardless of
    // whether an editor is even open — can simulate/draw/collide against it.
    void setInvaderModeEnabled (bool enabled);
    // Streak modulation, behind its own checkbox (shown only while Invader
    // mode is on): consecutive hits with no miss transpose the invader
    // layer up the scale a degree at a time, and long streaks swap it onto
    // a more exotic mode entirely. Opt-in because it deliberately moves the
    // notes away from the Scale/Root the user picked — for anyone using
    // Invader mode as a controlled musical layer rather than a game, that's
    // the last thing they want. Only the invader layer moves: the
    // sequencer's own notes always stay on the selected scale, so the
    // streak reads as a counter-melody rising against a fixed backing
    // rather than the whole piece lurching key.
    std::atomic<bool> invaderStreakModulationEnabled { false };
    // Consecutive hits without a miss. Published for the editor's label;
    // only ever written from run().
    std::atomic<int> invaderStreak { 0 };
    // Both -1..1, polled every editor timer tick from held-arrow-key state
    // (not a discrete keyPress), so holding a key steers or throttles
    // continuously instead of stepping once per OS key-repeat.
    //
    // `steer` (left/right) turns the ship; `throttle` (up/down) raises and
    // lowers how fast it's trying to go. The arrows used to push the ship
    // along the screen's own axes, which meant the direction it faced was
    // just wherever its velocity happened to point — pressing left while
    // travelling right swung it round bodily, and there was no way to aim
    // without also moving. Steering and throttle are separate controls now:
    // you can turn on the spot to line up a shot, and hold a speed through a
    // curve to graze a line of stars.
    void setShipControls (float steer, float throttle);
    void fireBullet();

    std::atomic<bool> invaderModeEnabled { false };
    std::atomic<float> shipX { 0.5f };
    std::atomic<float> shipY { 0.85f };
    std::atomic<float> shipVelX { 0.0f };
    std::atomic<float> shipVelY { 0.0f };
    std::atomic<float> shipSteer { 0.0f };      // -1 left .. +1 right
    std::atomic<float> shipThrottle { 0.0f };   // -1 slow down .. +1 speed up
    // How fast the ship is trying to travel along its facing. Held when no
    // key is pressed rather than decaying to zero — up and down set a cruising
    // speed, so one hand can stay on the steering while the ship keeps moving.
    std::atomic<float> shipTargetSpeed { 0.0f };

    struct NoteEvent {
        int noteNumber;
        double triggerPpq; // Continuous PPQ position when triggered
        double releasePpq; // Continuous PPQ position when note released (-1 if active)
        juce::Colour colour;
    };

    // Standalone playback simulation state
    std::atomic<bool> standalonePlaying { false };
    std::atomic<double> standalonePpqPosition { 0.0 };
    std::atomic<int> activeNoteCount { 0 };

    // The host transport, sampled in processBlock() and read from run().
    // getPlayHead() is only valid to call on the audio thread — run() used to
    // call it directly from its own background thread, which the VST3 wrapper
    // tolerated but the AU wrapper does not: AU answers position queries via
    // host callbacks that only work inside the render callback, so off-thread
    // they just fail. isHostPlaying then never became true under AU, the
    // sequencer never advanced, and nothing ever sounded (reproduced in
    // Ableton Live). Sampling here instead makes both formats behave the same.
    std::atomic<bool> hostIsPlaying { false };
    std::atomic<double> hostPpqPosition { 0.0 };

    // Generation parameters, set by the editor's controls; read continuously
    // by the capture thread regardless of whether an editor exists.
    std::atomic<int> targetSourceId { -1 };
    // Set by adoptSource() when the custom path changes while staying on
    // sourceId 999 (a new file/stream loaded without the id itself
    // changing) — consumed by run(), which can't otherwise tell that case
    // apart from "nothing changed" just from targetSourceId alone.
    std::atomic<bool> pendingSourceChange { false };
    std::atomic<OccultingObjectType> occultingObject { OccultingObjectType::Circle };
    std::atomic<int> currentScale { 0 };
    std::atomic<int> currentRootNote { 48 };
    std::atomic<float> detectionThreshold { 128.0f };
    std::atomic<bool> isAutoThresholdEnabled { true };

    // State the editor renders from, when it exists — all owned and updated
    // here so it reflects reality immediately whenever the editor (re)opens.
    std::atomic<int> lastPlayedNote { -1 };
    // Velocity of whatever triggered lastPlayedNote — already unifies pixel
    // brightness (video/image modes) and star magnitude (sky map) onto one
    // 0-127 scale, so the ember-circle overlay in run() can size itself by
    // it without caring which mode is active.
    std::atomic<int> lastPlayedVelocity { 100 };
    // Incremented each time a new note triggers with a valid position (see
    // run()) — drives step-dependent Occulting Object appearance: which
    // Moon phase image is shown, and the slow rotation/scroll applied to
    // every other body except Circle/Sun/Earth (Earth instead follows real
    // wall-clock time for its day/night terminator, not this counter).
    std::atomic<int> occultingObjectStep { 0 };
    std::atomic<float> currentMidiLevel { 0.0f };
    std::atomic<float> midiPeakLevel { 0.0f };
    // Same peak-hold-then-decay ballistics as the MIDI meter above, but for
    // SynthEngine's actual rendered audio output — set per-block in
    // processBlock (audio thread), decayed in run()'s ~30Hz loop alongside
    // the MIDI one so both keep ticking whether or not the editor is open.
    std::atomic<float> currentAudioLevel { 0.0f };
    std::atomic<float> audioPeakLevel { 0.0f };
    std::atomic<int> sequenceCursorX { -1 };
    std::atomic<double> continuousPpq { 0.0 };

    juce::CriticalSection imageLock;
    juce::Image juceImage;

    juce::CriticalSection pianoRollLock;
    std::vector<NoteEvent> pianoRollNotes;
    // How many quarter notes the sequencer's cursor sweeps across before
    // repeating — set by the editor's Steps selector (64/32/16/8/4/2/1),
    // 16 by default. The piano roll's visible time window (drawPianoRoll in
    // PluginEditor.cpp) and its note-history trim (recordNoteTrigger below)
    // both follow this too, so the roll always shows exactly one sweep's
    // worth of history regardless of how long that sweep currently is.
    std::atomic<double> sequenceLoopBeats { 16.0 };

    // ================= Object mode =====================================
    // Contour-detected objects lifted out of the picture and played with:
    // thrown, bounced off each other, looped along a path, blinked, tinted.
    //
    // The pixels MOVE. Each object is composited into the source frame before
    // the zoom crop, which means `gray` (what the sequencer samples) and
    // rgbFrame (what you see) both inherit it from one code path, and zoom,
    // pan and rotation keep working with no extra mapping. Throw a star to the
    // top of the frame and the sequencer plays it high; blink it and its note
    // comes and goes. That coupling is the whole point of the mode - an object
    // that moved without changing the music would just be decoration.
    enum class ObjectMotion   { Fixed, Free, Path };
    enum class ObjectPathShape { Drawn, Circle, Ellipse, Figure8 };

    struct SceneObject {
        cv::Mat sprite;              // BGR pixels lifted from the source frame
        cv::Mat mask;                // 8U coverage from the contour, edge-feathered
        cv::Point2f home { 0, 0 };   // normalised source centre it came from
        cv::Point2f pos  { 0, 0 };   // normalised centre right now
        cv::Point2f vel  { 0, 0 };   // normalised units per tick
        float radius = 0.02f;        // normalised collision disc
        float mass   = 1.0f;         // proportional to area, so big blobs shove small ones
        bool  blink  = false;
        float blinkHz = 2.0f;
        double blinkPhase = 0.0;
        bool  visible = true;
        bool  tinted = false;
        cv::Scalar tint { 1.0, 1.0, 1.0 };
        ObjectMotion motion = ObjectMotion::Fixed;
        ObjectPathShape pathShape = ObjectPathShape::Drawn;
        std::vector<cv::Point2f> path;   // normalised, closed loop
        float pathT = 0.0f;              // 0..1 around the path
        float pathSpeed = 1.0f;          // loops per ~4 seconds at 1.0
        bool  everMoved = false;         // once true the home spot stays erased
    };

    std::atomic<bool> objectModeEnabled { false };
    // Set from the editor, consumed on run()'s thread - detection touches the
    // frame and must not happen on the message thread.
    std::atomic<bool> objectDetectPending { false };
    std::atomic<int>  selectedObject { -1 };
    // Collisions and bounces play a note. No longer exposed as a toggle - the
    // mode is pointless silent, and it was the first thing anyone would want
    // left on anyway.
    std::atomic<bool> objectCollisionNotes { true };

    int  objectCount() const;
    // All of these take the objectLock themselves - safe from the message thread.
    int  objectAtViewPoint (float viewX, float viewY) const;
    void throwObject (int index, float velX, float velY);
    void setObjectMotion (int index, ObjectMotion m);
    void setObjectPathShape (int index, ObjectPathShape shape);
    void setObjectDrawnPath (int index, const std::vector<cv::Point2f>& pathNormalised);
    void setObjectPathSpeed (int index, float speed);
    void setObjectBlink (int index, bool on, float hz);
    // Cmd+click toggles blink, so the editor needs to flip it without knowing
    // the current state, and to read back what an object is doing in order to
    // show only the controls that apply to it.
    void toggleObjectBlink (int index);
    struct ObjectState {
        bool valid = false;
        bool blink = false;
        float blinkHz = 2.0f;
        bool tinted = false;
        ObjectMotion motion = ObjectMotion::Fixed;
        ObjectPathShape pathShape = ObjectPathShape::Drawn;
        float pathSpeed = 1.0f;
    };
    ObjectState objectState (int index) const;
    void setObjectTint (int index, bool on, float r, float g, float b);
    // Both take points in VIEW space (0..1 across the video rectangle on
    // screen) and map them back through zoom/pan/rotation themselves, so the
    // editor never has to know the transform.
    void placeObjectAtView (int index, float viewX, float viewY);
    void setObjectDrawnPathFromView (int index, const std::vector<cv::Point2f>& viewPath);
    void resetObject (int index);
    void clearObjects();
    // Normalised centre of an object, for drawing selection UI in the editor.
    bool objectViewPosition (int index, float& viewX, float& viewY, float& viewRadius) const;

private:
    void run() override;

    // Where the currently displayed view sits inside the captured frame, in
    // normalized source coordinates, plus the rotation applied after that
    // crop. run() already computes all of this for the zoom/pan/rotate path;
    // passing it on is what lets the invader's damage (below) be stored
    // against the SOURCE image rather than the screen, so holes stay on the
    // stars that were shot instead of sliding around as the view pans.
    struct ViewTransform {
        float cropX0 = 0.0f, cropY0 = 0.0f, cropW = 1.0f, cropH = 1.0f;
        float rotationDegrees = 0.0f;
    };


    void switchSource (int sourceId);
    // Reads a network stream on its own thread rather than inline in run().
    // A blocking capture>>frame sitting directly in the 30ms MIDI-generation
    // loop meant that loop's whole iteration cadence — not just the frame
    // display — was hostage to however fast (or slow, or stalled) the
    // stream happened to be: the sequencer cursor and note triggers only
    // ever advanced in lockstep with frame arrival, never smoothly on their
    // own. This thread just keeps lastGoodFrame updated; run() reads it
    // without ever calling into OpenCV's networking itself.
    void streamCaptureLoop();
    // Destructor-only: stops streamCaptureLoop() and reclaims its thread,
    // but never blocks longer than timeoutMs doing so — a stalled peer that
    // accepted the TCP connection and then went silent can leave the
    // thread's one blocking call (capture>>frame) stuck in FFmpeg's
    // poll()/recv() with no way to interrupt it from here, regardless of
    // CAP_PROP_READ_TIMEOUT_MSEC (that only bounds OpenCV's own retry loop
    // around av_read_frame, not a single stuck read — confirmed by
    // reproducing an indefinite hang against a dead stale stream URL even
    // with it set). If the thread hasn't finished within timeoutMs, it's
    // detached instead of joined so the app can still quit; the OS reclaims
    // it along with every other thread the moment the process actually
    // exits, which happens very shortly after since this only runs during
    // final teardown. NOT safe to reuse from switchSource() while the app
    // keeps running afterwards — stopStreamThread is one shared flag, so a
    // detached-but-still-blocked old thread that later wakes up would see
    // the flag reset by a freshly-started thread and start colliding with
    // it over the same non-thread-safe `capture` object.
    void stopAndJoinStreamCaptureThread (int timeoutMs);
    // `view` is only needed for the sky-map branch's damage check (see
    // isViewPositionDamaged) — every other source is already sampled from a
    // `gray` that has the damage subtracted out of it before it gets here.
    void processSequenceMode (const cv::Mat& gray, bool isHostPlaying, const ViewTransform& view);
    void recordNoteTrigger (int newNote, double currentPpq, int velocity = 100);
    // Draws whatever occultingObject currently selects, centred at
    // `centre` with `radius` (already zoom/magnitude-scaled by the
    // caller). `ringColour`/`ringThickness` are only used for Circle —
    // every celestial body instead composites a real photograph. `step`
    // (occultingObjectStep, passed in rather than read directly since this
    // is static) spins every body but Circle/Sun/Moon/Earth/Saturn a
    // little on each new note, so the picture isn't perfectly static from
    // one trigger to the next. `loopStepIndex` is which discrete beat of
    // the sequencer's own loop is currently playing (0..loopBeats-1,
    // constant for that whole beat) — the Moon's phase is one fixed
    // position of the cycle per loop step (one full new-to-full-to-new
    // cycle spread evenly across the loop's beats), rather than drifting
    // continuously with time or with the trigger-counted `step`. Static so
    // it has no implicit dependency on processor state beyond what's
    // passed in, keeping it easy to reason about from run().
    static void drawOccultingObject (cv::Mat& rgbFrame, OccultingObjectType type, cv::Point centre,
                                     int radius, const cv::Scalar& ringColour, int ringThickness,
                                     int step, int loopStepIndex, int loopStepCount);
    float computeSmoothedAutoThreshold (const cv::Mat& grayFrame);
    std::vector<int> getScaleNotes (MusicalMode mode, int rootNote, int numNotes);

    // Turns the facing by shipSteer, moves shipTargetSpeed by shipThrottle,
    // and eases shipVelX/Y towards facing * target speed before integrating
    // that into shipX/Y — every run() tick regardless of whether a frame was
    // captured that tick — same reasoning as the meter decay below: the ship should
    // keep drifting smoothly even through a stalled/missing frame, not
    // freeze. No-ops immediately when invaderModeEnabled is false.
    // --- Object mode internals (run() thread unless noted) ---------------
    // Re-detects from scratch: contours above the detection threshold become
    // objects, largest first and capped, so a noisy frame can't produce
    // thousands of them.
    // Detects ONLY within the rectangle currently on screen, not across the
    // whole source frame. Zoomed in, the brightest 48 objects in the full
    // picture can easily all be outside the view, which is what made visible
    // stars unselectable; and an area floor measured against the whole frame
    // rejects a star that looks large on screen. Both go away by working in
    // the view's own rectangle, which is also why the view moving has to
    // trigger a re-detect.
    void detectObjects (const cv::Mat& sourceBgr);
    // Shared tail of both detectors: turns a full-resolution binary region
    // mask into a SceneObject (sprite, feathered mask, centre, radius, mass).
    bool makeObjectFromMask (const cv::Mat& sourceBgr, const cv::Mat& regionMask,
                             double area, SceneObject& out) const;
    void updateObjectPhysics();
    // Composites every object into `sourceBgr` IN PLACE: erases the home
    // patch of anything that has moved or is hidden, then draws each visible
    // sprite at its live position. Called before the zoom crop, so one pass
    // serves both the sequencer and the display.
    void compositeObjects (cv::Mat& sourceBgr);
    // Median of a ring just outside the object, used to fill the hole it
    // leaves behind - a plain black fill reads as a rectangle punched out of
    // a nebula, where the local background reads as sky.
    cv::Scalar backgroundAround (const cv::Mat& sourceBgr, cv::Point centre, int radius) const;
    cv::Point2f pathPoint (const SceneObject& o, float t) const;
    // Undoes the view rotation and zoom crop: a point on screen becomes a
    // point in the source image, which is the space objects live in.
    cv::Point2f viewToSource (float viewX, float viewY) const;
    void objectCollisionNote (const SceneObject& a, float impactSpeed);

    std::vector<SceneObject> sceneObjects;
    // Guards sceneObjects and selectedObject against the editor's clicks and
    // property changes landing mid-composite on run()'s thread.
    mutable juce::CriticalSection objectLock;
    // The view transform run() last used, so the editor can map a click in
    // the video rectangle back to a source-normalised point. Written on
    // run()'s thread, read on the message thread, under its own lock.
    ViewTransform lastViewTransform;
    mutable juce::CriticalSection viewTransformLock;
    int ticksSinceObjectNote = 0;
    // The view objects were last detected for, so run() can notice the user
    // panning or zooming and re-detect against what is now on screen.
    ViewTransform lastDetectView;
    bool haveDetectedOnce = false;

    void updateShipPhysics();

    // Simulates bullets/collisions against `gray` and draws the ship,
    // bullets, and explosion flashes into `rgbFrame` (already at the
    // zoom-upscaled resolution overlays are drawn into — see run()). Takes
    // no zoom factor: because rgbFrame is back at ~native resolution
    // whatever the zoom, every sprite in here is sized as a fraction of
    // that frame and is already zoom-independent on screen.
    // No-ops immediately when invaderModeEnabled is false.
    void updateAndDrawInvaderMode (cv::Mat& rgbFrame, const cv::Mat& gray,
                                   const ViewTransform& view);

    // Bullets carry the MIDI note they sounded, because that note is held
    // for the bullet's whole flight and released when it hits or leaves the
    // frame (see fireBullet) — so a shot across the frame rings out and a
    // point-blank one stabs. `ticksAlive` bounds that: a bullet that somehow
    // never resolves can't leave a note stuck on forever.
    struct InvaderBullet { float x, y, vx, vy; uint8_t note; int ticksAlive; };
    // Bullets are written from both run() (simulate/spawn-via-fireBullet)
    // and the editor's message thread (fireBullet(), on every Space press)
    // — guarded by invaderLock. Explosion flashes and the scheduled-note
    // queue are only ever touched from run() itself, so they need no lock.
    juce::CriticalSection invaderLock;
    std::vector<InvaderBullet> invaderBullets;
    // Atomic rather than plain floats: updateShipPhysics() (run() thread)
    // now writes these as velocity settles into a direction, while
    // fireBullet() (the editor's message thread) reads them to aim a shot.
    std::atomic<float> shipFacingX { 0.0f }, shipFacingY { -1.0f };

    // A hit blooms into a plasma ball for about a second (see drawPlasmaBall):
    // `seed` fixes that one explosion's pattern and phase at spawn, so two
    // hits never animate in lockstep, and `sizeScale` (see
    // invaderExplosionMinScale) fixes how big that one ball gets.
    struct InvaderExplosionFlash { float x, y; int framesRemaining; float seed; float sizeScale; };
    static constexpr int invaderExplosionFrames = 66;   // ~2s at run()'s ~30ms cadence
    // Every hit used to bloom to exactly the same half-the-short-side ball,
    // which made a burst of them read as one repeated stamp. Each explosion
    // now draws its own size, uniformly between this fraction of that full
    // size and the full size itself — so most hits are modest and the
    // occasional one still takes the screen over. Applies to every source:
    // the full size it scales is already frame-relative.
    static constexpr float invaderExplosionMinScale = 0.12f;
    std::vector<InvaderExplosionFlash> invaderExplosionFlashes;

    // One multiplier over the whole craft — hull, outline, exhaust flames and
    // the offsets the flames spawn at — so the ship can be resized without the
    // flames detaching from the tail or dwarfing the hull. Sits on top of the
    // frame-relative sizing (see spriteScale in updateAndDrawInvaderMode),
    // which it deliberately doesn't replace: that keeps the ship the same size
    // relative to any source, this sets how big "the same size" is. Bullets
    // and explosions are NOT scaled by it — they aren't the ship, and a
    // half-size shot stops being visible on a full-resolution frame.
    static constexpr float invaderShipScale = 0.5f;

    // An old-school demoscene plasma ball, alpha-blended into the frame at
    // `centre`: four interfering sine fields through a cosine palette, wrapped
    // onto a sphere, with a specular highlight and a rim reflection to make it
    // read as a glassy ball rather than a flat disc. `age` runs 0..1 over the
    // explosion's life and drives the zoom-in, the fade and the plasma's own
    // motion.
    //
    // The plasma is computed into a small fixed tile and scaled up rather than
    // evaluated per output pixel: the field needs four sines per sample, and at
    // display resolution with several hits live at once that would be millions
    // of sine calls a second inside the ~30ms capture loop. Scaling up also
    // costs nothing visually here — a plasma is smooth by construction, and the
    // interpolation reads as part of the glow.
    static void drawPlasmaBall (cv::Mat& rgbFrame, cv::Point centre, int maxRadius,
                                float age, float seed);

    // Twin exhaust trails, one either side of the ship's rear, active only
    // while thrust is actually being applied (not while just coasting) —
    // up to 3 fading orange/red puffs per side. Each newly-spawned puff
    // also schedules its own short 6-note run (see the queue below),
    // giving continuous flight a musical "engine hum" tied to the scale,
    // distinct in register from the laser (high) and explosion (mid-crash).
    // run()-thread only, like the explosion flashes above.
    // dirX/dirY (pointing away from the ship, unit length) and flickerPhase
    // are fixed at spawn so a flame's shape/orientation stays stable for its
    // whole life instead of snapping to whatever the ship's current facing
    // is on every drawn frame.
    struct InvaderExhaustParticle { float x, y, dirX, dirY, flickerPhase; bool leftSide; int framesRemaining; };
    std::vector<InvaderExhaustParticle> invaderExhaustParticles;
    int ticksSinceLastExhaustSpawn = 0;
    // Alternates which side spawns on each throttled spawn tick, rather
    // than both sides firing (and each scheduling its own 6-note run) in
    // lockstep every time — keeps the note density musical instead of two
    // runs colliding every ~150ms while thrust is held.
    bool exhaustSpawnLeftNext = true;

    // A short, "very short, one after the other" ascending run through the
    // current scale on a hit — real MIDI output alongside SynthEngine's
    // synthesized boom. Scheduled against the SEQUENCER'S OWN TIMELINE
    // (continuousPpq) rather than a countdown of run() ticks: a hit's
    // flourish now starts on the next 1/16 of the bar and steps in 1/32s, so
    // the invader layer lands on the same grid as the sequencer instead of
    // wherever in the beat the keypress happened to fall. `firePpq` below 0
    // means "unquantised, fire on the next drain" — used when there's no
    // transport running to snap to (see scheduleInvaderRun/transportRunning),
    // since continuousPpq simply doesn't advance while stopped and a
    // quantised event would then never come due at all.
    // Only ever touched from run(), which both queues and drains it.
    struct PendingNoteEvent { double firePpq; int ticksUntilFire; uint8_t note; uint8_t velocity; bool isOn; };
    std::vector<PendingNoteEvent> pendingInvaderNotes;

    // True while there's a transport to quantise against — the host's, or
    // the standalone's simulated one.
    bool transportRunning() const;

    // The invader layer's own view of the current scale: the user's Scale and
    // Root, plus whatever streak modulation has done to them (a scale-degree
    // offset, and past a long streak a different mode altogether). Every
    // invader pitch goes through here, so the modulation applies uniformly to
    // lasers, impacts, grazes and the engine hum without each of them
    // re-deriving it. `count` notes ascending from root+transposeSemitones.
    std::vector<int> invaderScaleNotes (int transposeSemitones, int count);

    // The play field's vertical axis IS the pitch axis, using the same 24
    // lanes and the same top-is-high orientation the sequencer samples with
    // (see processSequenceMode) — so aiming high sounds high, and the piano
    // roll reads the same way for a hit as for a sequenced note.
    int invaderNoteForY (float normalizedY, int transposeSemitones);

    // A note consonant with whatever the sequencer is sounding right now
    // (lastPlayedNote): a third, fifth or seventh above it *in scale
    // degrees*, so it stays inside the mode instead of being a chromatic
    // interval bolted on. Falls back to a random scale note when the
    // sequencer is silent.
    int harmonisedInvaderNote (int transposeSemitones);

    // Queues `notes` as a run starting on the next `gridBeats` boundary,
    // `stepBeats` apart, each held `stepBeats * 0.9` so it reads as a run of
    // distinct notes rather than a cluster.
    void scheduleInvaderRun (const std::vector<int>& notes, uint8_t velocity,
                             double gridBeats, double stepBeats);

    // --- Destructive play: hits erase what the sequencer reads ------------
    // A persistent, slowly-healing mask of everything that's been shot, in
    // normalized SOURCE-frame coordinates (not screen), at a fixed small
    // resolution independent of however large the capture actually is.
    // run() subtracts it from `gray` before the sequencer samples that same
    // Mat, so shooting a star literally removes its note from the loop — and
    // because the invader's own collision test runs against the same damaged
    // Mat, a hole can't be shot twice. Heals over a few seconds, so the loop
    // thins out under fire and grows back when you stop.
    //
    // Sky Map mode is the one case where this is anchored to the view rather
    // than the sky: there's no stable source frame behind it, only whatever
    // RA/Dec the renderer last drew, so panning slides the damage relative to
    // the stars. The healing keeps that bounded rather than leaving holes
    // permanently in the wrong place.
    static constexpr int invaderDamageMaskSize = 256;
    cv::Mat invaderDamageMask;   // CV_8U, 0 = intact, 255 = fully erased
    // Toggling the mode (message thread) must not allocate or clear the Mat
    // run() is reading — it asks for a reset through this instead.
    std::atomic<bool> invaderDamageResetRequested { true };
    void applyInvaderDamage (cv::Mat& gray, const ViewTransform& view);
    void addInvaderDamage (float viewX, float viewY, const ViewTransform& view);
    // The damage that falls inside the current view, scaled to `size` and
    // rotated to match the display — shared by the audible path (subtracted
    // from `gray`) and the visible one (subtracted from the RGB frame, so the
    // crater is in the picture too). False when there's nothing damaged.
    bool buildVisibleDamage (cv::Size size, const ViewTransform& view, cv::Mat& out) const;
    // Screen (view-normalized) -> a pixel in the source-space mask. (-1, -1)
    // when there's no mask yet.
    cv::Point damageMaskPoint (float viewX, float viewY, const ViewTransform& view) const;
    // Subtracting the mask from `gray` is enough for every pixel-sampling
    // source, but NOT for Sky Map: that branch of processSequenceMode asks the
    // star catalog directly (findBrightestStar) and never looks at `gray` at
    // all, so a star shot out of the picture would go on triggering its note
    // regardless. The sky-map branch consults this instead, which is what
    // makes destructive play work in the one mode where shooting a star is
    // meant literally.
    bool isViewPositionDamaged (float viewX, float viewY, const ViewTransform& view) const;

    // What the invader counts as something worth hitting. Normally that's
    // just the shared detection threshold, so the gun and the sequencer agree
    // about what's an object — but in Sky Map mode that threshold is pinned to
    // 0 (see switchSource: the star sampler reads the catalog, not pixels, and
    // a nonzero value there would crush the rendered star field), and a
    // threshold of 0 makes literally every pixel of empty black sky a target.
    // That's what made shots detonate in mid-air on nothing.
    float invaderTargetThreshold() const;
    // Star cores render at 125..255 (see SkyMapRenderer::magnitudeToBrightness),
    // while the constellation figures (~63), Messier ellipses (~87) and labels
    // (~113) are all dimmer — so this hits stars and nothing else.
    static constexpr float skyMapStarThreshold = 120.0f;

    // --- Grazing: flying through brightness without destroying it ---------
    // Sampled at the ship's own position every tick; a run of notes comes out
    // while it's inside a bright region, rate-limited so it arpeggiates
    // rather than sprays. Non-destructive, unlike a hit — the two verbs read
    // differently both musically (legato line vs. percussive impact) and
    // visually (halo vs. explosion).
    int ticksSinceLastGraze = 0;
    bool shipIsGrazing = false;

    // Set by updateAndDrawInvaderMode (run() thread) on a hit, consumed by
    // processBlock (audio thread) — triggers SynthEngine's synthesized
    // boom. -1 means none pending. pendingLaserFrequencyHz/laserTriggerPending
    // are the same handoff for fireBullet()'s zap (message thread).
    std::atomic<float> pendingExplosionIntensity { -1.0f };
    std::atomic<bool> laserTriggerPending { false };
    std::atomic<float> pendingLaserFrequencyHz { 440.0f };

    juce::MidiBuffer incomingMidiQueue;
    juce::CriticalSection midiCriticalSection;

    // Only ever touched from processBlock (the audio thread) — see its own
    // comment for why that's the correct thread to dispatch note-on/off to
    // it from, rather than addMidiMessage (called from run()'s thread).
    SynthEngine synthEngine;
    void applyParametersToSynth();

    // Transport state as processBlock last saw it, for spotting the moment it
    // stops. Audio thread only — never read or written anywhere else.
    bool wasTransportPlaying = false;
    juce::String currentPresetName;
    double currentSampleRate = 44100.0;

    std::unique_ptr<juce::PropertiesFile> appProperties;

    int activeSourceId = -1;
    juce::String activeFilePath;
    // A shared_ptr, not a plain member — see stopAndJoinStreamCaptureThread()
    // and streamCaptureLoop(): when a stalled network read forces the
    // capture thread to be abandoned (detached) rather than joined,
    // streamCaptureLoop() keeps its OWN copy of this pointer for the rest
    // of the function, so cv::VideoCapture's real (FFmpeg-backed) resources
    // stay alive for as long as that orphaned thread might still be
    // touching them — reassigning/destroying this member (a fresh capture
    // for a new source, or ~VisionMidiProcessor itself) then only drops
    // THIS reference, instead of tearing down the object out from under a
    // thread that's mid-call on it (a real, reproduced SIGSEGV: the
    // orphaned thread crashing inside FFmpeg with a freed context).
    std::shared_ptr<cv::VideoCapture> capture = std::make_shared<cv::VideoCapture>();
    bool isStaticImage = false;
    // True while activeSourceId is a network URL (rtsp://, rtmp://,
    // http(s)://…) rather than a local file — switchSource()/streamCaptureLoop()
    // use this to know a stalled connection needs reopening rather than a
    // local file's "seek back to frame 0" EOF handling.
    bool isNetworkStream = false;
    // Owned entirely by streamCaptureLoop() while it's running: iterations
    // (at its own ~100ms poll interval, not run()'s 30ms) left before
    // retrying a reconnect, so a genuinely down server isn't hammered.
    int streamReconnectCooldown = 0;
    // When a real frame last arrived from the stream, so a long-but-normal
    // gap between frames (e.g. a slow astro-cam sending ~1 frame every 15s)
    // isn't mistaken for a dead connection needing a reconnect.
    juce::int64 lastStreamFrameTimeMs = 0;
    // Runs capture>>frame for a network stream so that blocking call can
    // never stall run()'s 30ms MIDI-generation loop — see streamCaptureLoop().
    // Started/joined from switchSource(); joining always happens BEFORE
    // switchSource takes captureLock below, since this thread needs that
    // same lock (briefly, between reads) to publish frames, and holding it
    // across the join would deadlock.
    std::thread streamCaptureThread;
    std::atomic<bool> stopStreamThread { false };
    // Set true by streamCaptureLoop() as the very last thing it does before
    // returning — stopAndJoinStreamCaptureThread() polls this instead of
    // calling join() directly, so it can give up waiting (and detach
    // instead) without ever calling join() concurrently from two places.
    std::atomic<bool> streamCaptureThreadFinished { true };
    // The last successfully captured/decoded frame for whatever source is
    // active — for a network stream this is written exclusively by
    // streamCaptureLoop(); for every other source it's written by run()
    // itself, immediately after capturing. Either way, run()'s MIDI
    // generation reads through this rather than a fresh capture>>frame each
    // iteration, so it ticks at its own steady 30ms regardless of how fast,
    // slow, or bursty the underlying source's own frame arrival is.
    cv::Mat lastGoodFrame;
    cv::Mat staticImageFrame;
    juce::CriticalSection captureLock;

    int lastTriggeredNote = -1;
    double lastRawPpq = 0.0;
    int loopCount = 0;
    std::atomic<bool> playbackResetRequested { false };

    cv::Point lastStarPos { -1, -1 };
    cv::Point persistentStarPos { -1, -1 };
    int circleHoldFramesRemaining = 0;
    int rainbowHueCounter = 0;
    // Eased toward the velocity-derived target radius each frame rather
    // than snapping straight to it — note-to-note velocity swings used to
    // make the Occulting Object visibly jump in size every single note.
    float smoothedOccultingObjectRadius = 60.0f;

    // Sky map source
    SkyMapRenderer skyMapRenderer;

    std::atomic<bool> isRunning { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VisionMidiProcessor)
};
