#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

class WaveformIconSelector;

// A view onto VisionMidiProcessor's state — the capture thread and the whole
// vision-to-MIDI engine live on the processor (see PluginProcessor.h), so
// that closing this editor's window (which a host is free to do at any time)
// never stops MIDI generation. This class only owns UI components, forwards
// their changes to the processor, and reads processor state to render.
class VisionMidiEditor : public juce::AudioProcessorEditor,
                         public juce::FileDragAndDropTarget,
                         // Object mode's colour picker is a ChangeBroadcaster;
                         // this is how its live updates get back here.
                         public juce::ChangeListener,
                         private juce::Timer
{
public:
    VisionMidiEditor (VisionMidiProcessor&);
    ~VisionMidiEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    // Total window width that exactly fits the video (at the current source's
    // aspect ratio) plus the fixed-width side chrome for a given window
    // height, with no leftover gap. Used both by the live resize constrainer
    // (so dragging never produces an off-aspect size) and to snap the window
    // once when the source's aspect ratio itself changes.
    int idealWidthForHeight (int height) const;

    // The other direction: given a width, the height that makes it exactly
    // ideal. Needed so the resize constrainer can follow a horizontal drag
    // as well as a vertical one — see EditorAspectConstrainer.
    int idealHeightForWidth (int width) const;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // Set true for the duration of a live corner-drag by
    // EditorAspectConstrainer::resizeStart()/resizeEnd(); paint() reads this
    // to avoid posting its own competing setSize() while the constrainer is
    // already driving the bounds every event.
    bool liveDragging = false;

    // The size at the moment a drag gesture starts, captured by
    // EditorAspectConstrainer::resizeStart(). JUCE's ResizableCornerComponent
    // computes every drag event's proposed bounds as this original size plus
    // the TOTAL mouse offset from drag-start (not incrementally from the
    // previous event), so comparing the raw proposed bounds against these
    // two numbers — rather than against last frame's already-aspect-
    // corrected bounds — is what makes it possible to tell which axis the
    // user is actually driving throughout the whole gesture.
    int dragStartWidth = 0;
    int dragStartHeight = 0;

private:
    void timerCallback() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    // The trackpad two-finger PINCH gesture (as opposed to a two-finger
    // SCROLL, which arrives as mouseWheelMove above) — macOS reports this
    // separately as a magnification gesture.
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

    void updateAvailableSources();
    void loadCustomFile();
    void loadNetworkStream();
    void adoptFile (const juce::File& file);
    void adoptSource (const juce::String& path);

    // Inline "Open File"/"Network Stream" pickers — plain child Components
    // laid over the editor, never a second top-level window. A native
    // FileChooser/AlertWindow (both call addToDesktop() to make a new OS
    // window) was confirmed, live in Ableton, to never actually appear —
    // no dialog shows up anywhere, and this editor's own host-provided
    // window loses its title/chrome and drops keyboard focus to whatever
    // app is behind it. Standalone never hit this since it doesn't host
    // the editor inside another app's window. Avoiding any second
    // top-level window entirely sidesteps that interaction rather than
    // chasing it further.
    void showFileBrowserOverlay();
    void hideFileBrowserOverlay();
    void showStreamOverlay();
    void hideStreamOverlay();
    void layoutOverlays();

    void drawPianoRoll (juce::Graphics& g, juce::Rectangle<float> bounds);
    // Shared by the video-side MIDI meter and the synth panel's audio-output
    // meter — a peak-hold bar plus a held-peak tick line, given whichever
    // pair of level/peak atomics it should be reading.
    void drawLevelMeter (juce::Graphics& g, juce::Rectangle<float> bounds, float level, float peak);
    juce::Rectangle<float> audioLevelMeterBounds;   // set in layoutSynthPanel(), read by paint()

    VisionMidiProcessor& audioProcessor;

    juce::ComboBox cameraSelector;
    juce::ComboBox scaleSelector;
    juce::ComboBox stepsSelector;   // sequencer loop length, in quarter notes
    juce::ComboBox occultingObjectSelector;   // what's drawn over the currently-played note
    juce::Slider rootNoteSlider;
    juce::Slider thresholdSlider;
    juce::ToggleButton autoThresholdToggle;
    // See PluginProcessor's invader-mode state — keyPressed() below reroutes
    // arrow keys/Space to ship movement/firing while this is on, instead of
    // their usual pan/play-stop behaviour.
    juce::ToggleButton invaderModeToggle;
    // Only shown while Invader mode is on — it controls nothing otherwise, and
    // a permanently visible checkbox for a sub-option of a mode that's off
    // just reads as clutter. resized() skips its row entirely when hidden, so
    // everything below closes up rather than leaving a gap.
    juce::ToggleButton invaderStreakToggle;

    // --- Object mode ------------------------------------------------------
    // Click an object to select it; drag it and let go to throw it; hold Alt
    // and drag to trace a looping path. The inspector below only appears once
    // something is selected, so the panel isn't cluttered the rest of the time.
    juce::ToggleButton objectModeToggle;
    juce::ComboBox     objectPathShapeSelector;
    juce::Slider       objectPathSpeedSlider;
    juce::Slider       objectBlinkRateSlider;
    juce::TextButton   objectTintButton { "Colour" };
    juce::TextButton   objectResetButton { "Reset" };
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void updateObjectInspectorVisibility();
    void refreshObjectInspector();

    // Drag state for the throw/draw gestures.
    bool isDraggingObject = false;
    bool isDrawingPath = false;
    int  draggedObject = -1;
    juce::Point<float> objectDragLastPos;
    juce::Point<float> objectDragVelocity;
    std::vector<cv::Point2f> drawnPath;
    // Last streak count pushed into the toggle's label, so timerCallback only
    // touches the button (and repaints) when the number actually changes.
    int lastShownInvaderStreak = -1;

    juce::Label rootNoteLabel;
    juce::Label thresholdLabel;
    juce::Label statusLabel;

    // Captions placed above each dropdown so it's clear what each one
    // controls, rather than relying on the dropdown's own currently-
    // selected text to convey that.
    juce::Label sourceCaption, stepsCaption, scaleCaption, occultingObjectCaption;

    juce::TextButton standalonePlayButton;

    // Pixel-rectangle zoom, available in every mode (sky map included, on
    // top of its own RA/Dec pan/zoom — this crops whatever frame it
    // renders). Plain ASCII text rather than a glyph: the sky map's
    // original zoom/pan buttons used Unicode arrows that didn't resolve in
    // this LookAndFeel's font and rendered as garbage boxes.
    juce::TextButton zoomInButton, zoomOutButton;

    // Rotates the same pixel-rectangle view by a fixed step every click —
    // same "every mode" reach as the zoom buttons above, and the same
    // underlying viewRotationDegrees that Cmd+drag also drives (see
    // mouseDown/mouseDrag).
    juce::TextButton rotateButton;

    juce::Rectangle<int> titleBounds;
    float moonPhase = 0.0f;
    float moonAngleDeg = 73.0f;
    float titleHue = 0.0f;   // cycles the OCCULTATION text through the rainbow — see timerCallback()
    std::unique_ptr<juce::LookAndFeel_V4> occultLookAndFeel;
    std::unique_ptr<juce::ComponentBoundsConstrainer> aspectConstrainer;

    bool lastKnownStandalonePlaying = false;
    bool isDraggingValidFile = false;
    int controlsBottomY = 0;
    // The height the layout actually needs: the left control column's own
    // content plus the synth panel reserved beneath it. Only meaningful
    // once resized() has run and set controlsBottomY.
    int requiredEditorHeight() const {
        return juce::jmax (400 + bottomChromeHeight, controlsBottomY + 45 + bottomChromeHeight);
    }
    float lastAppliedImageAspect = -1.0f;

    // Refreshed from paint() (which already holds imageLock to draw the
    // frame anyway) and read by idealWidthForHeight() without any locking.
    // The live resize constrainer calls idealWidthForHeight() on every drag
    // event, potentially dozens of times a second — taking imageLock there
    // meant the message thread blocked on whatever the capture thread was
    // doing whenever a drag and a frame update overlapped, which is exactly
    // what made shrinking the window feel like it was hitching/blocking.
    float cachedImageAspect = 16.0f / 9.0f;

    // Sky map: panned and zoomed by dragging/scrolling directly on the video
    // rather than dedicated buttons (see mouseDown/mouseDrag/mouseWheelMove).
    juce::Label skyMapCoordinatesLabel;
    bool isSkyMapMode = false;
    void updateSkyMapCoordinateDisplay();
    void updateSkyMapControlVisibility();

    // "Jump to constellation" picker, floated over the bottom-centre of the
    // video — bounds are set from paint() (see lastVideoBounds below) since
    // that's the only place the video's current on-screen rectangle is
    // known. constellationList mirrors the combo box's items in the same
    // order so its onChange handler can look up ra/dec by selected id.
    juce::ComboBox constellationSelector;
    std::vector<SkyMapRenderer::ConstellationInfo> constellationList;

    // Written in paint() where videoBounds is already computed, read
    // lock-free by the mouse handlers — same reasoning as cachedImageAspect
    // above: paint() already holds imageLock, the mouse handlers shouldn't
    // have to.
    juce::Rectangle<float> lastVideoBounds;

    // Sky-map drag-to-pan state, captured at mouseDown and read for the
    // whole gesture so the drag is anchored to an absolute RA/Dec rather
    // than accumulated per-event (which could drift on rounding).
    bool isPanningSkyMap = false;
    juce::Point<float> skyMapDragStartPos;
    float skyMapDragStartRaDegrees = 0.0f;
    float skyMapDragStartDecDegrees = 0.0f;

    // Same anchored-drag pattern as the sky-map state above, but for the
    // generic pixel-rectangle zoom (every mode, including on top of sky
    // map's own pan/zoom) — kept separate rather than shared since the
    // units differ (normalized 0..1 frame fractions here, RA/Dec degrees
    // there).
    bool isPanningView = false;
    juce::Point<float> viewDragStartPos;
    float viewDragStartCenterX = 0.5f;
    float viewDragStartCenterY = 0.5f;
    void zoomBy (float factor);   // multiplies audioProcessor.viewZoom, clamped

    // Un-rotates a screen-space delta (raw pixels, or a unit direction from
    // an arrow key) back into the crop's own unrotated axes, so drag/arrow
    // panning moves the view in the direction the cursor/key actually means
    // on screen rather than in the crop's pre-rotation axes. Shared by both
    // the generic pixel-rect pan and the sky map's RA/Dec pan, and by
    // keyPressed's arrow-key panning of each — see mouseDrag for the maths.
    juce::Point<float> unrotateScreenDelta (juce::Point<float> delta) const;

    // Cmd+drag rotates instead of panning — same anchored-drag pattern
    // again (angle at mouseDown + total pixel delta since, not accumulated
    // per-event), checked in mouseDown against the event's modifiers to
    // decide which of this and isPanningView applies for the gesture.
    bool isRotatingView = false;
    juce::Point<float> rotateDragStartPos;
    float rotateDragStartDegrees = 0.0f;

    // --- Synth panel, below the piano roll (see SynthEngine.h/.cpp for
    // what these actually control) --- the 3 choice parameters get named
    // combo boxes (each needs its own label/behaviour anyway); the dozen
    // float knobs are built generically from a static id/label list in
    // setupSynthPanel() rather than declared one by one, since a
    // juce::Slider+Label+attachment triple is otherwise a lot of near-
    // identical boilerplate per knob.
    void setupSynthPanel();
    void layoutSynthPanel (juce::Rectangle<int> area);
    static constexpr int synthPanelHeight = 150;
    // The "Note ..." readout's own strip, along the very bottom of the window
    // under the synth knobs. It used to sit between the piano roll and the
    // synth panel, which cost the roll a 35px band across the full width and
    // put a line of text in the middle of the layout; down here it reads as a
    // status bar and the roll gets that height back.
    static constexpr int statusBarHeight = 20;
    // Everything below the piano roll: the synth panel, the status strip, and
    // the margins around them. paint()'s mainArea and idealWidthForHeight()
    // both inset by exactly this, so the roll's bottom edge lands just above
    // the panel's Preset row with no dead space between them.
    static constexpr int bottomChromeHeight = synthPanelHeight + statusBarHeight + 10;

    juce::Label waveformCaption;

    // A row of small clickable waveform-shape icons (sine/saw/square/
    // triangle/noise) instead of a text dropdown — defined in
    // PluginEditor.cpp alongside OccultLookAndFeel; forward-declared here
    // so this header doesn't need the drawing details.
    std::unique_ptr<WaveformIconSelector> waveformSelector;

    // Preset bar, laid out along the synth panel's top row beside the Type
    // and Waveform controls: browse with the list or the arrows, Save
    // overwrites the current one, Save As prompts for a name, Del removes.
    juce::Label presetCaption;
    juce::ComboBox presetSelector;
    juce::TextButton prevPresetButton, nextPresetButton;
    juce::TextButton savePresetButton, saveAsPresetButton, deletePresetButton;
    int presetRefreshCounter = 0;
    juce::StringArray lastSeenPresetNames;
    void refreshPresetList (const juce::String& nameToSelect = {});
    void stepPreset (int delta);
    void promptForPresetName();

    struct SynthKnob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    std::vector<std::unique_ptr<SynthKnob>> synthKnobs;

    // See showFileBrowserOverlay()/showStreamOverlay() above.
    juce::WildcardFileFilter fileBrowserFilter { "*.mp4;*.avi;*.mov;*.m4v;*.jpg;*.jpeg;*.png;*.fits;*.fit;*.fts",
                                                  "*", "Video or image files" };
    std::unique_ptr<juce::FileBrowserComponent> fileBrowserOverlay;
    juce::TextButton fileBrowserChooseButton { "Load" }, fileBrowserCancelButton { "Cancel" };

    juce::Label streamPromptLabel;
    juce::TextEditor streamUrlEditor;
    juce::TextButton streamConnectButton { "Connect" }, streamCancelButton { "Cancel" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VisionMidiEditor)
};
