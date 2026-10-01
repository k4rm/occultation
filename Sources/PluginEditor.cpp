#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

//==============================================================================
// Occultation colour rite — a near-black crypt in tarnished silver, lit only
// by a dying ember. The structure (panels, frames, text) is cold chrome and
// bone; the only warmth left is the ember accents on active/interactive bits.
namespace OccultPalette
{
    static const juce::Colour voidBlack      (0xff030203);
    static const juce::Colour panel          (0xff0a0a0b);
    static const juce::Colour panelDeep      (0xff050505);
    static const juce::Colour silverEdgeDim  (0xff2b2e30);
    static const juce::Colour silverEdge     (0xff5c6266);
    static const juce::Colour bloodRed       (0xff6b0f1a);
    static const juce::Colour crimson        (0xff9c1530);
    static const juce::Colour hellfireOrange (0xffdd5420);
    static const juce::Colour emberGlow      (0xffff7a33);
    static const juce::Colour ashGrey        (0xff888d90);
    static const juce::Colour boneWhite      (0xffd9dcde);
}

// The piano roll's height as a fraction of the video's own height — shared
// by paint() and idealWidthForHeight() so the two geometry calculations
// (drawing the roll, and sizing the window to fit it) can't drift apart.
// 1/9 makes the roll a third the size it was at the previous 1/3 ratio.
static constexpr float kRollToVideoRatio = 1.0f / 9.0f;

class OccultLookAndFeel : public juce::LookAndFeel_V4
{
public:
    OccultLookAndFeel()
    {
        using namespace OccultPalette;

        setColour (juce::ResizableWindow::backgroundColourId, voidBlack);

        setColour (juce::ComboBox::backgroundColourId, panel);
        setColour (juce::ComboBox::outlineColourId, silverEdgeDim);
        setColour (juce::ComboBox::textColourId, boneWhite);
        setColour (juce::ComboBox::arrowColourId, hellfireOrange);
        setColour (juce::ComboBox::buttonColourId, panel);

        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::textColourId, boneWhite);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, bloodRed.withAlpha (0.55f));
        setColour (juce::PopupMenu::highlightedTextColourId, emberGlow);

        setColour (juce::Slider::backgroundColourId, panelDeep);
        setColour (juce::Slider::trackColourId, bloodRed);
        setColour (juce::Slider::thumbColourId, hellfireOrange);
        setColour (juce::Slider::textBoxTextColourId, boneWhite);
        setColour (juce::Slider::textBoxBackgroundColourId, panelDeep);
        setColour (juce::Slider::textBoxOutlineColourId, silverEdgeDim);

        setColour (juce::TextButton::buttonColourId, panel);
        setColour (juce::TextButton::buttonOnColourId, crimson);
        setColour (juce::TextButton::textColourOffId, boneWhite);
        setColour (juce::TextButton::textColourOnId, boneWhite);

        setColour (juce::ToggleButton::textColourId, ashGrey);
        setColour (juce::ToggleButton::tickColourId, hellfireOrange);
        setColour (juce::ToggleButton::tickDisabledColourId, silverEdgeDim);

        setColour (juce::Label::textColourId, ashGrey);

        setColour (juce::TextEditor::backgroundColourId, panelDeep);
        setColour (juce::TextEditor::textColourId, boneWhite);
        setColour (juce::TextEditor::outlineColourId, silverEdgeDim);
        setColour (juce::TextEditor::focusedOutlineColourId, hellfireOrange);

        setColour (juce::ScrollBar::thumbColourId, bloodRed);
    }

    // The synth panel's knobs: a brushed-metal instrument-gauge bezel with a
    // recessed track, a glowing ember value arc, tick marks, and a slim HUD
    // needle — read as a spaceship cockpit dial rather than the stock JUCE
    // rotary slider, while staying inside the same voidBlack/silverEdge/
    // hellfireOrange palette as the rest of the plugin.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                            float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle,
                            juce::Slider&) override
    {
        using namespace OccultPalette;

        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (2.0f);
        float diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
        auto centre = bounds.getCentre();
        float radius = diameter * 0.5f;
        float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        // Outer bezel: a flat metal ring with a stroked highlight along its
        // upper rim to read as lit from above — cheap flat fills/strokes
        // rather than a per-knob ColourGradient, which at 14 knobs redrawn
        // 30x/sec (the title/moon animation repaints the whole editor
        // continuously) was expensive enough to peg the message thread.
        g.setColour (silverEdgeDim);
        g.fillEllipse (centre.x - radius, centre.y - radius, diameter, diameter);
        {
            juce::Path rimHighlight;
            rimHighlight.addCentredArc (centre.x, centre.y, radius * 0.94f, radius * 0.94f, 0.0f,
                                         juce::MathConstants<float>::pi * -0.75f, juce::MathConstants<float>::pi * -0.05f, true);
            g.setColour (silverEdge.withAlpha (0.6f));
            g.strokePath (rimHighlight, juce::PathStrokeType (radius * 0.12f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (voidBlack);
        g.fillEllipse (centre.x - radius * 0.86f, centre.y - radius * 0.86f, radius * 1.72f, radius * 1.72f);
        g.setColour (silverEdge.withAlpha (0.5f));
        g.drawEllipse (centre.x - radius * 0.86f, centre.y - radius * 0.86f, radius * 1.72f, radius * 1.72f, 1.0f);

        // Recessed track (the gauge groove) and the glowing ember value arc.
        float trackRadius = radius * 0.78f;
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, trackRadius, trackRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (silverEdgeDim.withAlpha (0.7f));
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, trackRadius, trackRadius, 0.0f, rotaryStartAngle, angle, true);
        g.setColour (hellfireOrange.withAlpha (0.35f));
        g.strokePath (valueArc, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (emberGlow);
        g.strokePath (valueArc, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Tick marks around the dial, instrument-panel style.
        constexpr int numTicks = 11;
        for (int i = 0; i < numTicks; ++i) {
            float t = (float) i / (float) (numTicks - 1);
            float tickAngle = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
            auto inner = centre.getPointOnCircumference (radius * 0.68f, tickAngle);
            auto outer = centre.getPointOnCircumference (radius * 0.76f, tickAngle);
            g.setColour (ashGrey.withAlpha (0.5f));
            g.drawLine ({ inner, outer }, 1.2f);
        }

        // A slim HUD needle from the hub to a glowing tip near the arc.
        auto tip = centre.getPointOnCircumference (radius * 0.6f, angle);
        auto hubEdge = centre.getPointOnCircumference (radius * 0.2f, angle);
        g.setColour (boneWhite);
        g.drawLine ({ hubEdge, tip }, 2.0f);
        g.setColour (emberGlow);
        g.fillEllipse (tip.x - 2.5f, tip.y - 2.5f, 5.0f, 5.0f);

        // Centre cap: a small metallic dome — a flat fill plus a tiny offset
        // highlight dot to suggest a curved surface, instead of a gradient.
        float capRadius = radius * 0.22f;
        g.setColour (panel);
        g.fillEllipse (centre.x - capRadius, centre.y - capRadius, capRadius * 2.0f, capRadius * 2.0f);
        g.setColour (boneWhite.withAlpha (0.25f));
        float highlightR = capRadius * 0.4f;
        g.fillEllipse (centre.x - capRadius * 0.4f - highlightR, centre.y - capRadius * 0.4f - highlightR,
                       highlightR * 2.0f, highlightR * 2.0f);
    }
};

//==============================================================================
// A row of small clickable cells, each drawing the actual shape of the
// waveform it selects — replaces a text dropdown ("Sine"/"Saw"/...) with
// something you can recognise at a glance. Order matches SynthEngine::
// Waveform (Sine=0, Saw, Square, Triangle, Noise), i.e. the "waveform"
// APVTS parameter's choice index — selecting a cell here IS selecting
// that parameter value, nothing more.
class WaveformIconSelector : public juce::Component
{
public:
    std::function<void (int)> onSelect;

    void setSelectedIndex (int idx)
    {
        if (selected != idx) { selected = idx; repaint(); }
    }
    int getSelectedIndex() const { return selected; }

    void paint (juce::Graphics& g) override
    {
        using namespace OccultPalette;
        int n = (int) juce::numElementsInArray (names);
        if (n == 0 || getWidth() <= 0) return;
        float cellW = (float) getWidth() / (float) n;

        for (int i = 0; i < n; ++i) {
            auto cell = juce::Rectangle<float> (cellW * (float) i, 0.0f, cellW, (float) getHeight()).reduced (3.0f);
            bool isSel = (i == selected);

            g.setColour (isSel ? bloodRed.withAlpha (0.55f) : panelDeep);
            g.fillRoundedRectangle (cell, 4.0f);
            g.setColour (isSel ? silverEdge : silverEdgeDim);
            g.drawRoundedRectangle (cell, 4.0f, 1.0f);

            g.setColour (isSel ? emberGlow : ashGrey);
            drawWaveIcon (g, i, cell.reduced (5.0f));
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        int n = (int) juce::numElementsInArray (names);
        if (n == 0 || getWidth() <= 0) return;
        float cellW = (float) getWidth() / (float) n;
        int idx = juce::jlimit (0, n - 1, (int) (e.position.x / cellW));
        setSelectedIndex (idx);
        if (onSelect) onSelect (idx);
    }

private:
    int selected = 0;
    static constexpr const char* names[5] = { "Sine", "Saw", "Square", "Triangle", "Noise" };

    static void drawWaveIcon (juce::Graphics& g, int type, juce::Rectangle<float> r)
    {
        if (r.getWidth() <= 0.0f || r.getHeight() <= 0.0f) return;
        float x0 = r.getX(), x1 = r.getRight(), yM = r.getCentreY(), h = r.getHeight() * 0.5f;
        juce::Path p;

        switch (type) {
            case 0: {   // sine
                p.startNewSubPath (x0, yM);
                for (float t = 0.0f; t <= 1.0f; t += 0.05f)
                    p.lineTo (x0 + t * r.getWidth(), yM - std::sin (t * juce::MathConstants<float>::twoPi) * h);
                break;
            }
            case 1: {   // saw: ramps up then drops
                p.startNewSubPath (x0, yM + h);
                p.lineTo (x0, yM - h);
                p.lineTo (x1, yM + h);
                break;
            }
            case 2: {   // square
                float xm = (x0 + x1) * 0.5f;
                p.startNewSubPath (x0, yM);
                p.lineTo (x0, yM - h); p.lineTo (xm, yM - h);
                p.lineTo (xm, yM + h); p.lineTo (x1, yM + h);
                break;
            }
            case 3: {   // triangle
                float xm = (x0 + x1) * 0.5f;
                p.startNewSubPath (x0, yM + h);
                p.lineTo (xm, yM - h);
                p.lineTo (x1, yM + h);
                break;
            }
            default: {   // noise: a jagged, irregular zigzag
                static const float xs[] = { 0.12f, 0.27f, 0.38f, 0.52f, 0.64f, 0.79f, 0.9f, 1.0f };
                static const float ys[] = { -0.9f, 0.7f, -0.4f, 0.95f, -0.7f, 0.4f, -0.85f, 0.2f };
                p.startNewSubPath (x0, yM);
                for (int i = 0; i < 8; ++i)
                    p.lineTo (x0 + xs[i] * r.getWidth(), yM + ys[i] * h);
                break;
            }
        }
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
};

//==============================================================================
// Keeps interactive drag-resizing on-aspect live, with no async catch-up lag:
// whichever axis the drag has actually moved further *since the gesture
// started* drives the other, so the dragged corner tracks the cursor on
// both axes instead of only ever reacting to vertical movement. (An earlier
// version derived width from height unconditionally — since the only
// resizer installed is the bottom-right corner, which always reports
// isStretchingLeft == false, that meant the constrainer's own edge
// assignment always won and the user's horizontal drag distance was thrown
// away outright.)
//
// The comparison has to be against the drag's *starting* size, not last
// frame's already-corrected bounds: JUCE's ResizableCornerComponent always
// computes each event's raw proposal as (size at mouseDown) + (total mouse
// offset since mouseDown), so that raw `bounds` parameter is a drift-free
// read of which way the mouse has actually moved, however many events into
// the gesture this is. Comparing against the previous (already
// aspect-corrected) frame instead double-counts our own correction on the
// axis we're not driving — since that correction is itself proportional to
// the real axis's movement — and can misjudge which axis the user means.
class EditorAspectConstrainer : public juce::ComponentBoundsConstrainer
{
public:
    explicit EditorAspectConstrainer (VisionMidiEditor& e) : editor (e) {}

    void resizeStart() override {
        editor.liveDragging = true;
        editor.dragStartWidth = editor.getWidth();
        editor.dragStartHeight = editor.getHeight();
        driverAxisLatched = false;
    }

    void resizeEnd() override { editor.liveDragging = false; }

    void checkBounds (juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previousBounds,
                       const juce::Rectangle<int>& limits, bool isStretchingTop, bool isStretchingLeft,
                       bool isStretchingBottom, bool isStretchingRight) override
    {
        int rawWidth = bounds.getWidth();
        int rawHeight = bounds.getHeight();

        juce::ComponentBoundsConstrainer::checkBounds (bounds, previousBounds, limits,
                                                          isStretchingTop, isStretchingLeft,
                                                          isStretchingBottom, isStretchingRight);

        // Only the standalone window is ours to shape. Forcing the editor
        // back onto the source's aspect inside a plugin host fights whatever
        // size that host wants the view to be — the host asks for one shape,
        // we hand back another, and the result is a window that won't settle.
        // Plugins keep the min/max limits applied just above and letterbox
        // the video into whatever they're given instead (see paint()).
        if (! enforceAspect)
            return;

        int widthDelta  = std::abs (rawWidth  - editor.dragStartWidth);
        int heightDelta = std::abs (rawHeight - editor.dragStartHeight);

        // Decide which axis drives the correction ONCE per drag gesture, the
        // first time the mouse has moved far enough from drag-start to tell —
        // then latch it for the rest of the gesture. Re-deciding on every
        // single mouse-move event (the previous behaviour) meant that right
        // at the start of a drag, while both deltas are still small and close
        // to each other, the two could flip which one was larger from one
        // event to the next, making the corner briefly jump to the OTHER
        // axis's target size before settling — visible as a snap right as a
        // resize begins. A few pixels of slop before latching absorbs the
        // ambiguous open of the gesture; after that the choice can't flicker
        // because it's simply not re-examined again until resizeStart().
        constexpr int latchThresholdPx = 3;
        if (! driverAxisLatched && std::max (widthDelta, heightDelta) >= latchThresholdPx) {
            widthIsDriver = widthDelta >= heightDelta;
            driverAxisLatched = true;
        }

        if (! driverAxisLatched)
            return;   // still within the dead zone — let the raw (unaspected) bounds through unchanged

        if (widthIsDriver) {
            int idealHeight = editor.idealHeightForWidth (bounds.getWidth());
            if (isStretchingTop) bounds.setTop (bounds.getBottom() - idealHeight);
            else                 bounds.setHeight (idealHeight);
        } else {
            int idealWidth = editor.idealWidthForHeight (bounds.getHeight());
            if (isStretchingLeft) bounds.setLeft (bounds.getRight() - idealWidth);
            else                  bounds.setWidth (idealWidth);
        }

        // The base class's own min/max check already ran above, but only
        // against the RAW dragged bounds — the ideal-width/height
        // recomputation just above can then undercut those limits all over
        // again (e.g. a small dragged width clamps to getMinimumWidth() by
        // that base-class call, but the HEIGHT this override derives FROM
        // that clamped width via idealHeightForWidth() is never itself
        // re-checked against getMinimumHeight()). That let the window
        // shrink below what the left-panel controls and synth panel
        // actually need, clipping/overlapping them — so re-check both
        // dimensions one more time here, growing whichever fell short and
        // re-deriving its aspect partner so the pair stays on-aspect
        // rather than just individually in range.
        if (bounds.getHeight() < getMinimumHeight()) {
            bounds.setHeight (getMinimumHeight());
            bounds.setWidth (juce::jmax (getMinimumWidth(), editor.idealWidthForHeight (getMinimumHeight())));
        }
        if (bounds.getWidth() < getMinimumWidth()) {
            bounds.setWidth (getMinimumWidth());
            bounds.setHeight (juce::jmax (getMinimumHeight(), editor.idealHeightForWidth (getMinimumWidth())));
        }
    }

    void setEnforceAspect (bool shouldEnforce) { enforceAspect = shouldEnforce; }

private:
    VisionMidiEditor& editor;
    bool driverAxisLatched = false;
    bool widthIsDriver = true;
    bool enforceAspect = true;
};

//==============================================================================
VisionMidiEditor::VisionMidiEditor (VisionMidiProcessor& p)
    : juce::AudioProcessorEditor (&p),
      audioProcessor (p)
{
    auto constrainer = std::make_unique<EditorAspectConstrainer> (*this);
    constrainer->setSizeLimits (600, 400 + bottomChromeHeight, 3840, 2400 + bottomChromeHeight);
    constrainer->setEnforceAspect (p.wrapperType == juce::AudioProcessor::wrapperType_Standalone);
    aspectConstrainer = std::move (constrainer);
    setConstrainer (aspectConstrainer.get());

    setSize (900, 520 + bottomChromeHeight);
    setResizable (true, true);
    setWantsKeyboardFocus (true);

    // Scales the whole UI uniformly (layout, fonts, borders) rather than
    // rescaling every hand-placed pixel constant below — JUCE renders,
    // hit-tests, and (per juce_StandaloneFilterWindow.h) auto-sizes the host
    // window around this transform, and divides drag-resize deltas by it too.
    setTransform (juce::AffineTransform::scale (1.0f));

    occultLookAndFeel = std::make_unique<OccultLookAndFeel>();
    setLookAndFeel (occultLookAndFeel.get());

    // The title is hand-drawn in paint() rather than a Label, so a dark moon
    // can be drawn on top of it afterwards, in the same call, without a child
    // component's own background painting over and clipping the animation.

    // Captions above each dropdown, explaining what it controls — plain
    // small labels sharing the LookAndFeel's default text colour rather
    // than anything bespoke, so they read as chrome, not content.
    auto setupCaption = [this] (juce::Label& label, const juce::String& text) {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::Font (14.0f, juce::Font::bold));
        label.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (label);
    };
    setupCaption (sourceCaption, "Source");
    setupCaption (stepsCaption, "Steps");
    setupCaption (scaleCaption, "Scale");
    setupCaption (occultingObjectCaption, "Occulting Object");

    addAndMakeVisible (cameraSelector);
    updateAvailableSources();
    int savedId = audioProcessor.getLastSourceId();
    if (savedId > 0) {
        // dontSendNotification means onChange (below) never fires for this
        // restore, so isSkyMapMode has to be set here directly — otherwise a
        // saved Sky Map selection left the dropdown showing "Sky Map" while
        // isSkyMapMode silently stayed false.
        cameraSelector.setSelectedId (savedId, juce::dontSendNotification);
        isSkyMapMode = (savedId == 997);
    }

    cameraSelector.onChange = [this] {
        int selectedId = cameraSelector.getSelectedId();
        if (selectedId == 997) {
            audioProcessor.selectBuiltInSource (997);  // Sky Map source ID
            isSkyMapMode = true;
            updateSkyMapControlVisibility();
        } else if (selectedId == 1000) {
            isSkyMapMode = false;
            updateSkyMapControlVisibility();
            loadCustomFile();
        } else if (selectedId == 1001) {
            isSkyMapMode = false;
            updateSkyMapControlVisibility();
            loadNetworkStream();
        } else if (selectedId > 0) {
            isSkyMapMode = false;
            updateSkyMapControlVisibility();
            audioProcessor.selectBuiltInSource (selectedId);
        }
    };

    // Loop length for Step Sequence mode's sweeping cursor, in quarter
    // notes — item IDs double as the beat count itself so onChange doesn't
    // need a lookup table.
    addAndMakeVisible (stepsSelector);
    stepsSelector.addItem ("64 Steps", 64);
    stepsSelector.addItem ("32 Steps", 32);
    stepsSelector.addItem ("16 Steps", 16);
    stepsSelector.addItem ("8 Steps", 8);
    stepsSelector.addItem ("4 Steps", 4);
    stepsSelector.addItem ("2 Steps", 2);
    stepsSelector.addItem ("1 Step", 1);
    stepsSelector.setSelectedId ((int) audioProcessor.sequenceLoopBeats.load(), juce::dontSendNotification);
    stepsSelector.onChange = [this] {
        audioProcessor.sequenceLoopBeats.store ((double) stepsSelector.getSelectedId());
    };

    addAndMakeVisible (scaleSelector);
    scaleSelector.addItem ("1. Lydian", 1);
    scaleSelector.addItem ("2. Major (Ionian)", 2);
    scaleSelector.addItem ("3. Minor (Aeolian)", 3);
    scaleSelector.addItem ("4. Dorian", 4);
    scaleSelector.addItem ("5. Mixolydian", 5);
    scaleSelector.addItem ("6. Major Pentatonic", 6);
    scaleSelector.addItem ("7. Minor Pentatonic", 7);
    scaleSelector.addItem ("8. Chromatic", 8);
    scaleSelector.addItem ("9. Locrian", 9);
    scaleSelector.addItem ("10. Phrygian", 10);
    scaleSelector.addItem ("11. Harmonic Minor", 11);
    scaleSelector.addItem ("12. Melodic Minor", 12);
    scaleSelector.addItem ("13. Whole Tone", 13);
    scaleSelector.addItem ("14. Blues Scale", 14);
    scaleSelector.addItem ("15. Double Harmonic", 15);
    scaleSelector.addItem ("16. Enigmatic", 16);
    scaleSelector.addItem ("17. Hungarian Minor", 17);
    scaleSelector.addItem ("18. Neapolitan Major", 18);
    scaleSelector.addItem ("19. Neapolitan Minor", 19);
    scaleSelector.addItem ("20. Prometheus", 20);
    scaleSelector.addItem ("21. Tritone Scale", 21);
    scaleSelector.addItem ("22. In Sen", 22);
    scaleSelector.addItem ("23. Hirajoshi", 23);
    scaleSelector.addItem ("24. Iwato", 24);
    scaleSelector.addItem ("25. Kumoi", 25);
    scaleSelector.addItem ("26. Pelog", 26);
    scaleSelector.addItem ("27. Spanish Gypsy", 27);
    scaleSelector.addItem ("28. Balinese", 28);
    scaleSelector.addItem ("29. Byzantine", 29);
    scaleSelector.addItem ("30. Overtone Scale", 30);
    scaleSelector.addItem ("31. Altered Scale", 31);
    scaleSelector.addItem ("32. Lydian Augmented", 32);
    scaleSelector.addItem ("33. Lydian Dominant", 33);
    scaleSelector.addItem ("34. Locrian 6th", 34);
    scaleSelector.addItem ("35. Super Locrian bb7", 35);
    scaleSelector.setSelectedId (audioProcessor.currentScale.load() + 1, juce::dontSendNotification);
    scaleSelector.onChange = [this] {
        audioProcessor.currentScale.store (scaleSelector.getSelectedId() - 1);
    };

    // What's drawn over the currently-playing note's position — item IDs
    // double as the OccultingObjectType enum value itself (+1, since combo
    // box IDs can't be 0), so onChange doesn't need a lookup table.
    addAndMakeVisible (occultingObjectSelector);
    occultingObjectSelector.addItem ("Circle", 1);
    occultingObjectSelector.addItem ("Sun", 2);
    occultingObjectSelector.addItem ("Moon", 3);
    occultingObjectSelector.addItem ("Mercury", 4);
    occultingObjectSelector.addItem ("Venus", 5);
    occultingObjectSelector.addItem ("Earth", 6);
    occultingObjectSelector.addItem ("Mars", 7);
    occultingObjectSelector.addItem ("Jupiter", 8);
    occultingObjectSelector.addItem ("Saturn", 9);
    occultingObjectSelector.addItem ("Uranus", 10);
    occultingObjectSelector.addItem ("Neptune", 11);
    occultingObjectSelector.addItem ("Pluto", 12);
    occultingObjectSelector.setSelectedId ((int) audioProcessor.occultingObject.load() + 1, juce::dontSendNotification);
    occultingObjectSelector.onChange = [this] {
        audioProcessor.occultingObject.store ((OccultingObjectType) (occultingObjectSelector.getSelectedId() - 1));
    };

    addAndMakeVisible (rootNoteSlider);
    rootNoteSlider.setSliderStyle (juce::Slider::LinearVertical);
    rootNoteSlider.setRange (0.0, 127.0, 1.0);
    rootNoteSlider.setValue (audioProcessor.currentRootNote.load(), juce::dontSendNotification);
    rootNoteSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 50, 18);
    // Shows/accepts a note name (e.g. "C4") instead of a raw MIDI number —
    // octaveNumForMiddleC=4 matches the common "C4 = middle C = 60" naming.
    rootNoteSlider.textFromValueFunction = [] (double v) {
        return juce::MidiMessage::getMidiNoteName ((int) v, true, true, 4);
    };
    rootNoteSlider.valueFromTextFunction = [] (const juce::String& text) -> double {
        juce::String t = text.trim().toUpperCase();
        if (t.isEmpty()) return 60.0;
        int pitchClass;
        switch ((char) t[0]) {
            case 'C': pitchClass = 0;  break;
            case 'D': pitchClass = 2;  break;
            case 'E': pitchClass = 4;  break;
            case 'F': pitchClass = 5;  break;
            case 'G': pitchClass = 7;  break;
            case 'A': pitchClass = 9;  break;
            case 'B': pitchClass = 11; break;
            default:  return 60.0;
        }
        int i = 1;
        if (i < t.length() && t[i] == '#')      { pitchClass += 1; ++i; }
        else if (i < t.length() && t[i] == 'B') { pitchClass -= 1; ++i; }
        int octave = t.substring (i).getIntValue();
        int midi = (octave + 1) * 12 + pitchClass;
        return (double) juce::jlimit (0, 127, midi);
    };
    // setValue() no-ops (including skipping the text box refresh) when the
    // value passed in already equals the current one — which it does here,
    // since both reads come from the same unchanged atomic — so the text
    // box has to be told to redraw explicitly instead.
    rootNoteSlider.updateText();
    rootNoteSlider.onValueChange = [this] {
        audioProcessor.currentRootNote.store ((int)rootNoteSlider.getValue());
    };

    rootNoteLabel.setText ("Root Note", juce::dontSendNotification);
    rootNoteLabel.setFont (juce::Font(10.0f));
    rootNoteLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (rootNoteLabel);

    bool autoOn = audioProcessor.isAutoThresholdEnabled.load();
    addAndMakeVisible (autoThresholdToggle);
    autoThresholdToggle.setButtonText ("Auto Thresh");
    autoThresholdToggle.setToggleState (autoOn, juce::dontSendNotification);
    autoThresholdToggle.onClick = [this] {
        bool isAuto = autoThresholdToggle.getToggleState();
        audioProcessor.isAutoThresholdEnabled.store (isAuto);
        thresholdSlider.setAlpha (isAuto ? 0.5f : 1.0f);
    };

    addAndMakeVisible (invaderModeToggle);
    invaderModeToggle.setButtonText ("Invader Mode");
    invaderModeToggle.setToggleState (audioProcessor.invaderModeEnabled.load(), juce::dontSendNotification);
    invaderModeToggle.onClick = [this] {
        const bool on = invaderModeToggle.getToggleState();
        audioProcessor.setInvaderModeEnabled (on);
        invaderStreakToggle.setVisible (on);
        resized();             // the streak row appears/disappears with it
        grabKeyboardFocus();   // arrow keys/Space need to reach keyPressed() right away, not a click elsewhere first
    };

    // Streak modulation — a sub-option of Invader mode, so it's added but only
    // made visible while that mode is on (see the member's comment).
    addChildComponent (invaderStreakToggle);
    invaderStreakToggle.setButtonText ("Streak Mod.");
    invaderStreakToggle.setTooltip ("Consecutive hits transpose the invader layer up the scale, "
                                    "and long streaks push it onto a more exotic mode. "
                                    "The sequencer keeps the scale you chose.");
    invaderStreakToggle.setToggleState (audioProcessor.invaderStreakModulationEnabled.load(),
                                       juce::dontSendNotification);
    invaderStreakToggle.setVisible (audioProcessor.invaderModeEnabled.load());
    invaderStreakToggle.onClick = [this] {
        audioProcessor.invaderStreakModulationEnabled.store (invaderStreakToggle.getToggleState());
        grabKeyboardFocus();
    };

    thresholdSlider.setSliderStyle (juce::Slider::LinearVertical);
    thresholdSlider.setRange (0.0, 255.0, 1.0);
    thresholdSlider.setValue (audioProcessor.detectionThreshold.load(), juce::dontSendNotification);
    thresholdSlider.setAlpha (autoOn ? 0.5f : 1.0f);
    thresholdSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 50, 18);

    // Slider stays enabled (not setEnabled(false)) even while auto-threshold is
    // driving it, otherwise JUCE would never deliver the mouse events needed to
    // detect the click that's supposed to turn auto-threshold off.
    thresholdSlider.onDragStart = [this] {
        if (audioProcessor.isAutoThresholdEnabled.load()) {
            audioProcessor.isAutoThresholdEnabled.store (false);
            autoThresholdToggle.setToggleState (false, juce::dontSendNotification);
            thresholdSlider.setAlpha (1.0f);
        }
    };

    thresholdSlider.onValueChange = [this] {
        if (!audioProcessor.isAutoThresholdEnabled.load())
            audioProcessor.detectionThreshold.store ((float)thresholdSlider.getValue());
    };
    addAndMakeVisible (thresholdSlider);

    thresholdLabel.setText ("Thresh", juce::dontSendNotification);
    thresholdLabel.setFont (juce::Font(10.0f));
    thresholdLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (thresholdLabel);

    standalonePlayButton.setButtonText ("Play / Stop");
    standalonePlayButton.setClickingTogglesState (true);
    standalonePlayButton.setToggleState (audioProcessor.standalonePlaying.load(), juce::dontSendNotification);
    lastKnownStandalonePlaying = audioProcessor.standalonePlaying.load();
    standalonePlayButton.onClick = [this] {
        bool isPlaying = standalonePlayButton.getToggleState();
        audioProcessor.standalonePlaying.store (isPlaying);
        if (!isPlaying)
            audioProcessor.resetPlaybackState();
    };

    if (audioProcessor.wrapperType == juce::AudioProcessor::wrapperType_Standalone) {
        addAndMakeVisible (standalonePlayButton);
    } else {
        standalonePlayButton.setVisible (false);
    }

    zoomInButton.setButtonText ("+");
    zoomInButton.onClick = [this] { zoomBy (1.4f); };
    addAndMakeVisible (zoomInButton);

    zoomOutButton.setButtonText ("-");
    zoomOutButton.onClick = [this] { zoomBy (1.0f / 1.4f); };
    addAndMakeVisible (zoomOutButton);

    // Plain text rather than a rotation-arrow glyph — see zoomInButton/
    // zoomOutButton above, whose original Unicode arrows didn't resolve in
    // this LookAndFeel's font and rendered as garbage boxes.
    rotateButton.setButtonText ("Rot");
    rotateButton.onClick = [this] { audioProcessor.rotateViewBy (10.0f); };
    addAndMakeVisible (rotateButton);

    statusLabel.setText ("Status: Ready...", juce::dontSendNotification);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setFont (juce::Font (11.0f));
    statusLabel.setColour (juce::Label::textColourId, OccultPalette::hellfireOrange.withAlpha (0.85f));
    addAndMakeVisible (statusLabel);

    // Sky map: navigated by click-dragging and scrolling directly on the
    // image (see mouseDown/mouseDrag/mouseWheelMove) rather than dedicated
    // buttons — this label is the only sky-map-specific control left.
    // addChildComponent() (not addAndMakeVisible()) so it actually starts
    // hidden: addAndMakeVisible() unconditionally sets visible=true, which
    // would silently undo a setVisible(false) called first.
    skyMapCoordinatesLabel.setFont (juce::Font (11.0f));
    skyMapCoordinatesLabel.setJustificationType (juce::Justification::centredLeft);
    skyMapCoordinatesLabel.setColour (juce::Label::textColourId, OccultPalette::hellfireOrange.withAlpha (0.75f));
    addChildComponent (skyMapCoordinatesLabel);

    constellationList = SkyMapRenderer::listConstellations();
    int cassiopeiaId = 1;
    for (size_t i = 0; i < constellationList.size(); ++i) {
        int itemId = (int) i + 1;
        constellationSelector.addItem (constellationList[i].name, itemId);
        if (constellationList[i].name == "Cassiopeia")
            cassiopeiaId = itemId;
    }
    constellationSelector.setSelectedId (cassiopeiaId, juce::dontSendNotification);
    constellationSelector.onChange = [this] {
        int idx = constellationSelector.getSelectedId() - 1;
        if (idx < 0 || idx >= (int) constellationList.size())
            return;
        const auto& c = constellationList[(size_t) idx];
        audioProcessor.setSkyMapView (c.raHours * 15.0f, c.decDegrees,
                                      audioProcessor.skyMapZoomDegPerPixel.load());
        updateSkyMapCoordinateDisplay();
    };
    addChildComponent (constellationSelector);

    if (isSkyMapMode)
        updateSkyMapControlVisibility();

    // See showFileBrowserOverlay()/showStreamOverlay() in the header for why
    // these are plain child Components rather than a FileChooser/AlertWindow.
    // fileBrowserOverlay itself is created lazily, in showFileBrowserOverlay()
    // — constructing it eagerly here made it start listing the home
    // directory immediately on every launch, which macOS treats as needing
    // Documents-folder consent and pops that permission dialog before the
    // user has ever asked to open a file.
    fileBrowserChooseButton.onClick = [this] {
        auto file = fileBrowserOverlay->getSelectedFile (0);
        if (file.existsAsFile()) adoptFile (file);
        hideFileBrowserOverlay();
    };
    fileBrowserCancelButton.onClick = [this] { hideFileBrowserOverlay(); };
    addChildComponent (fileBrowserChooseButton);
    addChildComponent (fileBrowserCancelButton);

    streamPromptLabel.setText ("Enter an RTSP/RTMP/HTTP(S) video stream URL:", juce::dontSendNotification);
    streamPromptLabel.setFont (juce::Font (12.0f));
    streamPromptLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addChildComponent (streamPromptLabel);
    streamUrlEditor.setFont (juce::Font (13.0f));
    addChildComponent (streamUrlEditor);
    streamConnectButton.onClick = [this] {
        juce::String url = streamUrlEditor.getText().trim();
        if (url.isNotEmpty()) adoptSource (url);
        hideStreamOverlay();
    };
    streamCancelButton.onClick = [this] { hideStreamOverlay(); };
    addChildComponent (streamConnectButton);
    addChildComponent (streamCancelButton);

    setupSynthPanel();

    // Lay everything out now that the synth panel's components actually
    // exist. setSize() far above already fired resized() once, but that ran
    // before setupSynthPanel() had created any of them, so layoutSynthPanel()
    // took its "synthKnobs is still empty" early return and the knobs were
    // left at their default zero-size bounds. A host that then resizes the
    // editor (Standalone's window, most VST3 hosts) hides the bug by firing
    // resized() a second time; an AU host that creates its NSView at exactly
    // the size we asked for never does, so the knobs simply never appeared.
    resized();

    // The initial setSize far above is a fixed guess made before any of this
    // existed; the height the layout actually needs only becomes known once
    // resized() has measured the left control column. Standalone hid the
    // shortfall because the window grows to the constrainer's minimum
    // afterwards, but a plugin host sizes its window to whatever the editor
    // reports at attach time and never revisits it — so the guess stood and
    // the bottom of the knob row was simply clipped off. Ask for the real
    // height now, while the host is still reading it.
    if (getHeight() < requiredEditorHeight())
        setSize (getWidth(), requiredEditorHeight());

    startTimerHz(30);
}

VisionMidiEditor::~VisionMidiEditor() {
    setLookAndFeel (nullptr);
    stopTimer();
}

void VisionMidiEditor::setupSynthPanel()
{
    auto& apvts = audioProcessor.apvts;

    auto setupCaption = [this] (juce::Label& caption, const juce::String& text) {
        caption.setText (text, juce::dontSendNotification);
        caption.setFont (juce::Font (14.0f, juce::Font::bold));
        caption.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (caption);
    };

    // Waveform: a row of shape icons instead of a text dropdown — synced
    // both ways by hand (there's no ComboBoxAttachment-equivalent for a
    // plain Component), since Synth Type presets also change this
    // parameter out from under the UI and the icon row has to follow.
    waveformSelector = std::make_unique<WaveformIconSelector>();
    addAndMakeVisible (*waveformSelector);
    if (auto* waveformParam = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("waveform")))
        waveformSelector->setSelectedIndex (waveformParam->getIndex());
    waveformSelector->onSelect = [this] (int idx) {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (audioProcessor.apvts.getParameter ("waveform")))
            *p = idx;
    };
    setupCaption (waveformCaption, "Waveform");

    // --- Preset bar ---
    setupCaption (presetCaption, "Preset");
    presetSelector.setTextWhenNothingSelected ("Init");
    presetSelector.setTextWhenNoChoicesAvailable ("No presets");
    addAndMakeVisible (presetSelector);
    presetSelector.onChange = [this] {
        auto name = presetSelector.getText();
        if (name.isNotEmpty() && name != audioProcessor.getCurrentPresetName())
            audioProcessor.loadPreset (name);
    };

    auto setupPresetButton = [this] (juce::TextButton& b, const juce::String& text,
                                     std::function<void()> action) {
        b.setButtonText (text);
        b.onClick = std::move (action);
        addAndMakeVisible (b);
    };
    setupPresetButton (prevPresetButton, "<",  [this] { stepPreset (-1); });
    setupPresetButton (nextPresetButton, ">",  [this] { stepPreset (1); });
    setupPresetButton (savePresetButton, "Save", [this] {
        // Overwrites the selected preset in place. With nothing selected
        // there's nothing to overwrite, and a factory preset can't be
        // written to at all — both fall through to Save As, which is what
        // "save my edits to a built-in voice" should do anyway.
        auto current = audioProcessor.getCurrentPresetName();
        if (current.isEmpty() || audioProcessor.isFactoryPreset (current)) promptForPresetName();
        else { audioProcessor.savePreset (current); refreshPresetList (current); }
    });
    setupPresetButton (saveAsPresetButton, "Save As", [this] { promptForPresetName(); });
    setupPresetButton (deletePresetButton, "Del", [this] {
        auto current = audioProcessor.getCurrentPresetName();
        if (current.isEmpty()) return;
        if (audioProcessor.deletePreset (current)) refreshPresetList();
    });

    refreshPresetList (audioProcessor.getCurrentPresetName());

    // {parameter ID, short knob label} — order here is the order they lay
    // out left-to-right in layoutSynthPanel(). One flat row rather than
    // grouped sub-panels: 15 knobs plus the 3 combo boxes above already
    // fits this panel's height without needing tabs/pages.
    static const std::pair<const char*, const char*> knobDefs[] = {
        { "unisonDetune",     "Detune" },
        { "masterVolume",     "Volume" },
        { "glide",            "Glide" },
        { "attack",           "Attack" },
        { "decay",            "Decay" },
        { "sustain",          "Sustain" },
        { "release",          "Release" },
        { "filterCutoff",     "Cutoff" },
        { "filterResonance",  "Resonance" },
        { "filterEnvAmount",  "Filter Env" },
        { "distortionAmount", "Drive" },
        { "chorusAmount",     "Chorus" },
        { "delayAmount",      "Delay" },
        { "delayTime",        "Delay Time" },
        { "reverbAmount",     "Reverb" },
    };

    for (auto& [paramId, labelText] : knobDefs) {
        auto knob = std::make_unique<SynthKnob>();
        knob->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible (knob->slider);
        knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramId, knob->slider);

        knob->label.setText (labelText, juce::dontSendNotification);
        knob->label.setFont (juce::Font (14.0f, juce::Font::bold));
        knob->label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (knob->label);

        synthKnobs.push_back (std::move (knob));
    }
}

void VisionMidiEditor::refreshPresetList (const juce::String& nameToSelect)
{
    auto factory = audioProcessor.getFactoryPresetNames();
    auto user    = audioProcessor.getPresetNames();
    auto names   = audioProcessor.getAllPresetNames();

    // Two labelled banks in one list, the way a hardware-style preset
    // browser reads: the built-in voices first, then anything saved.
    presetSelector.clear (juce::dontSendNotification);
    int id = 1;
    if (! factory.isEmpty()) {
        presetSelector.addSectionHeading ("Factory");
        for (const auto& n : factory) presetSelector.addItem (n, id++);
    }
    if (! user.isEmpty()) {
        presetSelector.addSectionHeading ("User");
        for (const auto& n : user) presetSelector.addItem (n, id++);
    }

    int index = names.indexOf (nameToSelect);
    if (index >= 0)
        presetSelector.setSelectedId (index + 1, juce::dontSendNotification);
    else
        presetSelector.setSelectedId (0, juce::dontSendNotification);

    // Factory presets are the built-in table — they can be loaded and used
    // as a starting point, but not overwritten or removed.
    deletePresetButton.setEnabled (index >= 0 && ! audioProcessor.isFactoryPreset (nameToSelect));
    // Enabled from the first preset onwards, not the second: with exactly
    // one saved, the arrows are still how you load it without opening the
    // list, and nothing enables them again until you'd already selected it.
    prevPresetButton.setEnabled (! names.isEmpty());
    nextPresetButton.setEnabled (! names.isEmpty());
}

void VisionMidiEditor::stepPreset (int delta)
{
    auto names = audioProcessor.getAllPresetNames();
    if (names.isEmpty()) return;

    int index = names.indexOf (audioProcessor.getCurrentPresetName());
    // Nothing loaded yet: step into the list from either end rather than
    // doing nothing, so the arrows always go somewhere on a first press.
    index = (index < 0) ? (delta > 0 ? 0 : names.size() - 1)
                        : (index + delta + names.size()) % names.size();

    if (audioProcessor.loadPreset (names[index]))
        refreshPresetList (names[index]);
}

void VisionMidiEditor::promptForPresetName()
{
    auto* window = new juce::AlertWindow ("Save Preset",
                                          "Name this preset:",
                                          juce::MessageBoxIconType::NoIcon);
    auto suggested = audioProcessor.getCurrentPresetName();
    window->addTextEditor ("name", suggested.isEmpty() ? "My Preset" : suggested, {});
    window->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    // enterModalState, not runModalLoop: a plugin can't spin a modal loop
    // inside its host, and this works the same in the standalone anyway.
    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, window] (int result) {
            if (result == 1) {
                auto name = window->getTextEditorContents ("name").trim();
                if (name.isNotEmpty() && audioProcessor.savePreset (name))
                    refreshPresetList (name);
            }
        }), true);
}

void VisionMidiEditor::layoutSynthPanel (juce::Rectangle<int> area)
{
    auto top = area.removeFromTop (44);
    int gap = 8;

    // The Synth Type box used to sit here; its width now goes to the preset
    // bar, which is what selects a voice since the built-in ones became
    // factory presets.
    int waveformBoxW = 190;

    auto slot2 = top.removeFromLeft (waveformBoxW);
    waveformCaption.setBounds (slot2.getX(), slot2.getY(), waveformBoxW, 15);
    // resized() runs synchronously from the early setSize() call in the
    // constructor, well before setupSynthPanel() (further down that same
    // constructor) has actually created waveformSelector — guard against
    // that still-null unique_ptr rather than dereferencing it.
    if (waveformSelector != nullptr)
        waveformSelector->setBounds (slot2.getX(), slot2.getY() + 16, waveformBoxW, 24);

    // Preset bar fills whatever the Type and Waveform controls left of the
    // top row: [<][ name list ][>] [Save][Save As][Del]. The list takes the
    // slack so long preset names stay readable at any window width, and the
    // whole bar is simply skipped if the row is too narrow to hold it
    // legibly rather than overlapping the controls to its left.
    top.removeFromLeft (gap * 2);
    constexpr int arrowW = 22, saveW = 46, saveAsW = 62, delW = 36, minListW = 90;
    int neededW = arrowW * 2 + saveW + saveAsW + delW + minListW + gap * 5;
    bool presetBarFits = top.getWidth() >= neededW;

    presetCaption.setVisible (presetBarFits);
    presetSelector.setVisible (presetBarFits);
    prevPresetButton.setVisible (presetBarFits);
    nextPresetButton.setVisible (presetBarFits);
    savePresetButton.setVisible (presetBarFits);
    saveAsPresetButton.setVisible (presetBarFits);
    deletePresetButton.setVisible (presetBarFits);

    if (presetBarFits) {
        presetCaption.setBounds (top.getX(), top.getY(), top.getWidth(), 15);
        auto row = top.withTrimmedTop (16).withHeight (24);

        prevPresetButton.setBounds (row.removeFromLeft (arrowW));
        row.removeFromLeft (2);

        int listW = row.getWidth() - (arrowW + saveW + saveAsW + delW + gap * 4);
        presetSelector.setBounds (row.removeFromLeft (listW).reduced (2, 0));
        row.removeFromLeft (2);

        nextPresetButton.setBounds (row.removeFromLeft (arrowW));
        row.removeFromLeft (gap);
        savePresetButton.setBounds (row.removeFromLeft (saveW));
        row.removeFromLeft (gap);
        saveAsPresetButton.setBounds (row.removeFromLeft (saveAsW));
        row.removeFromLeft (gap);
        deletePresetButton.setBounds (row.removeFromLeft (delW));
    }

    area.removeFromTop (6);

    // Output-level meter, reserved from the right before the knobs claim
    // the rest — same look/width as the video-side MIDI meter, but running
    // the full height of this row instead of the video's height.
    auto meterSlot = area.removeFromRight (16);
    audioLevelMeterBounds = meterSlot.reduced (3, 0).toFloat();

    if (synthKnobs.empty()) return;
    int knobW = area.getWidth() / (int) synthKnobs.size();
    for (auto& knob : synthKnobs) {
        auto slot = area.removeFromLeft (knobW);
        // Label sits directly against the knob below it. The rotary
        // LookAndFeel draws its circle centred in the slider's full bounds
        // at a diameter of min(width,height) — if the slider were left
        // much taller than it is wide, that circle would centre itself
        // well below the label, opening up a visible gap that looks like
        // it belongs to the label rather than to unused space below the
        // knob. Capping the slider's height at its own width keeps the
        // circle pinned to the top, right against the label; any leftover
        // height is pushed below the knob instead, where it's invisible.
        knob->label.setBounds (slot.getX(), slot.getY(), knobW, 11);
        int sliderH = juce::jmin (slot.getHeight() - 11, knobW);
        knob->slider.setBounds (slot.getX(), slot.getY() + 11, knobW, sliderH);
    }
}

bool VisionMidiEditor::keyPressed (const juce::KeyPress& key) {
    // Invader mode claims Space for firing here; arrow keys are handled as
    // continuously-held state in timerCallback() instead (see there for
    // why), not as discrete keyPress events — but the discrete keyPress
    // still needs to be consumed (return true) rather than falling through
    // unhandled to AudioProcessorEditor::keyPressed(), which is exactly
    // what was making macOS play its system alert beep on every arrow
    // press: an unclaimed key event, not any sound Occultation itself was
    // making.
    if (audioProcessor.invaderModeEnabled.load()) {
        int code = key.getKeyCode();
        if (code == juce::KeyPress::spaceKey) {
            audioProcessor.fireBullet();
            return true;
        }
        if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey
            || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
            return true;
    }

    if (audioProcessor.wrapperType == juce::AudioProcessor::wrapperType_Standalone) {
        if (key.getKeyCode() == juce::KeyPress::spaceKey) {
            bool newState = !standalonePlayButton.getToggleState();
            standalonePlayButton.setToggleState (newState, juce::sendNotification);
            return true;
        }
    }

    // Arrow-key panning — a fixed step in normalized frame fractions for
    // the generic zoom, or in degrees (scaled by zoom, matching the drag
    // gesture's own scaling) for the sky map. Skipped entirely while
    // Invader mode is on, since arrow keys drive ship thrust instead (via
    // timerCallback()'s held-key poll) — this discrete keyPress would
    // otherwise ALSO fire once per OS key-repeat alongside that.
    int code = key.getKeyCode();
    if (! audioProcessor.invaderModeEnabled.load()
        && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey
            || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)) {
        // A raw screen-axis unit vector, un-rotated the same way a mouse
        // drag's pixel delta is (see unrotateScreenDelta) so an arrow key
        // still moves the view in the on-screen direction it names, rather
        // than the crop's pre-rotation axes, once the view is rotated.
        juce::Point<float> rawDir ((code == juce::KeyPress::leftKey) ? -1.0f : (code == juce::KeyPress::rightKey) ? 1.0f : 0.0f,
                                   (code == juce::KeyPress::upKey) ? -1.0f : (code == juce::KeyPress::downKey) ? 1.0f : 0.0f);
        juce::Point<float> dir = unrotateScreenDelta (rawDir);

        if (isSkyMapMode) {
            float zoom = audioProcessor.skyMapZoomDegPerPixel.load();
            float dec = audioProcessor.skyMapDecDegrees.load();
            float cosDec = std::max (0.15f, std::cos (dec * juce::MathConstants<float>::pi / 180.0f));
            float stepDegrees = 40.0f * zoom;   // 40px worth of the current view, same feel as a short drag
            audioProcessor.panSkyMap (dir.x * stepDegrees / cosDec, -dir.y * stepDegrees);
            updateSkyMapCoordinateDisplay();
        } else {
            float step = 0.08f / audioProcessor.viewZoom.load();
            audioProcessor.panView (dir.x * step, dir.y * step);
        }
        return true;
    }

    return juce::AudioProcessorEditor::keyPressed (key);
}

void VisionMidiEditor::mouseDown (const juce::MouseEvent& e) {
    if (! lastVideoBounds.contains (e.position))
        return;

    // Cmd+drag rotates instead of panning, in every mode (sky map included)
    // — checked first since it overrides the pan/RA-Dec-drag this same
    // gesture would otherwise start.
    if (e.mods.isCommandDown()) {
        isRotatingView = true;
        rotateDragStartPos = e.position;
        rotateDragStartDegrees = audioProcessor.viewRotationDegrees.load();
    } else if (isSkyMapMode) {
        isPanningSkyMap = true;
        skyMapDragStartPos = e.position;
        skyMapDragStartRaDegrees = audioProcessor.skyMapRaDegrees.load();
        skyMapDragStartDecDegrees = audioProcessor.skyMapDecDegrees.load();
    } else {
        isPanningView = true;
        viewDragStartPos = e.position;
        viewDragStartCenterX = audioProcessor.viewCenterX.load();
        viewDragStartCenterY = audioProcessor.viewCenterY.load();
    }
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void VisionMidiEditor::mouseDrag (const juce::MouseEvent& e) {
    if (isRotatingView) {
        // A genuine "grab and twist" gesture rather than a linear
        // pixels-to-degrees mapping: the rotation delta is the angle swept
        // between (video centre -> drag start) and (video centre -> current
        // position), so the point under the cursor at mouseDown keeps
        // tracking the cursor as the view turns, wherever on the video that
        // point was.
        juce::Point<float> centre = lastVideoBounds.getCentre();
        float startAngle   = std::atan2 (rotateDragStartPos.y - centre.y, rotateDragStartPos.x - centre.x);
        float currentAngle = std::atan2 (e.position.y - centre.y, e.position.x - centre.x);
        // Negated — the raw swept angle had the video turning opposite the
        // direction the cursor dragged (e.g. dragging down rotated it up).
        float deltaDegrees = -(currentAngle - startAngle) * (180.0f / juce::MathConstants<float>::pi);

        audioProcessor.viewRotationDegrees.store (rotateDragStartDegrees);
        audioProcessor.rotateViewBy (deltaDegrees);
    } else if (isPanningSkyMap) {
        // Anchored to the RA/Dec at mouseDown rather than accumulated per-
        // event, so the drag can't drift from the cursor over a long
        // gesture — the whole gesture is always "start position + total
        // pixel delta", not a chain of small deltas that could round
        // differently each time.
        float zoom = audioProcessor.skyMapZoomDegPerPixel.load();
        float dec = audioProcessor.skyMapDecDegrees.load();
        float cosDec = std::max (0.15f, std::cos (dec * juce::MathConstants<float>::pi / 180.0f));

        // Scale by the ratio of the renderer's actual frame width to the
        // on-screen video rect — lastVideoBounds may be smaller or larger
        // than the 1920px frame it displays.
        float pixelScale = (lastVideoBounds.getWidth() > 0.0f)
                                ? (float) VisionMidiProcessor::skyMapFrameWidth / lastVideoBounds.getWidth()
                                : 1.0f;

        // Un-rotate in raw screen pixels (uniform units) before splitting
        // into RA/Dec — see unrotateScreenDelta.
        juce::Point<float> rawDelta = e.position - skyMapDragStartPos;
        juce::Point<float> delta = unrotateScreenDelta (rawDelta);
        float deltaRaDegrees  = -(delta.x * pixelScale * zoom) / cosDec;
        float deltaDecDegrees =  (delta.y * pixelScale * zoom);

        audioProcessor.setSkyMapView (skyMapDragStartRaDegrees + deltaRaDegrees,
                                      skyMapDragStartDecDegrees + deltaDecDegrees,
                                      zoom);
        updateSkyMapCoordinateDisplay();
    } else if (isPanningView) {
        // Same anchored-drag pattern, in normalized 0..1 frame fractions
        // instead of RA/Dec degrees. Dragging right reveals content that
        // was further right, i.e. the center moves left — hence the minus.
        // Un-rotate in raw screen pixels (uniform units) before normalizing
        // each axis by its own (possibly different) width/height — see
        // unrotateScreenDelta.
        juce::Point<float> rawDelta = e.position - viewDragStartPos;
        juce::Point<float> delta = unrotateScreenDelta (rawDelta);
        float dCenterX = -(delta.x / lastVideoBounds.getWidth())  / audioProcessor.viewZoom.load();
        float dCenterY = -(delta.y / lastVideoBounds.getHeight()) / audioProcessor.viewZoom.load();

        audioProcessor.viewCenterX.store (viewDragStartCenterX);
        audioProcessor.viewCenterY.store (viewDragStartCenterY);
        audioProcessor.panView (dCenterX, dCenterY);
    }
}

void VisionMidiEditor::mouseUp (const juce::MouseEvent&) {
    if (isPanningSkyMap || isPanningView || isRotatingView) {
        isPanningSkyMap = false;
        isPanningView = false;
        isRotatingView = false;
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }
}

void VisionMidiEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    if (! lastVideoBounds.contains (e.position))
        return;

    // Exponential so the zoom feels the same (a fixed percentage per notch)
    // whether already deep-zoomed-in or zoomed-out.
    float factor = std::pow (0.85f, wheel.deltaY * 4.0f);
    if (isSkyMapMode) {
        audioProcessor.setSkyMapZoom (audioProcessor.skyMapZoomDegPerPixel.load() * factor);
        updateSkyMapCoordinateDisplay();
    } else {
        zoomBy (1.0f / factor);   // skyMap's factor shrinks the field to zoom in; viewZoom instead grows to zoom in
    }
}

void VisionMidiEditor::mouseMagnify (const juce::MouseEvent& e, float scaleFactor) {
    if (! lastVideoBounds.contains (e.position))
        return;

    // scaleFactor > 1 means the pinch spread the fingers apart (macOS's
    // magnify-content convention) — zoom in either way.
    if (isSkyMapMode) {
        audioProcessor.setSkyMapZoom (audioProcessor.skyMapZoomDegPerPixel.load() / juce::jmax (0.01f, scaleFactor));
        updateSkyMapCoordinateDisplay();
    } else {
        zoomBy (scaleFactor);
    }
}

void VisionMidiEditor::zoomBy (float factor) {
    audioProcessor.setViewZoom (audioProcessor.viewZoom.load() * factor);
}

juce::Point<float> VisionMidiEditor::unrotateScreenDelta (juce::Point<float> delta) const {
    // Inverse of the rotation run() applies to the crop before display (see
    // its own comment on the matching getRotationMatrix2D/warpAffine call):
    // that forward transform is (cosθ·x+sinθ·y, -sinθ·x+cosθ·y), and since
    // it's a pure rotation (orthonormal), its inverse is its transpose,
    // (cosθ·x-sinθ·y, sinθ·x+cosθ·y). Without this, dragging (or an arrow
    // key) moved the view along the CROP's pre-rotation axes rather than
    // the axes the cursor/key actually appear to move along on screen once
    // the display itself is rotated.
    float rotationRad = audioProcessor.viewRotationDegrees.load() * juce::MathConstants<float>::pi / 180.0f;
    float cosR = std::cos (rotationRad), sinR = std::sin (rotationRad);
    return { cosR * delta.x - sinR * delta.y, sinR * delta.x + cosR * delta.y };
}

void VisionMidiEditor::updateAvailableSources() {
    cameraSelector.clear();
    cameraSelector.addItem ("Source...", 1);
    cameraSelector.addItem ("Cam 0", 2);
    cameraSelector.addItem ("Cam 1", 3);
    cameraSelector.addItem ("Cam 2", 4);

    juce::String customPath = audioProcessor.getLastFilePath();
    bool isStreamUrl = customPath.contains ("://");
    if (customPath.isNotEmpty() && (isStreamUrl || juce::File (customPath).existsAsFile())) {
        cameraSelector.addSeparator();
        juce::String label = isStreamUrl ? ("Stream: " + customPath)
                                          : ("File: " + juce::File (customPath).getFileName());
        cameraSelector.addItem (label, 999);
    }
    cameraSelector.addSeparator();
    cameraSelector.addItem ("Sky Map", 997);
    cameraSelector.addItem ("Open File...", 1000);
    cameraSelector.addItem ("Network Stream...", 1001);
}

void VisionMidiEditor::loadCustomFile() {
    showFileBrowserOverlay();
}

void VisionMidiEditor::loadNetworkStream() {
    juce::String lastPath = audioProcessor.getLastFilePath();
    streamUrlEditor.setText (lastPath.contains ("://") ? lastPath : juce::String ("rtsp://"), juce::dontSendNotification);
    showStreamOverlay();
}

void VisionMidiEditor::showFileBrowserOverlay() {
    // Created on first use, not in the constructor — see the comment there.
    if (fileBrowserOverlay == nullptr) {
        fileBrowserOverlay = std::make_unique<juce::FileBrowserComponent> (
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            juce::File::getSpecialLocation (juce::File::userHomeDirectory),
            &fileBrowserFilter, nullptr);
        addChildComponent (*fileBrowserOverlay);
    }
    fileBrowserOverlay->setVisible (true);
    fileBrowserChooseButton.setVisible (true);
    fileBrowserCancelButton.setVisible (true);
    layoutOverlays();
    fileBrowserOverlay->toFront (false);
    fileBrowserChooseButton.toFront (false);
    fileBrowserCancelButton.toFront (false);
    repaint();
}

void VisionMidiEditor::hideFileBrowserOverlay() {
    fileBrowserOverlay->setVisible (false);
    fileBrowserChooseButton.setVisible (false);
    fileBrowserCancelButton.setVisible (false);
    // The dropdown may still be showing "Open File..." (a cancel, or no
    // selection) — restore it to whatever source is actually active.
    updateAvailableSources();
    repaint();
}

void VisionMidiEditor::showStreamOverlay() {
    streamPromptLabel.setVisible (true);
    streamUrlEditor.setVisible (true);
    streamConnectButton.setVisible (true);
    streamCancelButton.setVisible (true);
    layoutOverlays();
    streamPromptLabel.toFront (false);
    streamUrlEditor.toFront (true);
    streamConnectButton.toFront (false);
    streamCancelButton.toFront (false);
    repaint();
}

void VisionMidiEditor::hideStreamOverlay() {
    streamPromptLabel.setVisible (false);
    streamUrlEditor.setVisible (false);
    streamConnectButton.setVisible (false);
    streamCancelButton.setVisible (false);
    updateAvailableSources();
    repaint();
}

void VisionMidiEditor::layoutOverlays() {
    // A centred box over the whole editor — plain child Components, not a
    // second top-level window (see the header comment on these methods).
    auto full = getLocalBounds();

    // resized() runs synchronously from the early setSize() call in the
    // constructor, well before fileBrowserOverlay is constructed further
    // down (it's built lazily on first "Open File..." use) — guard this
    // block only, rather than bailing out of the whole function, which
    // used to also skip laying out the network-stream overlay below
    // whenever the file browser hadn't been created yet.
    if (fileBrowserOverlay != nullptr && fileBrowserOverlay->isVisible()) {
        auto box = full.withSizeKeepingCentre (juce::jmin (560, full.getWidth() - 40),
                                                juce::jmin (460, full.getHeight() - 40));
        auto buttonRow = box.removeFromBottom (32);
        fileBrowserCancelButton.setBounds (buttonRow.removeFromRight (90));
        buttonRow.removeFromRight (8);
        fileBrowserChooseButton.setBounds (buttonRow.removeFromRight (90));
        box.removeFromBottom (6);
        fileBrowserOverlay->setBounds (box);
    }

    if (streamPromptLabel.isVisible()) {
        auto box = full.withSizeKeepingCentre (juce::jmin (480, full.getWidth() - 40), 110);
        streamPromptLabel.setBounds (box.removeFromTop (20));
        box.removeFromTop (8);
        streamUrlEditor.setBounds (box.removeFromTop (26));
        box.removeFromTop (10);
        auto buttonRow = box.removeFromTop (28);
        streamCancelButton.setBounds (buttonRow.removeFromRight (90));
        buttonRow.removeFromRight (8);
        streamConnectButton.setBounds (buttonRow.removeFromRight (90));
    }
}

void VisionMidiEditor::adoptFile (const juce::File& file) {
    adoptSource (file.getFullPathName());
}

void VisionMidiEditor::adoptSource (const juce::String& path) {
    audioProcessor.adoptSource (path);
    updateAvailableSources();
    cameraSelector.setSelectedId (999, juce::dontSendNotification);
}

static bool isSupportedMediaFile (const juce::File& file)
{
    static const juce::StringArray extensions {
        ".mp4", ".avi", ".mov", ".m4v", ".jpg", ".jpeg", ".png", ".fits", ".fit", ".fts"
    };
    return extensions.contains (file.getFileExtension().toLowerCase());
}

bool VisionMidiEditor::isInterestedInFileDrag (const juce::StringArray& files) {
    for (auto& f : files)
        if (isSupportedMediaFile (juce::File (f)))
            return true;
    return false;
}

void VisionMidiEditor::fileDragEnter (const juce::StringArray& files, int, int) {
    isDraggingValidFile = isInterestedInFileDrag (files);
    repaint();
}

void VisionMidiEditor::fileDragExit (const juce::StringArray&) {
    isDraggingValidFile = false;
    repaint();
}

void VisionMidiEditor::filesDropped (const juce::StringArray& files, int, int) {
    isDraggingValidFile = false;
    for (auto& f : files) {
        juce::File file (f);
        if (isSupportedMediaFile (file) && file.existsAsFile()) {
            adoptFile (file);
            break;
        }
    }
    repaint();
}

void VisionMidiEditor::timerCallback() {
    // Keep the preset list live rather than frozen at the moment the editor
    // was constructed: presets can appear from another plugin instance, a
    // second window, or the user dropping a file into the presets folder.
    // Rebuilt only when the names actually changed, and never while the
    // popup is open (that would yank the list out from under the mouse).
    if (++presetRefreshCounter >= 60) {          // ~2s at 30Hz
        presetRefreshCounter = 0;
        if (! presetSelector.isPopupActive()) {
            auto names = audioProcessor.getAllPresetNames();
            if (names != lastSeenPresetNames) {
                lastSeenPresetNames = names;
                refreshPresetList (audioProcessor.getCurrentPresetName());
            }
        }
    }

    // Invader mode's ship thrust is polled as held-key STATE here, once per
    // tick, rather than applied as a fixed step from discrete keyPress
    // events — isKeyCurrentlyDown lets any combination of arrows be held
    // at once (so run()'s physics can end up pointing the ship at any
    // angle, not just the 4/8 a single keypress could produce), and a
    // continuous poll is what makes thrust stop the instant a key's
    // released rather than waiting for the OS's key-repeat to catch up.
    if (audioProcessor.invaderModeEnabled.load()) {
        // Left/right steer, up/down throttle — not four directions of push.
        // Up is "faster" and down is "slower", so the sign is flipped from
        // the screen-axis convention the panning keys use below.
        float steer = 0.0f, throttle = 0.0f;
        if (juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::leftKey))  steer    -= 1.0f;
        if (juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::rightKey)) steer    += 1.0f;
        if (juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::upKey))    throttle += 1.0f;
        if (juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::downKey))  throttle -= 1.0f;
        audioProcessor.setShipControls (steer, throttle);

        // Show the running streak on the checkbox itself, so the transposition
        // you're hearing has a visible cause. Only touched when the number
        // changes — this runs 30 times a second.
        if (invaderStreakToggle.getToggleState()) {
            const int streak = audioProcessor.invaderStreak.load();
            if (streak != lastShownInvaderStreak) {
                lastShownInvaderStreak = streak;
                invaderStreakToggle.setButtonText (streak > 0 ? "Streak Mod. x" + juce::String (streak)
                                                             : "Streak Mod.");
            }
        } else if (lastShownInvaderStreak != -1) {
            lastShownInvaderStreak = -1;
            invaderStreakToggle.setButtonText ("Streak Mod.");
        }
    }

    // Full rainbow sweep roughly every 2 seconds at this 30Hz timer — quite
    // fast, deliberately much quicker than the ember-circle overlay's 10s.
    titleHue = std::fmod (titleHue + 1.0f / 60.0f, 1.0f);

    // Full transit across the title roughly every 8 seconds at this 30Hz timer.
    moonPhase += 1.0f / 240.0f;
    if (moonPhase > 1.0f) {
        moonPhase -= 1.0f;
        // A fresh oblique route each pass, so it isn't the same diagonal
        // every single time — the very first pass uses the 73 degree start.
        moonAngleDeg = juce::Random::getSystemRandom().nextFloat() * 50.0f - 25.0f;
    }

    if (audioProcessor.wrapperType == juce::AudioProcessor::wrapperType_Standalone) {
        bool hostPlayingNow = audioProcessor.standalonePlaying.load();
        if (hostPlayingNow != lastKnownStandalonePlaying) {
            lastKnownStandalonePlaying = hostPlayingNow;
            // Only reacts when the atomic changed for a reason other than this
            // button's own click (e.g. an incoming MIDI Start/Stop from
            // PluginProcessor::processBlock) — the button is already in sync
            // when the click was the cause, so this stays a no-op then.
            if (standalonePlayButton.getToggleState() != hostPlayingNow)
                standalonePlayButton.setToggleState (hostPlayingNow, juce::sendNotification);
        }

        if (hostPlayingNow) {
            double rawPpq = audioProcessor.standalonePpqPosition.load() + (2.0 / 30.0);
            // Was hardcoded to 16 regardless of the Steps selector — see the
            // matching fix in PluginProcessor::processBlock for why that
            // broke anything other than the default 16-step sequence.
            double loopBeats = audioProcessor.sequenceLoopBeats.load();
            if (rawPpq >= loopBeats) rawPpq = std::fmod (rawPpq, loopBeats);
            audioProcessor.standalonePpqPosition.store (rawPpq);
        }
    }

    // Reflects the auto-computed threshold back onto the slider — the
    // processor only ever touches its own atomics, never this Component, so
    // this poll is the one place that crosses back into UI territory.
    if (audioProcessor.isAutoThresholdEnabled.load()) {
        double t = (double) audioProcessor.detectionThreshold.load();
        if (std::abs (thresholdSlider.getValue() - t) > 0.5)
            thresholdSlider.setValue (t, juce::dontSendNotification);
    }

    int currentNote = audioProcessor.lastPlayedNote.load();
    static int displayedNote = -2;
    if (currentNote != displayedNote) {
        displayedNote = currentNote;
        if (currentNote >= 0) {
            statusLabel.setText ("Note: " + juce::MidiMessage::getMidiNoteName (currentNote, true, true, 3) + " (" + juce::String(currentNote) + ")", juce::dontSendNotification);
        }
    }

    // Synth Type presets change the "waveform" parameter out from under the
    // UI (see PluginProcessor::applyParametersToSynth) — the icon selector
    // has no attachment to follow that automatically, so poll it here.
    // (startTimerHz() only starts after setupSynthPanel() has already
    // constructed waveformSelector, but null-check anyway rather than rely
    // on that ordering never changing.)
    if (waveformSelector != nullptr) {
        if (auto* waveformParam = dynamic_cast<juce::AudioParameterChoice*> (audioProcessor.apvts.getParameter ("waveform")))
            waveformSelector->setSelectedIndex (waveformParam->getIndex());
    }

    updateSkyMapCoordinateDisplay();

    repaint();
}

void VisionMidiEditor::drawLevelMeter (juce::Graphics& g, juce::Rectangle<float> bounds, float level, float peak)
{
    using namespace OccultPalette;

    g.setColour (panelDeep);
    g.fillRoundedRectangle (bounds, 2.0f);

    g.setColour (silverEdgeDim);
    g.drawRoundedRectangle (bounds, 2.0f, 1.0f);

    float activeH = bounds.getHeight() * level;
    if (activeH > 1.0f) {
        juce::Rectangle<float> activeMeter (bounds.getX() + 1.0f, bounds.getBottom() - activeH, bounds.getWidth() - 2.0f, activeH);

        // Rises from a smouldering dried-blood base into a hellfire flare —
        // the meter itself reads as an ember being fanned back to life.
        juce::ColourGradient grad (
            bloodRed, activeMeter.getX(), activeMeter.getBottom(),
            hellfireOrange, activeMeter.getX(), activeMeter.getY(), false
        );
        g.setGradientFill (grad);
        g.fillRect (activeMeter);
    }

    if (peak > 0.02f) {
        float peakY = bounds.getBottom() - (bounds.getHeight() * peak);
        g.setColour (emberGlow);
        g.drawHorizontalLine ((int)peakY, bounds.getX(), bounds.getRight());
    }
}

// Pitch runs left (low) to right (high) across columns; time runs top to
// bottom — a note is born at the playhead (the top edge) and scrolls
// downward as it ages, exiting off the bottom once it falls outside
// audioProcessor.sequenceLoopBeats (the sequencer's current loop length —
// see the Steps selector). (Rotated 90° from the roll's previous
// right-to-left scroll: same "born at the playhead, ages away from it"
// logic, just with the playhead moved from the right edge to the top edge.)
void VisionMidiEditor::drawPianoRoll (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    using namespace OccultPalette;

    g.setColour (panelDeep);
    g.fillRect (bounds);

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (bounds.toNearestInt());

    int minNote = audioProcessor.currentRootNote.load();
    int maxNote = minNote + 24;

    float laneWidth = bounds.getWidth() / (float)(maxNote - minNote);
    for (int n = minNote; n < maxNote; ++n) {
        float x = bounds.getX() + (n - minNote) * laneWidth;

        bool isAccidental = juce::MidiMessage::isMidiNoteBlack (n);
        if (isAccidental) {
            g.setColour (bloodRed.withAlpha (0.16f));
            g.fillRect (x, bounds.getY(), laneWidth, bounds.getHeight());
        }

        g.setColour (silverEdgeDim.withAlpha (0.5f));
        g.drawVerticalLine ((int) x, bounds.getY(), bounds.getBottom());
    }

    const juce::ScopedLock sl (audioProcessor.pianoRollLock);

    double continuousPpq = audioProcessor.continuousPpq.load();

    // The playhead ("now") sits at the top edge; age scrolls notes downward.
    float playheadY = bounds.getY();

    for (const auto& note : audioProcessor.pianoRollNotes)
    {
        if (note.noteNumber < minNote || note.noteNumber >= maxNote)
            continue;

        double endPpqVal = (note.releasePpq < 0) ? continuousPpq : note.releasePpq;
        double loopBeats = audioProcessor.sequenceLoopBeats.load();

        // y2 is the note's newer (top) edge — at the playhead itself while
        // still sounding — y1 its older (bottom) edge, further down the
        // more time has passed since it triggered.
        float y2 = playheadY + (float)((continuousPpq - endPpqVal) / loopBeats) * bounds.getHeight();
        float y1 = playheadY + (float)((continuousPpq - note.triggerPpq) / loopBeats) * bounds.getHeight();

        if (y2 > bounds.getBottom() && y1 > bounds.getBottom()) continue;

        float noteH = std::max (3.0f, y1 - y2);
        float noteX = bounds.getX() + (note.noteNumber - minNote) * laneWidth;

        juce::Rectangle<float> noteRect (noteX + 1.0f, y2, laneWidth - 2.0f, noteH);

        // Blazing star trail fading out toward the playhead (behind the
        // moving note, i.e. above it, since the note is falling away downward)
        float trailLen = 50.0f;
        float trailStartY = y2;
        float trailEndY = std::max (playheadY, y2 - trailLen);

        if (trailStartY > trailEndY) {
            juce::Rectangle<float> trailRect (noteX + 2.0f, trailEndY, laneWidth - 4.0f, trailStartY - trailEndY);
            juce::ColourGradient trailGrad (
                note.colour.withAlpha (0.0f), noteX + laneWidth * 0.5f, trailEndY,
                note.colour.brighter (0.8f).withAlpha (0.8f), noteX + laneWidth * 0.5f, trailStartY, false
            );
            g.setGradientFill (trailGrad);
            g.fillRoundedRectangle (trailRect, 1.0f);
        }

        // Main Note Body
        juce::ColourGradient fillGrad (
            note.colour.brighter (0.2f), noteRect.getX(), noteRect.getY(),
            note.colour.darker (0.2f), noteRect.getRight(), noteRect.getBottom(), false
        );
        g.setGradientFill (fillGrad);
        g.fillRoundedRectangle (noteRect, 1.5f);

        g.setColour (boneWhite.withAlpha (0.25f));
        g.drawRoundedRectangle (noteRect, 1.5f, 1.0f);
    }

    // Playhead line locked at the top edge, with a soft ember bloom falling away below it
    juce::Rectangle<float> glow (bounds.getX(), playheadY, bounds.getWidth(), 6.0f);
    g.setGradientFill (juce::ColourGradient (hellfireOrange.withAlpha (0.5f), 0.0f, glow.getY(),
                                              hellfireOrange.withAlpha (0.0f), 0.0f, glow.getBottom(), false));
    g.fillRect (glow);

    g.setColour (emberGlow);
    g.fillRect (bounds.getX(), playheadY, bounds.getWidth(), 2.0f);
}

void VisionMidiEditor::paint (juce::Graphics& g)
{
    using namespace OccultPalette;

    g.fillAll (voidBlack);

    // A dying vignette — the light gutters out toward the corners
    {
        float cx = (float)getWidth() * 0.5f;
        float cy = (float)getHeight() * 0.5f;
        juce::ColourGradient vignette (juce::Colours::transparentBlack, cx, cy,
                                        juce::Colours::black.withAlpha (0.75f), 0.0f, 0.0f, true);
        vignette.addColour (0.65, juce::Colours::transparentBlack);
        g.setGradientFill (vignette);
        g.fillAll();
    }

    // Left Controls. Height stops above the synth panel at the bottom of
    // the window (leaving it the same 45px clearance the panel itself
    // always had below IT) rather than the old fixed getHeight()-45 — that
    // used to reach far past the sidebar's actual content and straight
    // through the synth panel's Detune/Volume knobs below it, since it
    // predates the synth panel's own 150px strip at the bottom: its right
    // edge showed up as a stray vertical line cutting through those knobs.
    float leftPanelHeight = (float) (getHeight() - 45 - synthPanelHeight);
    g.setColour (panel);
    g.fillRoundedRectangle (10.0f, 10.0f, 135.0f, leftPanelHeight, 6.0f);
    g.setColour (silverEdge);
    g.drawRoundedRectangle (10.0f, 10.0f, 135.0f, leftPanelHeight, 6.0f, 1.0f);

    // The title, with its own namesake animation: a 100px dark moon transits
    // across the letters — drawn after the text, in the same call, so it
    // truly occults them rather than sitting behind via child-paint order.
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (juce::Rectangle<int> (10, 10, 135, (int) leftPanelHeight));

        g.setColour (juce::Colour::fromHSV (titleHue, 0.8f, 1.0f, 1.0f));
        juce::Font titleFont (14.0f, juce::Font::bold);
        titleFont.setExtraKerningFactor (0.12f);
        g.setFont (titleFont);
        g.drawText ("OCCULTATION", titleBounds, juce::Justification::centred, true);

        float moonRadius = 13.0f;   // halved along with titleZoneHeight, below
        float trackStart = (float) titleBounds.getX() - moonRadius;
        float trackEnd = (float) titleBounds.getRight() + moonRadius;
        float horizontalDist = trackEnd - trackStart;
        float moonCX = trackStart + moonPhase * horizontalDist;

        // Oblique route: a straight diagonal through the title's vertical
        // centre, sloped by moonAngleDeg — a fresh angle gets picked each
        // pass in timerCallback() so the sweep doesn't repeat identically.
        float angleRad = juce::degreesToRadians (moonAngleDeg);
        float moonCY = (float) titleBounds.getCentreY() + std::tan (angleRad) * (moonPhase - 0.5f) * horizontalDist;

        juce::ColourGradient moonShade (juce::Colour (0xff020203), moonCX - moonRadius * 0.4f, moonCY - moonRadius * 0.4f,
                                          juce::Colour (0xff1e1f21), moonCX + moonRadius * 0.6f, moonCY + moonRadius * 0.6f, false);
        g.setGradientFill (moonShade);
        g.fillEllipse (moonCX - moonRadius, moonCY - moonRadius, moonRadius * 2.0f, moonRadius * 2.0f);

        g.setColour (juce::Colours::black);
        g.drawEllipse (moonCX - moonRadius, moonCY - moonRadius, moonRadius * 2.0f, moonRadius * 2.0f, 1.5f);
    }


    auto mainArea = getLocalBounds();
    mainArea.removeFromLeft (155);
    mainArea.removeFromRight (15);
    mainArea.removeFromTop (10);
    // Down to just above the synth panel's Preset row — the status label no
    // longer sits in between, so nothing is reserved for it here.
    mainArea.removeFromBottom (bottomChromeHeight);

    const juce::ScopedLock sl (audioProcessor.imageLock);
    if (audioProcessor.juceImage.isValid())
    {
        const juce::Image& juceImage = audioProcessor.juceImage;
        float iW = (float)juceImage.getWidth();
        float iH = (float)juceImage.getHeight();
        float imgAspect = iW / iH;
        cachedImageAspect = imgAspect;

        float meterWidth = 10.0f;
        float meterGap = 4.0f;
        float rollGap = 8.0f;   // gap between the video row and the piano roll below it

        // The video plus the piano roll strip below it (kRollToVideoRatio of
        // the video's own height) always fill the full available height
        // between them — no letterboxing above/below — and the window's
        // WIDTH grows to accommodate that instead of the video shrinking to
        // fit a narrower window. videoH + rollGap + videoH*kRollToVideoRatio
        // = mainArea.getHeight().
        float availW = (float) mainArea.getWidth() - meterGap - meterWidth;
        float availH = (float) mainArea.getHeight() - rollGap;

        float videoH = availH / (1.0f + kRollToVideoRatio);
        float videoW = videoH * imgAspect;

        // Deriving the width from the height alone assumes the window will
        // be widened to match (see the setSize below). A plugin host is free
        // to hand us any size it likes and refuse that request, and then the
        // video simply overflowed the space available and got clipped. Fit
        // it inside both axes instead, so the whole image is always visible
        // whatever shape the host picks — letterboxed rather than cropped.
        if (videoW > availW && availW > 0.0f) {
            videoW = availW;
            videoH = videoW / imgAspect;
        }
        // The roll takes ALL the height left over beside the video, rather
        // than a fixed ratio of it. When the video is width-limited (a host
        // window narrower than the source's aspect wants, or any non-ideal
        // shape) the ratio left a block shorter than mainArea, which was then
        // centred — the black bands above and below the roll. Absorbing the
        // remainder here means video + gap + roll always fills the area
        // exactly, and the roll simply grows a little in a narrow window.
        float rollHeight = juce::jmax (availH * 0.12f, availH - videoH);

        // Interactive drag-resizing is kept on-aspect live by the
        // EditorAspectConstrainer (see the constructor), so no per-frame
        // correction is needed here. But switching to a source with a
        // different aspect ratio changes the ideal width without any drag
        // happening at all — catch that once, right when it happens, instead
        // of leaving a stale-width gap next to the video. Skipped while a
        // drag is live: posting an async setSize mid-drag was fighting the
        // constrainer's own bounds for every single one of paint()'s 30
        // calls/sec, which is what made the window feel like it was
        // snapping/hitching independent of the corner-tracking bug itself.
        // Standalone only: there the window is ours, so snapping its width to
        // the source's aspect keeps the video edge-to-edge with no dead strip
        // beside it. In a plugin the host owns the window — asking it to
        // resize on every source change fights whatever layout it has chosen
        // (and some hosts simply refuse), so there we just fit into the size
        // we're given, which the letterbox maths above now guarantees.
        if (audioProcessor.wrapperType == juce::AudioProcessor::wrapperType_Standalone
            && ! liveDragging && std::abs (imgAspect - lastAppliedImageAspect) > 0.001f) {
            lastAppliedImageAspect = imgAspect;
            int idealWidth = idealWidthForHeight (getHeight());
            if (std::abs (getWidth() - idealWidth) > 1) {
                juce::MessageManager::callAsync ([this, idealWidth] {
                    setSize (idealWidth, getHeight());
                });
            }
        }

        // Centred in whatever space is left over, so a host-imposed shape
        // letterboxes symmetrically instead of pinning the video to a corner.
        // Top-aligned: the block fills the area's height by construction now,
        // so there is nothing left to centre within.
        float sharedY = (float) mainArea.getY();
        float videoX  = (float) mainArea.getX() + (availW - videoW) * 0.5f;

        // Video bounds matched to aspect scale
        juce::Rectangle<float> videoBounds (videoX, sharedY, videoW, videoH);
        lastVideoBounds = videoBounds;

        // "Jump to constellation" picker, floated centred over the video's
        // own bottom edge. Positioned here rather than in resized() because
        // videoBounds only exists at all once the image's aspect ratio is
        // known — recomputing the same rectangle every paint() is harmless,
        // JUCE no-ops setBounds() when it hasn't actually changed.
        if (isSkyMapMode) {
            int pickerW = 170, pickerH = 24;
            constellationSelector.setBounds ((int) (videoBounds.getCentreX() - pickerW * 0.5f),
                                             (int) (videoBounds.getBottom() - pickerH - 10.0f),
                                             pickerW, pickerH);
        }

        // Zoom +/-/rotate trio, floated in the video's bottom-right corner in
        // every mode — same reasoning as the picker above for why this is
        // set here rather than in resized().
        {
            int btnSize = 24, gap = 4;
            zoomOutButton.setBounds ((int) (videoBounds.getRight() - btnSize - 10.0f),
                                     (int) (videoBounds.getBottom() - btnSize - 10.0f),
                                     btnSize, btnSize);
            zoomInButton.setBounds (zoomOutButton.getX() - btnSize - gap, zoomOutButton.getY(),
                                    btnSize, btnSize);
            int rotateBtnWidth = 40;
            rotateButton.setBounds (zoomInButton.getX() - rotateBtnWidth - gap, zoomOutButton.getY(),
                                    rotateBtnWidth, btnSize);
        }

        // Level meter beside the video, spanning just its height.
        juce::Rectangle<float> meterBounds (videoBounds.getRight() + meterGap, sharedY, meterWidth, videoH);

        // Piano roll now sits below the video+meter row, spanning their
        // combined width, at kRollToVideoRatio of the video's own height.
        juce::Rectangle<float> pianoRollBounds (videoX, videoBounds.getBottom() + rollGap,
                                                videoW + meterGap + meterWidth, rollHeight);

        juce::Rectangle<float> combinedBounds = videoBounds.getUnion (pianoRollBounds);
        g.setColour (panelDeep);
        g.fillRoundedRectangle (combinedBounds, 4.0f);

        // Draw feed image stretched precisely into its pixel-matched bounding box
        g.drawImage (juceImage, videoBounds, juce::RectanglePlacement::stretchToFit);

        // Render sliding piano roll
        drawPianoRoll (g, pianoRollBounds);

        // Draw outer frame around image & roll
        g.setColour (silverEdge);
        g.drawRoundedRectangle (combinedBounds, 4.0f, 1.0f);

        // Level meter indicator
        drawLevelMeter (g, meterBounds, audioProcessor.currentMidiLevel.load(), audioProcessor.midiPeakLevel.load());
        zoomInButton.setVisible (true);
        zoomOutButton.setVisible (true);
        rotateButton.setVisible (true);
    }
    else
    {
        g.setColour (ashGrey.withAlpha (0.5f));
        g.setFont (14.0f);
        g.drawText ("Awaiting the Feed...", mainArea, juce::Justification::centred, true);
        zoomInButton.setVisible (false);
        zoomOutButton.setVisible (false);
        rotateButton.setVisible (false);
    }

    // One frame around the whole Waveform icon-button row, matching the
    // Synth Type box beside it (which gets its outline for free from
    // ComboBox's own LookAndFeel) — each icon button already draws its own
    // small cell border, but the group itself had none, so it read as a
    // loose row of buttons floating next to a boxed dropdown instead of two
    // equally-weighted controls.
    if (waveformSelector != nullptr) {
        auto waveformBox = waveformSelector->getBounds().toFloat().expanded (4.0f);
        g.setColour (panel);
        g.fillRoundedRectangle (waveformBox, 6.0f);
        g.setColour (silverEdge);
        g.drawRoundedRectangle (waveformBox, 6.0f, 1.0f);
    }

    // Synth panel's own output-level meter — bounds set in layoutSynthPanel().
    if (! audioLevelMeterBounds.isEmpty())
        drawLevelMeter (g, audioLevelMeterBounds, audioProcessor.currentAudioLevel.load(), audioProcessor.audioPeakLevel.load());

    if (isDraggingValidFile) {
        auto bounds = getLocalBounds().toFloat().reduced (4.0f);
        g.setColour (hellfireOrange.withAlpha (0.08f));
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (hellfireOrange);
        g.drawRoundedRectangle (bounds, 6.0f, 2.5f);
        g.setFont (juce::Font (16.0f, juce::Font::bold));
        g.drawText ("Drop to load media", getLocalBounds(), juce::Justification::centred, true);
    }

    bool fileBrowserShowing = fileBrowserOverlay != nullptr && fileBrowserOverlay->isVisible();
    if (fileBrowserShowing || streamPromptLabel.isVisible()) {
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRect (getLocalBounds());
        auto box = (fileBrowserShowing ? fileBrowserOverlay->getBounds().expanded (10)
                                        : streamPromptLabel.getBounds().getUnion (streamConnectButton.getBounds()).expanded (16));
        g.setColour (panel);
        g.fillRoundedRectangle (box.toFloat(), 6.0f);
        g.setColour (silverEdge);
        g.drawRoundedRectangle (box.toFloat(), 6.0f, 1.0f);
    }
}

// Mirrors paint()'s geometry (mainArea insets of 155/15/10/35, an 8px gap
// above the piano roll, a 10px meter + 4px gap beside the video) but solved
// the other way round: given a height, what total window width makes the
// video exactly fill mainArea with zero leftover gap?
int VisionMidiEditor::idealWidthForHeight (int height) const
{
    // Reads the cached aspect ratio rather than locking imageLock directly —
    // the live resize constrainer calls this on every drag event, and
    // blocking the message thread on a lock the capture thread periodically
    // holds is exactly what made shrinking the window feel like it hitched.
    float imgAspect = cachedImageAspect;

    // Vertical chrome is 10 at the top plus bottomChromeHeight beneath (synth
    // panel + status strip), and an 8px gap between video and roll; the rest
    // splits into video + roll at kRollToVideoRatio — see paint(), which only
    // departs from that ratio when the window is too narrow to honour it.
    float availableH = (float) juce::jmax (1, height - 10 - bottomChromeHeight - 8);
    float videoH = availableH / (1.0f + kRollToVideoRatio);
    float videoW = videoH * imgAspect;

    // W = 155(left) + videoW + 4(meterGap) + 10(meterWidth) + 15(right)
    float totalW = 184.0f + videoW;

    return juce::jlimit (600, 1920, juce::roundToInt (totalW));
}

// The inverse of idealWidthForHeight(): given a width, what height makes
// that the exact ideal width? idealWidthForHeight is monotonically
// increasing in height (taller video -> wider window), so a binary search
// over its range finds the answer without duplicating the formula above —
// the two directions can't drift apart from each other this way.
int VisionMidiEditor::idealHeightForWidth (int width) const
{
    int lo = 400, hi = 1200;
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        if (idealWidthForHeight (mid) <= width) lo = mid;
        else                                    hi = mid;
    }
    return lo;
}

void VisionMidiEditor::resized() {
    int w = getWidth();
    int h = getHeight();

    int leftX = 16;
    int controlW = 123;
    int y = 16;

    // Reserves a tall band for the title so the moon has room to transit
    // across it without overlapping cameraSelector below. Halved from the
    // original 108 — the moon (see paint()) is halved right along with it.
    constexpr int titleZoneHeight = 54;
    // Full panel width, not controlW — "OCCULTATION" at 14pt bold with the
    // kerning below doesn't fit in the narrower dropdown-aligned width and
    // was silently ellipsing to "OCCULTATI...".
    titleBounds = { 10, y, 135, titleZoneHeight };
    y += titleZoneHeight + 8;

    // Each dropdown gets a small caption above it, then the control itself.
    auto layoutCaptioned = [&] (juce::Label& caption, juce::Component& control, int trailingGap) {
        caption.setBounds (leftX, y, controlW, 11); y += 12;
        control.setBounds (leftX, y, controlW, 24); y += 24 + trailingGap;
    };
    layoutCaptioned (sourceCaption, cameraSelector, 6);
    layoutCaptioned (stepsCaption, stepsSelector, 6);
    layoutCaptioned (scaleCaption, scaleSelector, 8);
    layoutCaptioned (occultingObjectCaption, occultingObjectSelector, 6);

    int colW = 58;
    int gap = 7;
    int sliderH = 150;

    rootNoteSlider.setBounds (leftX, y, colW, sliderH);
    thresholdSlider.setBounds (leftX + colW + gap, y, colW, sliderH);
    y += sliderH + 2;

    // Labels below their sliders, not above.
    rootNoteLabel.setBounds (leftX, y, colW, 14);
    thresholdLabel.setBounds (leftX + colW + gap, y, colW, 14);
    y += 16 + 10;

    autoThresholdToggle.setBounds (leftX, y, controlW, 20); y += 24;
    invaderModeToggle.setBounds (leftX, y, controlW, 20); y += 24;

    // Indented under Invader mode to read as its sub-option, and costing no
    // vertical space at all while it's hidden.
    if (invaderStreakToggle.isVisible()) {
        invaderStreakToggle.setBounds (leftX + 14, y, controlW - 14, 20);
        y += 24;
    }

    if (audioProcessor.wrapperType == juce::AudioProcessor::wrapperType_Standalone) {
        standalonePlayButton.setBounds (leftX, y, controlW, 26);
        y += 32;
    }

    if (isSkyMapMode) {
        skyMapCoordinatesLabel.setBounds (leftX, y, controlW, 40);
        y += 45;
    }

    controlsBottomY = y;

    // Reserved at the very bottom of the window, below the piano roll and
    // status label — see setupSynthPanel()/layoutSynthPanel(). paint()'s
    // mainArea (video + piano roll) is inset by this same synthPanelHeight
    // from the bottom, so the two never overlap.
    layoutSynthPanel ({ 15, h - synthPanelHeight - statusBarHeight - 4, w - 30, synthPanelHeight - 5 });

    // Under the knobs, along the bottom edge — see statusBarHeight.
    statusLabel.setBounds (15, h - statusBarHeight - 2, w - 30, statusBarHeight);

    // Depends on controlsBottomY, just computed above, so this has to be set
    // fresh on every resized() rather than once in the constructor — the
    // sky-map coordinates label changes how much vertical room the control
    // panel needs. Replaces the old approach of an async setSize() posted
    // from paint() whenever the window dipped below this height, which fired
    // on every one of paint()'s 30 calls/sec and fought a live drag.
    aspectConstrainer->setMinimumHeight (requiredEditorHeight());

    layoutOverlays();
}

void VisionMidiEditor::updateSkyMapControlVisibility() {
    skyMapCoordinatesLabel.setVisible (isSkyMapMode);
    constellationSelector.setVisible (isSkyMapMode);
    resized();
}

void VisionMidiEditor::updateSkyMapCoordinateDisplay() {
    if (!isSkyMapMode) return;
    float ra = audioProcessor.skyMapRaDegrees.load();
    float dec = audioProcessor.skyMapDecDegrees.load();
    float zoom = audioProcessor.skyMapZoomDegPerPixel.load();
    // Convert RA from degrees to hours and minutes
    float raHours = ra / 15.0f;
    int raH = (int)raHours;
    int raM = (int)((raHours - raH) * 60.0f);
    skyMapCoordinatesLabel.setText (
        "RA: " + juce::String (raH) + "h " + juce::String (raM) + "m\n"
        "Dec: " + juce::String (dec, 1) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")) + "\n"
        "Field: " + juce::String (1920.0f * zoom, 0) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")),
        juce::dontSendNotification
    );
}
