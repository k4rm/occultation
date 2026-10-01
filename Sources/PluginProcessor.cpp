#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"
#include <opencv2/opencv.hpp>
#include <opencv2/geometry/2d.hpp>   // getRotationMatrix2D — not pulled in by the umbrella header in OpenCV 5.x
#include <cstdint>
#include <cstring>
#include <map>
#include <utility>

namespace {

struct CelestialAsset { const char* data; int size; };

const CelestialAsset* celestialAssetFor (OccultingObjectType type)
{
    static const std::map<OccultingObjectType, CelestialAsset> assets = {
        { OccultingObjectType::Sun,     { BinaryData::sun_png,     BinaryData::sun_pngSize } },
        { OccultingObjectType::Mercury, { BinaryData::mercury_png, BinaryData::mercury_pngSize } },
        { OccultingObjectType::Venus,   { BinaryData::venus_png,   BinaryData::venus_pngSize } },
        { OccultingObjectType::Earth,   { BinaryData::earth_png,   BinaryData::earth_pngSize } },
        { OccultingObjectType::Mars,    { BinaryData::mars_png,    BinaryData::mars_pngSize } },
        { OccultingObjectType::Jupiter, { BinaryData::jupiter_png, BinaryData::jupiter_pngSize } },
        { OccultingObjectType::Saturn,  { BinaryData::saturn_png,  BinaryData::saturn_pngSize } },
        { OccultingObjectType::Uranus,  { BinaryData::uranus_png,  BinaryData::uranus_pngSize } },
        { OccultingObjectType::Neptune, { BinaryData::neptune_png, BinaryData::neptune_pngSize } },
        { OccultingObjectType::Pluto,   { BinaryData::pluto_png,   BinaryData::pluto_pngSize } },
    };
    auto it = assets.find (type);
    return it == assets.end() ? nullptr : &it->second;
}

// The Moon is the one body with a real photograph for each of several
// distinct appearances rather than one fixed picture — new, waxing
// crescent, first quarter, waxing gibbous, full, waning gibbous, last
// quarter, waning crescent, in that (real, cyclical) order.
constexpr int numMoonPhases = 8;

CelestialAsset moonPhaseAsset (int phaseIndex)
{
    static const CelestialAsset phases[numMoonPhases] = {
        { BinaryData::moon_phase_1_png, BinaryData::moon_phase_1_pngSize },
        { BinaryData::moon_phase_2_png, BinaryData::moon_phase_2_pngSize },
        { BinaryData::moon_phase_3_png, BinaryData::moon_phase_3_pngSize },
        { BinaryData::moon_phase_4_png, BinaryData::moon_phase_4_pngSize },
        { BinaryData::moon_phase_5_png, BinaryData::moon_phase_5_pngSize },
        { BinaryData::moon_phase_6_png, BinaryData::moon_phase_6_pngSize },
        { BinaryData::moon_phase_7_png, BinaryData::moon_phase_7_pngSize },
        { BinaryData::moon_phase_8_png, BinaryData::moon_phase_8_pngSize },
    };
    return phases[((phaseIndex % numMoonPhases) + numMoonPhases) % numMoonPhases];
}

// Decodes an on-disk image (jpg/png/…) to a BGR cv::Mat via JUCE rather than
// cv::imread. OpenCV is linked statically here (see CMakeLists) and its
// bundled libjpeg fails to decode inside this binary — cv::imread/imdecode
// find the decoder (haveImageReader() is true) but hand back an empty Mat
// with no exception, for every JPEG. PNG through OpenCV is unaffected, but
// there's no reason to keep two decoders around: JUCE's has to be linked in
// regardless, reads jpg/png/gif, and takes file access out of OpenCV's hands
// entirely (which also side-steps non-ASCII path handling).
static cv::Mat loadImageFileAsMat (const juce::File& file)
{
    juce::Image img = juce::ImageFileFormat::loadFrom (file);
    if (! img.isValid()) return {};

    img = img.convertedToFormat (juce::Image::ARGB);
    juce::Image::BitmapData src (img, juce::Image::BitmapData::readOnly);

    cv::Mat bgr (img.getHeight(), img.getWidth(), CV_8UC3);
    for (int y = 0; y < img.getHeight(); ++y) {
        auto* dstRow = bgr.ptr<uchar> (y);
        for (int x = 0; x < img.getWidth(); ++x) {
            auto px = src.getPixelColour (x, y);
            dstRow[x * 3 + 0] = px.getBlue();
            dstRow[x * 3 + 1] = px.getGreen();
            dstRow[x * 3 + 2] = px.getRed();
        }
    }
    return bgr;
}

cv::Mat decodeRGBA (const CelestialAsset& asset)
{
    cv::Mat result;
    cv::Mat buf (1, asset.size, CV_8UC1, (void*) asset.data);
    cv::Mat decoded = cv::imdecode (buf, cv::IMREAD_UNCHANGED);
    if (! decoded.empty() && decoded.channels() == 4)
        cv::cvtColor (decoded, result, cv::COLOR_BGRA2RGBA);
    return result;
}

// Decoded once per (type, variant) — this is only ever called from run()'s
// single background thread, so a plain static cache needs no locking.
// `variant` is the Moon's phase index for OccultingObjectType::Moon and
// unused (always 0) for every other type, which only ever has one image.
// Cached as RGBA (cv::imdecode's native order is BGRA) to match rgbFrame's
// own RGB channel order, so the compositor below can copy channels
// straight across.
const cv::Mat& decodedCelestialImage (OccultingObjectType type, int variant)
{
    static std::map<std::pair<OccultingObjectType, int>, cv::Mat> cache;
    auto key = std::make_pair (type, type == OccultingObjectType::Moon ? variant : 0);
    auto cached = cache.find (key);
    if (cached != cache.end())
        return cached->second;

    cv::Mat result;
    if (type == OccultingObjectType::Moon) {
        result = decodeRGBA (moonPhaseAsset (variant));

        // Each phase photo bakes its unlit portion in as a fully opaque
        // near-black disc — composited as-is, that reads as a stark black
        // hole punched out of the starfield rather than a barely-visible
        // dark limb. Fade alpha toward zero for dark pixels (below a
        // luminance threshold) so the unlit side blends into whatever's
        // behind it instead; the lit crescent's crater texture is well
        // above this threshold, so it stays exactly as bright and opaque
        // as before.
        if (! result.empty()) {
            constexpr float fadeThreshold = 60.0f;   // 0-255 luminance
            for (int y = 0; y < result.rows; ++y) {
                auto* row = result.ptr<cv::Vec4b> (y);
                for (int x = 0; x < result.cols; ++x) {
                    cv::Vec4b& px = row[x];
                    if (px[3] == 0) continue;
                    float luminance = 0.299f * (float) px[0] + 0.587f * (float) px[1] + 0.114f * (float) px[2];
                    if (luminance < fadeThreshold)
                        px[3] = (uchar) ((float) px[3] * (luminance / fadeThreshold));
                }
            }
        }
    } else if (const CelestialAsset* asset = celestialAssetFor (type))
        result = decodeRGBA (*asset);

    return cache.emplace (key, result).first->second;
}

// A slow spin, applied to most bodies (not Circle/Sun/Moon/Earth/Saturn,
// which all have their own treatment) so the same picture doesn't look
// perfectly frozen from one note to the next — a simplified stand-in for
// the real planet rotating, since a flat photo (rather than a full
// equirectangular map) can't be reprojected properly. Rotating about the
// image's own centre, rather than a horizontal wraparound scroll, is what
// makes this seamless: every pixel's distance from centre — and therefore
// its position relative to the circular alpha mask — is unchanged by a
// rotation, so the disc's silhouette staying exactly put while only its
// surface texture turns. A scroll doesn't have that property: the disc
// itself is circular, and the built-in wraparound point cuts across its
// middle, so mid-scroll it visibly split into two crescents rather than
// looking like a whole rotating sphere.
cv::Mat rotateAboutCentre (const cv::Mat& src, float angleDegrees)
{
    if (src.empty()) return src;
    cv::Point2f centre ((float) src.cols / 2.0f, (float) src.rows / 2.0f);
    cv::Mat rot = cv::getRotationMatrix2D (centre, angleDegrees, 1.0);
    cv::Mat result;
    cv::warpAffine (src, result, rot, src.size(), cv::INTER_LINEAR,
                    cv::BORDER_CONSTANT, cv::Scalar (0, 0, 0, 0));
    return result;
}

// Earth's night side, approximated from the real current UTC hour rather
// than occultingObjectStep — a simple rotating terminator (midnight
// longitude opposite local noon) rather than a full solar-subpoint
// calculation, which is plenty for what's meant to be a nice detail on an
// icon-sized overlay rather than an ephemeris.
void applyEarthTerminator (cv::Mat& rgba)
{
    juce::Time now = juce::Time::getCurrentTime();
    double localHours = now.getHours() + now.getMinutes() / 60.0;
    double utcHours = std::fmod (localHours - now.getUTCOffsetSeconds() / 3600.0 + 24.0, 24.0);
    // Local noon sits under the sun at longitude 0 when utcHours == 12;
    // the disc's horizontal axis is treated as -180..+180 degrees of
    // longitude, so the sun-facing longitude simply slides across it
    // through the day.
    float noonLongitudeDeg = (float) ((utcHours - 12.0) * 15.0);

    int w = rgba.cols, h = rgba.rows;
    float cx = w / 2.0f, cy = h / 2.0f, r = std::min (cx, cy);
    if (r <= 0.0f) return;

    for (int y = 0; y < h; ++y) {
        cv::Vec4b* row = rgba.ptr<cv::Vec4b> (y);
        for (int x = 0; x < w; ++x) {
            cv::Vec4b& px = row[x];
            if (px[3] == 0) continue;

            float nx = (x - cx) / r, ny = (y - cy) / r;
            float d2 = nx * nx + ny * ny;
            if (d2 > 1.0f) continue;

            // Longitude of this pixel on the visible hemisphere, and its
            // angular distance from the sun-facing longitude — beyond 90
            // degrees it's night.
            float pixelLongitudeDeg = nx * 90.0f;
            float angleFromNoon = std::abs (pixelLongitudeDeg - noonLongitudeDeg);
            if (angleFromNoon > 180.0f) angleFromNoon = 360.0f - angleFromNoon;

            float night = juce::jlimit (0.0f, 1.0f, (angleFromNoon - 80.0f) / 20.0f);
            if (night <= 0.0f) continue;

            float darken = 1.0f - night * 0.55f;
            px[0] = (uchar) (px[0] * darken);
            px[1] = (uchar) (px[1] * darken);
            px[2] = (uchar) (px[2] * darken);
        }
    }
}

// Alpha-composites a small RGBA image onto a 3-channel RGB frame, centred
// at `centre` and clipped to stay within `dest`'s bounds (the target
// position can legitimately fall near/off an edge).
void compositeRGBAOnto (cv::Mat& dest, const cv::Mat& rgba, cv::Point centre)
{
    int x0 = centre.x - rgba.cols / 2, y0 = centre.y - rgba.rows / 2;
    int srcX0 = std::max (0, -x0), srcY0 = std::max (0, -y0);
    int dstX0 = std::max (0, x0),  dstY0 = std::max (0, y0);
    int overlapW = std::min (rgba.cols - srcX0, dest.cols - dstX0);
    int overlapH = std::min (rgba.rows - srcY0, dest.rows - dstY0);
    if (overlapW <= 0 || overlapH <= 0) return;

    for (int j = 0; j < overlapH; ++j) {
        const cv::Vec4b* srcRow = rgba.ptr<cv::Vec4b> (srcY0 + j);
        cv::Vec3b* dstRow = dest.ptr<cv::Vec3b> (dstY0 + j);
        for (int i = 0; i < overlapW; ++i) {
            const cv::Vec4b& s = srcRow[srcX0 + i];
            if (s[3] == 0) continue;
            float a = (float) s[3] / 255.0f;
            cv::Vec3b& d = dstRow[dstX0 + i];
            d[0] = (uchar) ((float) s[0] * a + (float) d[0] * (1.0f - a));
            d[1] = (uchar) ((float) s[1] * a + (float) d[1] * (1.0f - a));
            d[2] = (uchar) ((float) s[2] * a + (float) d[2] * (1.0f - a));
        }
    }
}

} // namespace

//==============================================================================
// Minimal FITS reader for DWARF mini raw/stacked captures — no external FITS
// library, since we only ever need a single primary HDU of image data.
// Reads one big-endian sample (per the FITS BITPIX convention).
static double readFitsSample (const uint8_t* p, int bitpix)
{
    switch (bitpix) {
        case 8:
            return (double) p[0];
        case 16: {
            auto v = (int16_t) (((uint16_t) p[0] << 8) | (uint16_t) p[1]);
            return (double) v;
        }
        case 32: {
            auto v = (int32_t) (((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16)
                               | ((uint32_t) p[2] << 8) | (uint32_t) p[3]);
            return (double) v;
        }
        case -32: {
            uint32_t u = ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16)
                        | ((uint32_t) p[2] << 8) | (uint32_t) p[3];
            float f;
            std::memcpy (&f, &u, sizeof (f));
            return (double) f;
        }
        case -64: {
            uint64_t u = 0;
            for (int i = 0; i < 8; ++i) u = (u << 8) | (uint64_t) p[i];
            double d;
            std::memcpy (&d, &u, sizeof (d));
            return d;
        }
        default:
            return 0.0;
    }
}

// Astro sub-exposures are dominated by background sky with a long bright-star
// tail, so a straight min/max stretch crushes everything to near-black. This
// clips the extreme percentiles (found via a histogram, not a full sort) and
// linearly maps what's left onto 0-255 for a usable preview.
static cv::Mat stretchPlaneTo8Bit (const cv::Mat& plane32f, double loPercentile, double hiPercentile)
{
    cv::Mat out8 (plane32f.size(), CV_8UC1, cv::Scalar (0));

    double minV, maxV;
    cv::minMaxLoc (plane32f, &minV, &maxV);
    if (maxV <= minV) return out8;

    constexpr int numBins = 65536;
    std::vector<uint32_t> hist (numBins, 0);
    double binScale = (double) (numBins - 1) / (maxV - minV);
    for (int y = 0; y < plane32f.rows; ++y) {
        const float* row = plane32f.ptr<float> (y);
        for (int x = 0; x < plane32f.cols; ++x) {
            int bin = (int) juce::jlimit (0.0, (double) (numBins - 1), (row[x] - minV) * binScale);
            ++hist[(size_t) bin];
        }
    }

    auto total = (long long) plane32f.total();
    long long loCount = (long long) ((double) total * (loPercentile / 100.0));
    long long hiCount = (long long) ((double) total * (hiPercentile / 100.0));

    double loVal = minV, hiVal = maxV;
    long long cum = 0;
    bool loFound = false;
    for (int b = 0; b < numBins; ++b) {
        cum += hist[(size_t) b];
        if (!loFound && cum >= loCount) { loVal = minV + (double) b / binScale; loFound = true; }
        if (cum >= hiCount) { hiVal = minV + (double) b / binScale; break; }
    }
    if (hiVal <= loVal) hiVal = loVal + 1.0;

    double outScale = 255.0 / (hiVal - loVal);
    for (int y = 0; y < plane32f.rows; ++y) {
        const float* row = plane32f.ptr<float> (y);
        uint8_t* orow = out8.ptr<uint8_t> (y);
        for (int x = 0; x < plane32f.cols; ++x)
            orow[x] = (uint8_t) juce::jlimit (0.0, 255.0, (row[x] - loVal) * outScale);
    }
    return out8;
}

// Loads a single-HDU FITS file as either a mono (raw Bayer, uncalibrated) or
// BGR (already-debayered 3-plane stack) 8-bit cv::Mat, ready to drop straight
// into the same pipeline used for jpg/png/video frames.
static cv::Mat loadFitsAsMat (const juce::String& path)
{
    juce::File fitsFile (path);
    juce::FileInputStream in (fitsFile);
    if (!in.openedOk()) return {};

    int bitpix = 0, naxis = 0;
    int64_t naxis1 = 0, naxis2 = 0, naxis3 = 1;
    double bzero = 0.0, bscale = 1.0;
    bool sawSimple = false, sawEnd = false;

    char block[2880];
    int blocksRead = 0;
    while (!sawEnd) {
        if (in.read (block, 2880) != 2880 || ++blocksRead > 200) return {};

        for (int i = 0; i < 2880 && !sawEnd; i += 80) {
            const char* card = block + i;

            char keyBuf[9];
            std::memcpy (keyBuf, card, 8);
            keyBuf[8] = '\0';
            juce::String key = juce::String (keyBuf).trim();

            if (key == "END") { sawEnd = true; break; }
            if (card[8] != '=') continue;

            char valueBuf[71];
            std::memcpy (valueBuf, card + 10, 70);
            valueBuf[70] = '\0';
            juce::String value (valueBuf);
            int slash = value.indexOfChar ('/');
            if (slash >= 0) value = value.substring (0, slash);
            value = value.trim();

            if (key == "SIMPLE")      sawSimple = value.startsWithChar ('T');
            else if (key == "BITPIX") bitpix = value.getIntValue();
            else if (key == "NAXIS")  naxis = value.getIntValue();
            else if (key == "NAXIS1") naxis1 = value.getLargeIntValue();
            else if (key == "NAXIS2") naxis2 = value.getLargeIntValue();
            else if (key == "NAXIS3") naxis3 = value.getLargeIntValue();
            else if (key == "BZERO")  bzero = value.getDoubleValue();
            else if (key == "BSCALE") bscale = value.getDoubleValue();
        }
    }

    if (!sawSimple || naxis < 2 || naxis1 <= 0 || naxis2 <= 0
        || (bitpix != 8 && bitpix != 16 && bitpix != 32 && bitpix != -32 && bitpix != -64))
        return {};

    int numPlanes = (naxis >= 3 && naxis3 > 1) ? (int) juce::jlimit ((int64_t) 1, (int64_t) 4, naxis3) : 1;
    size_t bytesPerSample = (size_t) std::abs (bitpix) / 8;
    size_t pixelsPerPlane = (size_t) naxis1 * (size_t) naxis2;
    size_t totalBytes = pixelsPerPlane * (size_t) numPlanes * bytesPerSample;

    std::vector<uint8_t> raw (totalBytes);
    size_t readSoFar = 0;
    while (readSoFar < totalBytes) {
        int chunk = (int) std::min (totalBytes - readSoFar, (size_t) (1 << 20));
        int got = in.read (raw.data() + readSoFar, chunk);
        if (got <= 0) return {};
        readSoFar += (size_t) got;
    }

    std::vector<cv::Mat> planes;
    for (int pl = 0; pl < numPlanes; ++pl)
        planes.emplace_back ((int) naxis2, (int) naxis1, CV_32F);

    const uint8_t* src = raw.data();
    for (int pl = 0; pl < numPlanes; ++pl) {
        for (int y = 0; y < (int) naxis2; ++y) {
            float* rowPtr = planes[(size_t) pl].ptr<float> (y);
            for (int x = 0; x < (int) naxis1; ++x) {
                rowPtr[x] = (float) (readFitsSample (src, bitpix) * bscale + bzero);
                src += bytesPerSample;
            }
        }
    }

    cv::Mat result;
    if (numPlanes >= 3) {
        // Plane order for a DWARF/Siril RGB cube is R, G, B; cv::Mat is BGR.
        cv::Mat b8 = stretchPlaneTo8Bit (planes[2], 0.5, 99.5);
        cv::Mat g8 = stretchPlaneTo8Bit (planes[1], 0.5, 99.5);
        cv::Mat r8 = stretchPlaneTo8Bit (planes[0], 0.5, 99.5);
        cv::merge (std::vector<cv::Mat> { b8, g8, r8 }, result);
    } else {
        // A single raw Bayer-mosaic plane — left un-debayered (this feeds a
        // brightness-driven MIDI mapping, not a colour-accurate viewer).
        result = stretchPlaneTo8Bit (planes[0], 1.0, 99.9);
    }

    // FITS row 0 is the bottom of the image; flip to the usual top-down raster.
    cv::flip (result, result, 0);
    return result;
}

//==============================================================================
// Instrument bus shape (see CMakeLists.txt: IS_SYNTH TRUE, IS_MIDI_EFFECT
// FALSE) — MIDI in, stereo audio OUT, no audio input bus, matching JUCE's
// own official synth-plugin template. The stereo output is genuinely used
// now (SynthEngine renders into it in processBlock) — this used to be a
// pure MIDI-effect plugin with an audio bus kept only to satisfy Ableton
// Live's VST3 loader (see git history), which no longer applies now that
// there's real audio to output through it.
VisionMidiProcessor::VisionMidiProcessor()
     : AudioProcessor (BusesProperties()
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
       juce::Thread ("VisionCaptureThread")
{
    juce::PropertiesFile::Options options;
    options.applicationName     = "Occultation";
    options.filenameSuffix      = "settings";
    options.osxLibrarySubFolder = "Application Support";
    options.folderName          = "Occultation";
    options.storageFormat       = juce::PropertiesFile::storeAsXML;
    appProperties = std::make_unique<juce::PropertiesFile> (options);

    // Sky Map (997) is the default rather than Cam 0: it needs no camera
    // permission, no file, and no network, so a fresh install — or a plugin
    // instance in a sandboxed host that can't reach any of those — still
    // shows something and generates notes immediately.
    int savedId = appProperties->getIntValue ("lastSourceId", skyMapSourceId);
    targetSourceId = savedId > 0 ? savedId : skyMapSourceId;

    // Start on the first factory voice. The old Synth Type parameter
    // defaulted to index 0 and stamped that voice in on the first audio
    // callback; with that gone, nothing would otherwise pick a starting
    // sound and the raw parameter defaults aren't any particular voice.
    // A host restoring saved state overwrites this afterwards, as it should.
    if (auto factoryNames = getFactoryPresetNames(); ! factoryNames.isEmpty())
        loadPreset (factoryNames[0]);

    startThread();
}

VisionMidiProcessor::~VisionMidiProcessor() {
    isRunning = false;
    signalThreadShouldExit();
    // Must actually finish before the stream-thread cleanup below runs, not
    // just be given a head start: run() is what calls switchSource(), which
    // itself joins streamCaptureThread — if this timed out while that join
    // was still in flight, the code below would call .join() on the same
    // std::thread concurrently with it, which is undefined behaviour. 3s
    // comfortably covers switchSource()'s own worst case (bounded by
    // CAP_PROP_READ_TIMEOUT_MSEC, 1s, on the capture — see streamCaptureLoop()).
    stopThread (3000);

    // Safe now that run() has actually stopped — nothing else can still be
    // starting or joining a stream thread. Not folded into the join above:
    // release()ing/reopening a VideoCapture from a thread other than the
    // one currently reading it is explicitly not something OpenCV
    // guarantees is safe, so this only ever touches it once run() is gone.
    stopStreamThread.store (true);
    stopAndJoinStreamCaptureThread (3000);
}

void VisionMidiProcessor::stopAndJoinStreamCaptureThread (int timeoutMs) {
    if (! streamCaptureThread.joinable())
        return;

    for (int waited = 0; waited < timeoutMs && ! streamCaptureThreadFinished.load(); waited += 20)
        std::this_thread::sleep_for (std::chrono::milliseconds (20));

    if (streamCaptureThreadFinished.load())
        streamCaptureThread.join();
    else
        streamCaptureThread.detach();
}

const juce::String VisionMidiProcessor::getName() const { return JucePlugin_Name; }
bool VisionMidiProcessor::acceptsMidi() const { return true; }
// Still true — Occultation keeps sending the same notes out as MIDI (e.g.
// to drive an external synth) even now that it also renders them itself;
// see PluginProcessor's dual audio-synth + MIDI-output design.
bool VisionMidiProcessor::producesMidi() const { return true; }
// No longer a MIDI effect now that it renders real audio through
// SynthEngine — an "instrument" (IS_SYNTH, see CMakeLists.txt) instead.
bool VisionMidiProcessor::isMidiEffect() const { return false; }
double VisionMidiProcessor::getTailLengthSeconds() const { return 2.0; }   // covers the synth's own release/reverb tail

int VisionMidiProcessor::getNumPrograms() { return 1; }
int VisionMidiProcessor::getCurrentProgram() { return 0; }
void VisionMidiProcessor::setCurrentProgram (int (index)) {}
const juce::String VisionMidiProcessor::getProgramName (int index) { return {}; }
void VisionMidiProcessor::changeProgramName (int index, const juce::String& newName) {}

void VisionMidiProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    synthEngine.prepare (sampleRate, samplesPerBlock);
}

void VisionMidiProcessor::releaseResources() { synthEngine.reset(); }

#ifndef JucePlugin_PreferredChannelConfigurations
bool VisionMidiProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Instrument shape: no audio input bus at all (there isn't one — see
    // the constructor), stereo or mono audio output from SynthEngine.
    auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}
#endif

void VisionMidiProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    #if JucePlugin_Build_Standalone
    // The standalone app has no host transport of its own to query, so let an
    // external MIDI clock (e.g. Ableton Live's "Sync" MIDI output, looped back
    // via IAC into this app's MIDI input) start/stop it — otherwise stopping
    // playback in another application this app merely sends MIDI to has no
    // way to reach it at all.
    for (const auto metadata : midiMessages) {
        auto msg = metadata.getMessage();
        if (msg.isMidiStart()) {
            standalonePpqPosition.store (0.0);
            standalonePlaying.store (true);
        } else if (msg.isMidiContinue()) {
            standalonePlaying.store (true);
        } else if (msg.isMidiStop()) {
            standalonePlaying.store (false);
            resetPlaybackState();
        }
    }

    if (standalonePlaying.load()) {
        double beatsPerSample = (120.0 / 60.0) / currentSampleRate;
        double currentPpq = standalonePpqPosition.load() + (beatsPerSample * buffer.getNumSamples());
        // Was hardcoded to 16 regardless of the Steps selector — with, say,
        // 64 steps selected, this wrapped the simulated transport back to 0
        // a quarter of the way through the sequence every time, so the
        // cursor (and barProgress derived from it in processSequenceMode)
        // never swept past the first 16 beats: the back three quarters of
        // a 64-step sequence were simply never reachable.
        double loopBeats = sequenceLoopBeats.load();
        if (currentPpq >= loopBeats) {
            currentPpq = std::fmod (currentPpq, loopBeats);
        }
        standalonePpqPosition.store (currentPpq);
    }
    #endif

    // Sampled here, on the audio thread, because that's the only place
    // getPlayHead() is valid — run() reads these atomics instead of calling
    // it from its own thread. See the members' declaration for why AU broke
    // on the old approach while VST3 happened to survive it.
    if (wrapperType != juce::AudioProcessor::wrapperType_Standalone) {
        if (auto* ph = getPlayHead()) {
            if (auto pos = ph->getPosition(); pos.hasValue()) {
                hostIsPlaying.store (pos->getIsPlaying());
                hostPpqPosition.store (pos->getPpqPosition().orFallback (0.0));
            }
        }
    }

    {
        const juce::ScopedLock sl (midiCriticalSection);
        midiMessages.addEvents (incomingMidiQueue, 0, buffer.getNumSamples(), 0);
        incomingMidiQueue.clear();
    }

    // Dispatches to the synth engine happen here, on the audio thread that
    // owns it, rather than from addMidiMessage() (called from run()'s
    // background thread) — midiMessages is the exact same thread-safe
    // handoff point the MIDI-out path already uses (incomingMidiQueue,
    // drained just above under midiCriticalSection), so this reuses that
    // instead of adding a second, separate lock around the synth's voices.
    applyParametersToSynth();

    // --- Nothing sounds while the transport is stopped ---
    // The sequencer already stops CHOOSING notes when the transport does, but
    // that left anything already sounding to ring on: sustained voices holding
    // their release, the reverb/delay tails, an invader explosion's rumble,
    // and any note whose note-off happened not to have been sent yet. Stopping
    // the host is meant to produce silence, so the whole engine is torn down on
    // the edge and nothing is synthesized until it starts again.
    const bool transportPlaying = wrapperType == juce::AudioProcessor::wrapperType_Standalone
                                      ? standalonePlaying.load()
                                      : hostIsPlaying.load();
    if (! transportPlaying) {
        if (wasTransportPlaying) {
            // Kills voices, envelopes, both SFX state machines and the effect
            // tails in one go (see SynthEngine::reset).
            synthEngine.reset();
            // And release anything still held downstream — our own MIDI output
            // is a separate path from the synth, so a note-on that went out
            // before the stop would otherwise hang in whatever it's feeding.
            midiMessages.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            activeNoteCount.store (0);
            wasTransportPlaying = false;
        }
        // Drop any SFX the invader queued in the meantime rather than letting
        // them fire the moment playback resumes.
        laserTriggerPending.exchange (false);
        pendingExplosionIntensity.exchange (-1.0f);

        buffer.clear();
        currentAudioLevel.store (0.0f);
        audioPeakLevel.store (0.0f);
        return;
    }
    wasTransportPlaying = true;

    for (const auto metadata : midiMessages) {
        auto msg = metadata.getMessage();
        if (msg.isNoteOn())       synthEngine.noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff()) synthEngine.noteOff (msg.getNoteNumber());
    }
    // Invader-mode SFX triggers, set from run() (a hit) or fireBullet()
    // (a shot, on the message thread) — consumed here since SynthEngine's
    // one-shot state machines are only ever touched from the audio thread,
    // same as the voices above.
    if (laserTriggerPending.exchange (false))
        synthEngine.triggerLaser (pendingLaserFrequencyHz.load());
    float explosionIntensity = pendingExplosionIntensity.exchange (-1.0f);
    if (explosionIntensity >= 0.0f)
        synthEngine.triggerExplosion (explosionIntensity);

    synthEngine.renderNextBlock (buffer, buffer.getNumSamples());

    // Peak-hold for the audio output meter — decayed in run()'s loop,
    // exactly like currentMidiLevel/midiPeakLevel above.
    float blockPeak = buffer.getMagnitude (0, buffer.getNumSamples());
    currentAudioLevel.store (juce::jmax (currentAudioLevel.load(), blockPeak));
    audioPeakLevel.store (juce::jmax (audioPeakLevel.load(), blockPeak));
}

void VisionMidiProcessor::addMidiMessage (uint8_t note, uint8_t velocity, bool isNoteOn)
{
    const juce::ScopedLock sl (midiCriticalSection);
    juce::MidiMessage msg = isNoteOn ? juce::MidiMessage::noteOn (1, (int)note, (uint8_t)velocity)
                                     : juce::MidiMessage::noteOff (1, (int)note, (uint8_t)0);
    incomingMidiQueue.addEvent (msg, 0);

    if (isNoteOn && velocity > 0) {
        activeNoteCount.fetch_add (1);
    } else {
        int current = activeNoteCount.load();
        if (current > 0) {
            activeNoteCount.fetch_sub (1);
        }
    }
}

//==============================================================================
void VisionMidiProcessor::adoptSource (const juce::String& path) {
    appProperties->setValue ("lastFilePath", path);
    appProperties->setValue ("lastSourceId", 999);
    appProperties->saveIfNeeded();
    targetSourceId = 999;
    // run() (a different thread) used to notice "the path changed while
    // staying on sourceId 999" by re-reading appProperties and comparing it
    // against activeFilePath on every single loop iteration — comparing a
    // juce::String written here against one read there, with no
    // synchronization between the two, forever. This flag says the same
    // thing without any cross-thread string traffic in the hot loop.
    pendingSourceChange.store (true);
}

void VisionMidiProcessor::selectBuiltInSource (int sourceId) {
    if (sourceId <= 0) return;
    appProperties->setValue ("lastSourceId", sourceId);
    if (sourceId != 999)
        appProperties->setValue ("lastFilePath", juce::String());
    appProperties->saveIfNeeded();
    targetSourceId = sourceId;
}

juce::String VisionMidiProcessor::getLastFilePath() const {
    return appProperties->getValue ("lastFilePath");
}

int VisionMidiProcessor::getLastSourceId() const {
    return appProperties->getIntValue ("lastSourceId", skyMapSourceId);
}

void VisionMidiProcessor::resetPlaybackState() {
    standalonePpqPosition.store (0.0);
    // continuousPpq/lastRawPpq/loopCount/pianoRollNotes are only ever touched
    // by the capture thread itself; this just flags the reset for run() to
    // perform on its own thread instead of racing it from here.
    playbackResetRequested.store (true);
}

void VisionMidiProcessor::panSkyMap (float dRaDegrees, float dDecDegrees) {
    float newRa = skyMapRaDegrees.load() + dRaDegrees;
    float newDec = skyMapDecDegrees.load() + dDecDegrees;

    // Wrap over the poles instead of clamping there: crossing dec = +/-90
    // continues onto the antipodal meridian (RA + 180), which is genuinely
    // the other half of the sky with different constellations — so panning
    // never hits a wall, it just keeps revealing new sky. (Each iteration
    // halves how far past the pole newDec still is, so this always
    // terminates in a couple of steps even for a very large single pan.)
    while (newDec >  90.0f) { newDec =  180.0f - newDec; newRa += 180.0f; }
    while (newDec < -90.0f) { newDec = -180.0f - newDec; newRa += 180.0f; }

    while (newRa < 0.0f) newRa += 360.0f;
    while (newRa >= 360.0f) newRa -= 360.0f;

    skyMapRaDegrees.store (newRa);
    skyMapDecDegrees.store (newDec);
}

void VisionMidiProcessor::setSkyMapZoom (float zoomDegPerPixel) {
    // 0.002 deg/px (~4 degree field at 1920px, tight enough to pick out a
    // single star cluster) to 0.2 deg/px (~384 degree field, wider than the
    // whole sky) covers deep zoom through zoomed-all-the-way-out.
    float clampedZoom = std::max (0.002f, std::min (0.2f, zoomDegPerPixel));
    skyMapZoomDegPerPixel.store (clampedZoom);
}

void VisionMidiProcessor::setSkyMapView (float raDegrees, float decDegrees, float zoomDegPerPixel) {
    panSkyMap (raDegrees - skyMapRaDegrees.load(), decDegrees - skyMapDecDegrees.load());
    setSkyMapZoom (zoomDegPerPixel);
}

void VisionMidiProcessor::setViewZoom (float zoom) {
    float clamped = juce::jlimit (1.0f, 8.0f, zoom);
    viewZoom.store (clamped);

    // Re-clamp the center for the new zoom: at higher zoom the crop
    // rectangle is narrower, so a center that was valid (or even centred)
    // at the old zoom can now overhang the frame edge.
    float halfFracX = 0.5f / clamped;
    float halfFracY = 0.5f / clamped;
    viewCenterX.store (juce::jlimit (halfFracX, 1.0f - halfFracX, viewCenterX.load()));
    viewCenterY.store (juce::jlimit (halfFracY, 1.0f - halfFracY, viewCenterY.load()));
}

void VisionMidiProcessor::panView (float dNormalizedX, float dNormalizedY) {
    float zoom = viewZoom.load();
    float halfFracX = 0.5f / zoom;
    float halfFracY = 0.5f / zoom;
    viewCenterX.store (juce::jlimit (halfFracX, 1.0f - halfFracX, viewCenterX.load() + dNormalizedX));
    viewCenterY.store (juce::jlimit (halfFracY, 1.0f - halfFracY, viewCenterY.load() + dNormalizedY));
}

void VisionMidiProcessor::rotateViewBy (float deltaDegrees) {
    float updated = viewRotationDegrees.load() + deltaDegrees;
    // Wrap into (-180, 180] rather than letting it grow unbounded — repeated
    // taps of the rotate button, or a long Cmd-drag, would otherwise send
    // this to a huge magnitude that std::fmod below would still normalize
    // correctly, but keeping it small is just tidier to read back for a UI.
    updated = std::fmod (updated + 180.0f, 360.0f);
    if (updated <= 0.0f) updated += 360.0f;
    viewRotationDegrees.store (updated - 180.0f);
}

void VisionMidiProcessor::setInvaderModeEnabled (bool enabled) {
    invaderModeEnabled.store (enabled);
    if (enabled) {
        shipX.store (0.5f);
        shipY.store (0.85f);
        shipVelX.store (0.0f);
        shipVelY.store (0.0f);
        shipSteer.store (0.0f);
        shipThrottle.store (0.0f);
        shipTargetSpeed.store (0.0f);
        shipFacingX.store (0.0f);
        shipFacingY.store (-1.0f);   // nose up
        invaderExhaustParticles.clear();
        ticksSinceLastExhaustSpawn = 0;
    }
    invaderStreak.store (0);
    // run() owns the mask; this only asks for it to be cleared (see the
    // member's comment) so toggling the mode never leaves old craters behind.
    invaderDamageResetRequested.store (true);

    const juce::ScopedLock sl (invaderLock);
    // Bullets now hold a sounding note for their whole flight, so they can't
    // just be dropped — each one's note-off has to go out or it stays on
    // forever in whatever the host has downstream.
    for (const auto& b : invaderBullets)
        addMidiMessage (b.note, 0, false);
    invaderBullets.clear();
}

bool VisionMidiProcessor::transportRunning() const {
    return wrapperType == juce::AudioProcessor::wrapperType_Standalone ? standalonePlaying.load()
                                                                      : hostIsPlaying.load();
}

std::vector<int> VisionMidiProcessor::invaderScaleNotes (int transposeSemitones, int count) {
    auto mode = (MusicalMode) currentScale.load();
    int root = currentRootNote.load() + transposeSemitones;
    int degreeOffset = 0;

    if (invaderStreakModulationEnabled.load()) {
        const int streak = invaderStreak.load();
        // One scale degree per hit, capped at an octave's worth: past that the
        // invader layer would climb clean out of audible range on a long run.
        degreeOffset = juce::jmin (streak, 7);

        // Every `escalationStep` hits the layer also changes mode, walking
        // from the user's own choice into progressively more strung-out
        // territory. Fixed order rather than random so a streak sounds like a
        // build with a direction, and so the same streak sounds the same
        // twice.
        constexpr int escalationStep = 8;
        static const MusicalMode escalations[] = {
            MusicalMode::HarmonicMinor, MusicalMode::HungarianMinor,
            MusicalMode::Byzantine,     MusicalMode::AlteredScale
        };
        if (streak >= escalationStep) {
            int stage = juce::jlimit (0, (int) std::size (escalations) - 1,
                                      streak / escalationStep - 1);
            mode = escalations[stage];
        }
    }

    auto notes = getScaleNotes (mode, root, count + degreeOffset);
    // Transposing by DEGREES, not semitones: drop the lowest `degreeOffset`
    // notes so the run starts that far up the mode and every interval above
    // it stays diatonic to that mode.
    if (degreeOffset > 0 && (int) notes.size() > degreeOffset)
        notes.erase (notes.begin(), notes.begin() + degreeOffset);
    return notes;
}

int VisionMidiProcessor::invaderNoteForY (float normalizedY, int transposeSemitones) {
    auto notes = invaderScaleNotes (transposeSemitones, 24);
    if (notes.empty()) return 60;
    // 24 lanes, top of the frame = highest note — identical to the mapping
    // processSequenceMode samples with, including the 23 - lane inversion.
    int lane = juce::jlimit (0, 23, (int) (normalizedY * 24.0f));
    return juce::jlimit (0, 127, notes[(size_t) juce::jmin (23 - lane, (int) notes.size() - 1)]);
}

int VisionMidiProcessor::harmonisedInvaderNote (int transposeSemitones) {
    auto notes = invaderScaleNotes (0, 36);   // three octaves to find room above
    if (notes.empty()) return 60;
    auto& rng = juce::Random::getSystemRandom();

    int soundingNote = lastPlayedNote.load();
    int soundingIndex = -1;
    if (soundingNote >= 0) {
        for (size_t i = 0; i < notes.size(); ++i) {
            if (notes[i] % 12 == soundingNote % 12) { soundingIndex = (int) i; break; }
        }
    }

    int chosen;
    if (soundingIndex >= 0) {
        // Degrees, not semitones: +2 / +4 / +6 scale steps is a third, fifth
        // or seventh *of the current mode*, so it's consonant with whatever
        // the sequencer is on without needing to know the mode's intervals.
        const int degreesUp = 2 + 2 * rng.nextInt (3);
        chosen = notes[(size_t) juce::jlimit (0, (int) notes.size() - 1, soundingIndex + degreesUp)];
    } else {
        chosen = notes[(size_t) rng.nextInt ((int) notes.size())];
    }
    return juce::jlimit (0, 127, chosen + transposeSemitones);
}

void VisionMidiProcessor::scheduleInvaderRun (const std::vector<int>& notes, uint8_t velocity,
                                              double gridBeats, double stepBeats) {
    // Snap the run's ONSET to the grid, then step through it far faster than
    // the grid itself: the flourish arrives on the beat but still reads as a
    // quick run rather than a slow arpeggio spread over half a bar.
    const bool quantise = transportRunning() && gridBeats > 0.0;
    const double start = quantise ? std::ceil (continuousPpq.load() / gridBeats) * gridBeats : 0.0;

    int index = 0;
    for (int n : notes) {
        const auto note = (uint8_t) juce::jlimit (0, 127, n);
        if (quantise) {
            const double at = start + (double) index * stepBeats;
            pendingInvaderNotes.push_back ({ at,                     0, note, velocity, true });
            pendingInvaderNotes.push_back ({ at + stepBeats * 0.9, 0, note, 0,        false });
        } else {
            // No transport to snap to — fall back to the tick spacing this
            // used before, so the mode still plays (and still sounds like a
            // run) with the sequencer stopped.
            pendingInvaderNotes.push_back ({ -1.0, index,     note, velocity, true });
            pendingInvaderNotes.push_back ({ -1.0, index + 1, note, 0,        false });
        }
        ++index;
    }
}

void VisionMidiProcessor::applyInvaderDamage (cv::Mat& gray, const ViewTransform& view) {
    if (invaderDamageResetRequested.exchange (false) || invaderDamageMask.empty()) {
        invaderDamageMask = cv::Mat::zeros (invaderDamageMaskSize, invaderDamageMaskSize, CV_8U);
        return;
    }
    if (gray.empty()) return;

    // Heal linearly rather than by a decay factor: an exponential one stalls
    // at small integer values on CV_8U (3 * 0.985 rounds back to 3) and the
    // crater never actually closes. ~2 per ~30ms tick closes a full-strength
    // hit in about four seconds, so the loop audibly thins out under sustained
    // fire and fills back in when you stop.
    constexpr int healPerTick = 2;
    cv::subtract (invaderDamageMask, cv::Scalar (healPerTick), invaderDamageMask);

    cv::Mat visibleDamage;
    if (! buildVisibleDamage (gray.size(), view, visibleDamage)) return;

    // Subtracting (rather than masking to zero) means a partly-healed crater
    // dims its star instead of switching it back on all at once: as the mask
    // fades, whatever was there crosses back over the threshold gradually.
    cv::subtract (gray, visibleDamage, gray);
}

bool VisionMidiProcessor::buildVisibleDamage (cv::Size size, const ViewTransform& view, cv::Mat& out) const {
    // Cheap early-out: with nothing damaged there's no reason to resize a mask
    // full of zeroes onto every frame.
    if (invaderDamageMask.empty() || cv::countNonZero (invaderDamageMask) == 0)
        return false;

    const int mw = invaderDamageMask.cols, mh = invaderDamageMask.rows;
    const int rx = juce::jlimit (0, mw - 1, (int) (view.cropX0 * (float) mw));
    const int ry = juce::jlimit (0, mh - 1, (int) (view.cropY0 * (float) mh));
    const int rw = juce::jlimit (1, mw - rx, (int) std::ceil (view.cropW * (float) mw));
    const int rh = juce::jlimit (1, mh - ry, (int) std::ceil (view.cropH * (float) mh));

    cv::resize (invaderDamageMask (cv::Rect (rx, ry, rw, rh)), out, size, 0, 0, cv::INTER_LINEAR);

    if (std::abs (view.rotationDegrees) > 0.01f) {
        // The same rotation run() applied to the frame, so damage turns with
        // the image it belongs to.
        cv::Point2f centre ((float) out.cols * 0.5f, (float) out.rows * 0.5f);
        cv::Mat rot = cv::getRotationMatrix2D (centre, (double) view.rotationDegrees, 1.0);
        cv::Mat rotated;
        cv::warpAffine (out, rotated, rot, out.size(), cv::INTER_LINEAR,
                        cv::BORDER_CONSTANT, cv::Scalar (0));
        out = rotated;
    }
    return true;
}

cv::Point VisionMidiProcessor::damageMaskPoint (float viewX, float viewY, const ViewTransform& view) const {
    if (invaderDamageMask.empty()) return { -1, -1 };

    // Screen -> source. Undo the display rotation about the view's centre
    // first (the transpose of the matrix warpAffine applied — see the same
    // inverse worked out in processSequenceMode's sky-map branch), then map
    // through the zoom crop.
    float dx = viewX - 0.5f, dy = viewY - 0.5f;
    if (std::abs (view.rotationDegrees) > 0.01f) {
        const float rad = view.rotationDegrees * juce::MathConstants<float>::pi / 180.0f;
        const float c = std::cos (rad), s = std::sin (rad);
        const float rotX = c * dx - s * dy;
        const float rotY = s * dx + c * dy;
        dx = rotX; dy = rotY;
    }
    const float sourceX = view.cropX0 + (dx + 0.5f) * view.cropW;
    const float sourceY = view.cropY0 + (dy + 0.5f) * view.cropH;

    const int mw = invaderDamageMask.cols, mh = invaderDamageMask.rows;
    return { juce::jlimit (0, mw - 1, (int) (sourceX * (float) mw)),
             juce::jlimit (0, mh - 1, (int) (sourceY * (float) mh)) };
}

void VisionMidiProcessor::addInvaderDamage (float viewX, float viewY, const ViewTransform& view) {
    const cv::Point centre = damageMaskPoint (viewX, viewY, view);
    if (centre.x < 0) return;

    // Scaled by the crop, so a hit erases the same amount of what's ON SCREEN
    // at any zoom — which is what the player aimed at — rather than the same
    // absolute patch of source pixels.
    const int radius = juce::jmax (2, (int) (0.03f * (float) invaderDamageMask.cols
                                             * juce::jmax (view.cropW, view.cropH)));
    cv::circle (invaderDamageMask, centre, radius, cv::Scalar (255), -1, cv::LINE_AA);
}

void VisionMidiProcessor::drawPlasmaBall (cv::Mat& rgbFrame, cv::Point centre, int maxRadius,
                                          float age, float seed)
{
    // Zoom in fast and ease out, then hold at full size while it fades — the
    // eye catches the bloom, then the ball is just there, dissolving. The
    // fraction is small because the ball's whole life is ~2s: the bloom itself
    // still wants to be over in under a third of a second, even now that it has
    // half the screen to cross.
    const float grow = 1.0f - std::pow (1.0f - juce::jmin (1.0f, age / 0.15f), 3.0f);
    const int radius = (int) ((float) maxRadius * (0.15f + 0.85f * grow));
    if (radius < 3) return;

    // Half transparent at its peak, after a very short attack so it doesn't
    // pop in at full strength.
    const float fade = age < 0.04f ? age / 0.04f
                                   : std::pow (1.0f - (age - 0.04f) / 0.96f, 1.5f);
    const float peakAlpha = 0.5f * juce::jlimit (0.0f, 1.0f, fade);
    if (peakAlpha <= 0.004f) return;

    // The visible part of the ball, clipped to the frame.
    const cv::Rect ballRect (centre.x - radius, centre.y - radius, radius * 2, radius * 2);
    const cv::Rect clipped = ballRect & cv::Rect (0, 0, rgbFrame.cols, rgbFrame.rows);
    if (clipped.width <= 0 || clipped.height <= 0) return;

    // 128 rather than 64: the filaments are thin by design, and at 64 they
    // came out of the upscale visibly smeared. ~16k samples per explosion per
    // frame is still nothing next to evaluating the field at display size.
    // Capped at the ball's own drawn size, though — a small explosion (sizes
    // are randomised per hit, see invaderExplosionMinScale) would otherwise
    // pay for the full field only to have it scaled straight back down.
    const int tileSize = juce::jlimit (32, 128, radius * 2);
    cv::Mat colourTile (tileSize, tileSize, CV_8UC3, cv::Scalar (0, 0, 0));
    cv::Mat alphaTile  (tileSize, tileSize, CV_32F,  cv::Scalar (0.0f));

    const float phase = seed * juce::MathConstants<float>::twoPi + age * 9.0f;
    // Light from the upper left, the direction every demo and every raytraced
    // sphere of that era used.
    constexpr float lightX = -0.53f, lightY = -0.62f, lightZ = 0.58f;

    for (int ty = 0; ty < tileSize; ++ty) {
        auto* colourRow = colourTile.ptr<cv::Vec3b> (ty);
        auto* alphaRow  = alphaTile.ptr<float> (ty);
        const float v = ((float) ty + 0.5f) / (float) tileSize * 2.0f - 1.0f;

        for (int tx = 0; tx < tileSize; ++tx) {
            const float u = ((float) tx + 0.5f) / (float) tileSize * 2.0f - 1.0f;
            const float r2 = u * u + v * v;
            if (r2 >= 1.0f) continue;               // outside the ball

            const float z = std::sqrt (1.0f - r2);  // sphere normal's third component

            // Spherical warp: sampling the plasma through the sphere's own
            // surface (rather than flat across the disc) is what stops it
            // looking like a circular crop of a flat pattern — the field
            // compresses towards the edge exactly as a texture on a ball does.
            const float warp = 1.0f / (z + 0.35f);
            const float px = u * warp * 2.6f;
            const float py = v * warp * 2.6f;

            // Four interfering sine fields — the classic plasma.
            float field = std::sin (px * 2.3f + phase)
                        + std::sin (py * 2.9f - phase * 0.8f)
                        + std::sin ((px + py) * 1.7f + phase * 0.6f)
                        + std::sin (std::sqrt (px * px + py * py) * 3.4f - phase * 1.4f);
            field = field * 0.125f + 0.5f;          // -4..4 -> 0..1

            // Where the interfering fields cancel, draw a bright thin ridge:
            // that's what turns a smooth blob of colour into the filaments a
            // plasma globe actually has. A full-sweep rainbow palette (the
            // other obvious demoscene choice) came out looking like a beach
            // ball — this keeps to two stops, violet into cyan, with the
            // filaments burning towards white.
            const float filament = std::pow (1.0f - std::abs (2.0f * field - 1.0f), 3.5f);
            const float mix = field * field * (3.0f - 2.0f * field);   // smoothstep
            float rr = 0.34f - 0.14f * mix + filament * 0.85f;
            float gg = 0.10f + 0.62f * mix + filament * 0.90f;
            float bb = 0.66f + 0.30f * mix + filament * 0.95f;

            // Shading and the two reflections that sell it as glass: a tight
            // specular highlight where the light hits, and a rim that lifts
            // the limb of the ball away from whatever's behind it.
            const float diffuse = juce::jmax (0.0f, u * lightX + v * lightY + z * lightZ);
            const float specular = std::pow (diffuse, 42.0f);
            const float rim = std::pow (1.0f - z, 2.6f);

            const float shade = 0.55f + 0.45f * diffuse;
            rr = rr * shade + specular + rim * 0.55f;
            gg = gg * shade + specular + rim * 0.30f;
            bb = bb * shade + specular + rim * 0.75f;

            colourRow[tx] = cv::Vec3b ((uchar) juce::jlimit (0, 255, (int) (rr * 255.0f)),
                                       (uchar) juce::jlimit (0, 255, (int) (gg * 255.0f)),
                                       (uchar) juce::jlimit (0, 255, (int) (bb * 255.0f)));

            // Thin where the gas is, solid along the filaments and at the
            // highlight — which is what makes it read as something you can
            // see through rather than a painted sphere.
            const float r = std::sqrt (r2);
            const float edge = juce::jlimit (0.0f, 1.0f, (1.0f - r) / 0.18f);
            alphaRow[tx] = edge * (0.45f + 0.55f * filament + 0.15f * rim) + specular * 0.6f;
        }
    }

    // Scale to the ball's on-screen size, then take only the part that's
    // actually inside the frame.
    cv::Mat colourFull, alphaFull;
    cv::resize (colourTile, colourFull, cv::Size (ballRect.width, ballRect.height), 0, 0, cv::INTER_LINEAR);
    cv::resize (alphaTile,  alphaFull,  cv::Size (ballRect.width, ballRect.height), 0, 0, cv::INTER_LINEAR);

    const cv::Rect srcRect (clipped.x - ballRect.x, clipped.y - ballRect.y, clipped.width, clipped.height);
    cv::Mat colourPart = colourFull (srcRect);
    cv::Mat alphaPart  = alphaFull (srcRect);
    cv::Mat destination = rgbFrame (clipped);

    for (int y = 0; y < destination.rows; ++y) {
        auto* dst = destination.ptr<cv::Vec3b> (y);
        const auto* src = colourPart.ptr<cv::Vec3b> (y);
        const auto* a = alphaPart.ptr<float> (y);

        for (int x = 0; x < destination.cols; ++x) {
            const float alpha = juce::jlimit (0.0f, 1.0f, a[x]) * peakAlpha;
            if (alpha <= 0.004f) continue;
            for (int c = 0; c < 3; ++c)
                dst[x][c] = (uchar) juce::jlimit (0, 255,
                    (int) ((float) dst[x][c] * (1.0f - alpha) + (float) src[x][c] * alpha));
        }
    }
}

float VisionMidiProcessor::invaderTargetThreshold() const {
    if (activeSourceId == skyMapSourceId)
        return skyMapStarThreshold;
    return detectionThreshold.load();
}

bool VisionMidiProcessor::isViewPositionDamaged (float viewX, float viewY, const ViewTransform& view) const {
    if (! invaderModeEnabled.load()) return false;
    const cv::Point p = damageMaskPoint (viewX, viewY, view);
    if (p.x < 0) return false;
    // Half-erased still counts as gone, so a star drops out of the loop as the
    // crater lands rather than fading back in and out around the edge of it.
    return invaderDamageMask.at<uchar> (p.y, p.x) >= 128;
}

void VisionMidiProcessor::setShipControls (float steer, float throttle) {
    shipSteer.store (steer);
    shipThrottle.store (throttle);
}

void VisionMidiProcessor::updateShipPhysics() {
    if (! invaderModeEnabled.load()) return;

    // Exactly two things move the ship, and they don't interact:
    //
    //  - left/right rotate the facing, whether or not it's moving, so it can
    //    be aimed while sitting still;
    //  - up/down move a target speed, held when no key is down (a cruise
    //    setting rather than a push), and the ship travels along its nose at
    //    precisely that speed.
    //
    // Velocity used to EASE towards facing * speed, for a slide through turns.
    // That quietly made steering a third control over speed: interpolating
    // between two vectors at an angle shortens the result, so every turn cost
    // momentum and a hard one nearly stopped the ship. Speed now comes from
    // the throttle and nothing else.
    //
    // ~230 degrees/second at run()'s ~30ms cadence — a full 180 degree turn in
    // about eight tenths of a second.
    constexpr float turnDegreesPerTick = 7.0f;
    // ~1.5s from a standstill to full speed, and the same to stop again.
    constexpr float maxSpeed = 0.05f;
    constexpr float speedChangePerTick = maxSpeed / 50.0f;
    constexpr float halfShip = 0.025f;

    // --- Steering ---
    const float steer = juce::jlimit (-1.0f, 1.0f, shipSteer.load());
    if (std::abs (steer) > 0.001f) {
        const float rad = steer * turnDegreesPerTick * juce::MathConstants<float>::pi / 180.0f;
        const float c = std::cos (rad), s2 = std::sin (rad);
        const float fx = shipFacingX.load(), fy = shipFacingY.load();
        shipFacingX.store (fx * c - fy * s2);
        shipFacingY.store (fx * s2 + fy * c);
    }

    // --- Throttle ---
    const float throttle = juce::jlimit (-1.0f, 1.0f, shipThrottle.load());
    const float targetSpeed = juce::jlimit (0.0f, maxSpeed,
                                            shipTargetSpeed.load() + throttle * speedChangePerTick);
    shipTargetSpeed.store (targetSpeed);

    // --- Motion ---
    // Straight along the nose, at exactly the throttle's speed.
    const float vx = shipFacingX.load() * targetSpeed;
    const float vy = shipFacingY.load() * targetSpeed;
    shipVelX.store (vx);
    shipVelY.store (vy);

    // Position is simply clamped to the field: fly into an edge and the ship
    // holds against it until it's steered away. Bleeding speed off on contact
    // was tempting, but the clamp fires just as much when travelling ALONG an
    // edge as into it, so it turned skimming the border — a normal thing to
    // want to do, since that's where a frame's content often is — into a crawl.
    shipX.store (juce::jlimit (halfShip, 1.0f - halfShip, shipX.load() + vx));
    shipY.store (juce::jlimit (halfShip, 1.0f - halfShip, shipY.load() + vy));

    // Exhaust: two flame trails behind the ship, one on each side, only
    // while thrust is actually being held (not just coasting on leftover
    // inertia) — a new puff on alternating sides roughly every 150ms,
    // capped at 3 visible per side below like a real trail thinning out.
    // Every spawn also schedules its own short 6-note run from the current
    // scale/mode (queued the same way the explosion's ascending run is),
    // so continuous flight carries a musical "engine" tied to the mode.
    // Only while actually accelerating — not while coasting at a held speed,
    // braking, or merely turning. The engine hum's note stream follows the
    // same rule, so the musical "engine" sounds when the ship is working.
    bool isThrusting = throttle > 0.001f;
    ++ticksSinceLastExhaustSpawn;
    constexpr int exhaustSpawnPeriodTicks = 5;   // ~150ms at run()'s ~30ms cadence
    if (isThrusting && ticksSinceLastExhaustSpawn >= exhaustSpawnPeriodTicks) {
        ticksSinceLastExhaustSpawn = 0;
        bool spawnLeft = exhaustSpawnLeftNext;
        exhaustSpawnLeftNext = ! exhaustSpawnLeftNext;

        float fx = shipFacingX.load(), fy = shipFacingY.load();
        float px = -fy, py = fx;   // perpendicular to facing — the "side" offset
        float sign = spawnLeft ? -1.0f : 1.0f;
        constexpr float behindDist = 0.028f * invaderShipScale;
        constexpr float sideDist = 0.012f * invaderShipScale;
        float ex = shipX.load() - fx * behindDist + px * sideDist * sign;
        float ey = shipY.load() - fy * behindDist + py * sideDist * sign;
        float flicker = juce::Random::getSystemRandom().nextFloat() * juce::MathConstants<float>::twoPi;
        invaderExhaustParticles.push_back ({ ex, ey, -fx, -fy, flicker, spawnLeft, 14 });

        // Cap at 3 visible per side — oldest of that side drops first.
        for (bool wantLeft : { true, false }) {
            int count = 0;
            for (long j = (long) invaderExhaustParticles.size() - 1; j >= 0; --j) {
                if (invaderExhaustParticles[(size_t) j].leftSide == wantLeft && ++count > 3)
                    invaderExhaustParticles.erase (invaderExhaustParticles.begin() + j);
            }
        }

        // Quantised to 1/8 with 1/32 steps: the engine hum is the most
        // frequent event in the mode (one every ~150ms of held thrust), so
        // snapping it to the same grid as everything else is what keeps
        // continuous flight from smearing across the beat. Runs through
        // invaderScaleNotes, so a streak carries the engine up with it.
        scheduleInvaderRun (invaderScaleNotes (0, 6), 26, 0.5, 0.125);
    }
}

void VisionMidiProcessor::fireBullet() {
    // The laser's pitch — both the real MIDI note and the synthesized sweep's
    // starting frequency — is chosen to be consonant with whatever the
    // sequencer is sounding at this instant (a third, fifth or seventh above
    // it in scale degrees), transposed up two octaves for that "high pitch
    // laser" register. Previously a random note from the scale: in the mode
    // that was already tied to the selected scale, but it took no notice of
    // the melody actually playing underneath, so the two layers could sit a
    // second apart as easily as a fifth.
    const int laserNote = harmonisedInvaderNote (24);
    float laserFreqHz = 440.0f * std::pow (2.0f, ((float) laserNote - 69.0f) / 12.0f);

    // Doppler: firing while accelerating along the ship's own heading pitches
    // the zap up, firing while drifting backwards pitches it down. Only the
    // synthesized sweep bends — bending the MIDI note would need pitch bend,
    // which on a shared channel would drag the sequencer's notes with it.
    constexpr float maxSpeed = 0.05f;   // matches updateShipPhysics
    const float closingSpeed = (shipVelX.load() * shipFacingX.load()
                                + shipVelY.load() * shipFacingY.load()) / maxSpeed;
    laserFreqHz *= juce::jlimit (0.7f, 1.4f, 1.0f + 0.35f * closingSpeed);

    bool bulletSpawned = false;
    {
        const juce::ScopedLock sl (invaderLock);
        constexpr size_t maxBullets = 24;
        if (invaderBullets.size() < maxBullets) {
            constexpr float bulletSpeed = 0.035f;   // normalized units per ~30ms run() tick
            // Leaves the NOSE, not the middle of the hull: spawning at the
            // ship's own centre put the bullet inside the sprite, and since
            // the collision test runs on the tick it's spawned, firing while
            // sitting over anything bright detonated instantly at the ship's
            // own position. Every shot taken while flying across a star field
            // became an explosion on the ship rather than one out where the
            // bullet went. The arming delay in updateAndDrawInvaderMode is the
            // other half of that fix.
            constexpr float muzzleOffset = 0.035f;
            invaderBullets.push_back ({ shipX.load() + shipFacingX.load() * muzzleOffset,
                                        shipY.load() + shipFacingY.load() * muzzleOffset,
                                        shipFacingX.load() * bulletSpeed, shipFacingY.load() * bulletSpeed,
                                        (uint8_t) laserNote, 0 });
            bulletSpawned = true;
        }
    }

    // Only sound the note if a bullet actually went out: the note is released
    // by that bullet's own death (hit, or off the edge of the frame — see
    // updateAndDrawInvaderMode), so a shot swallowed by the bullet cap would
    // leave a note on with nothing left alive to turn it off. Time of flight
    // is therefore the note's length: a shot clean across the frame rings out,
    // a point-blank hit stabs.
    if (! bulletSpawned) return;

    addMidiMessage ((uint8_t) laserNote, 110, true);
    pendingLaserFrequencyHz.store (laserFreqHz);
    laserTriggerPending.store (true);
}

void VisionMidiProcessor::updateAndDrawInvaderMode (cv::Mat& rgbFrame, const cv::Mat& gray,
                                                    const ViewTransform& view)
{
    if (! invaderModeEnabled.load() || gray.empty() || rgbFrame.empty())
        return;

    const float threshold = invaderTargetThreshold();
    int grayW = gray.cols, grayH = gray.rows;
    int rgbW = rgbFrame.cols, rgbH = rgbFrame.rows;

    // Craters, drawn into the picture itself before anything else goes on top
    // of it — so what's been shot out of the loop is visible as well as
    // audible, and you can see what's left to hit. The source frame is never
    // touched: the damage is a separate mask composited here (and subtracted
    // from `gray` in applyInvaderDamage), so nothing is permanently eaten out
    // of a loaded image or a video's decoded frames, and it heals when you stop
    // firing instead of leaving you with an empty picture and no way back.
    {
        cv::Mat visibleDamage;
        if (buildVisibleDamage (rgbFrame.size(), view, visibleDamage)) {
            cv::Mat damageRgb;
            cv::cvtColor (visibleDamage, damageRgb, cv::COLOR_GRAY2RGB);
            cv::subtract (rgbFrame, damageRgb, rgbFrame);
        }
    }

    {
        const juce::ScopedLock sl (invaderLock);
        for (size_t i = 0; i < invaderBullets.size(); ) {
            auto& b = invaderBullets[i];
            const float fromX = b.x, fromY = b.y;
            b.x += b.vx;
            b.y += b.vy;
            ++b.ticksAlive;

            // Armed only once it's clear of the ship. Without this, a shot
            // fired while the hull overlaps anything bright — which is most of
            // the time, since grazing rewards flying right through the bright
            // parts — hits on its first tick and blooms an explosion centred
            // on the ship itself. Two ticks puts it ~0.07 of the frame out,
            // past both the sprite and the graze radius.
            constexpr int armingTicks = 2;
            const bool armed = b.ticksAlive > armingTicks;

            // Sweep the whole segment travelled this tick, not just the point
            // it landed on. A bullet crosses 0.035 of the frame per tick —
            // around 67 pixels on a 1920-wide one — while a star is only 2-7
            // pixels across, so testing a single small neighbourhood at the
            // end of each step jumps straight over almost everything: measured
            // against a real sky-map frame, only 3% of shots ever registered,
            // against 11% when the segment is actually swept. That gap was
            // invisible while Sky Map pinned the threshold to 0 and every
            // pixel counted as a hit; it turned into "the gun doesn't work"
            // the moment the threshold started meaning something.
            bool hit = false;
            float hitBrightness = 0.0f;
            float hitX = b.x, hitY = b.y;
            if (armed) {
                const float dxPixels = (b.x - fromX) * (float) grayW;
                const float dyPixels = (b.y - fromY) * (float) grayH;
                // One sample every ~2 pixels, so nothing star-sized fits
                // between two of them.
                const int steps = juce::jlimit (1, 256,
                    (int) std::ceil (std::sqrt (dxPixels * dxPixels + dyPixels * dyPixels) * 0.5f));
                constexpr int r = 2;

                for (int step = 1; step <= steps && ! hit; ++step) {
                    const float f = (float) step / (float) steps;
                    const float sampleX = fromX + (b.x - fromX) * f;
                    const float sampleY = fromY + (b.y - fromY) * f;
                    if (sampleX < 0.0f || sampleX > 1.0f || sampleY < 0.0f || sampleY > 1.0f) continue;

                    const int px = juce::jlimit (0, grayW - 1, (int) (sampleX * (float) grayW));
                    const int py = juce::jlimit (0, grayH - 1, (int) (sampleY * (float) grayH));
                    for (int oy = -r; oy <= r && ! hit; ++oy) {
                        for (int ox = -r; ox <= r && ! hit; ++ox) {
                            int sx = px + ox, sy = py + oy;
                            if (sx < 0 || sx >= grayW || sy < 0 || sy >= grayH) continue;
                            uchar v = gray.at<uchar> (sy, sx);
                            // Strictly greater, exactly as processSequenceMode
                            // tests it — with >=, a threshold of 0 made even a
                            // pure-black pixel a hit.
                            if ((float) v > threshold) {
                                hit = true;
                                hitBrightness = (float) v;
                                // Where along the path it actually struck, not
                                // where the tick happened to end: the bullet
                                // stops at the first thing in its way, and the
                                // explosion, the crater and the note's pitch
                                // all belong at that point.
                                hitX = sampleX; hitY = sampleY;
                            }
                        }
                    }
                }
            }

            bool offscreen = b.x < -0.05f || b.x > 1.05f || b.y < -0.05f || b.y > 1.05f;
            // A bullet that somehow neither hits nor leaves the frame (a
            // zero-velocity shot fired while perfectly stationary) would hold
            // its note forever otherwise.
            constexpr int maxBulletTicks = 200;   // ~6s
            bool expired = b.ticksAlive > maxBulletTicks;

            if (hit) {
                // At half the short side each ball costs ~1.4ms to draw and
                // lives two seconds, so a burst of hits stacks up fast: twelve
                // at once would be over half the ~30ms capture tick, and at
                // this size they'd be visual mush anyway. Six is ~28% worst
                // case. Oldest goes first — it's the one already faded out.
                constexpr size_t maxConcurrentExplosions = 6;
                if (invaderExplosionFlashes.size() >= maxConcurrentExplosions)
                    invaderExplosionFlashes.erase (invaderExplosionFlashes.begin());
                auto& rng = juce::Random::getSystemRandom();
                invaderExplosionFlashes.push_back ({ hitX, hitY, invaderExplosionFrames,
                                                     rng.nextFloat(),
                                                     juce::jmap (rng.nextFloat(),
                                                                 invaderExplosionMinScale, 1.0f) });
                pendingExplosionIntensity.store (juce::jlimit (0.2f, 1.0f, hitBrightness / 255.0f));

                // Time of flight ends here: release the note this bullet has
                // been holding since it was fired.
                addMidiMessage (b.note, 0, false);

                // Erase what was hit from what the sequencer reads, in source
                // coordinates so the hole stays on the thing that was shot.
                addInvaderDamage (hitX, hitY, view);

                // Consecutive hits drive streak modulation, when that's on
                // (see invaderScaleNotes) — a miss below resets it.
                invaderStreak.fetch_add (1);

                // The "very short, one after the other" ascending run — but
                // now starting from the LANE THAT WAS HIT rather than always
                // from the tonic, using the sequencer's own 24-lane mapping,
                // and snapped onto the bar's 1/16 grid with 1/32 steps. 32
                // notes requested, not 24, so a hit in the topmost lane still
                // has six degrees of room above it to run through.
                auto notes = invaderScaleNotes (12, 32);
                const int lane = juce::jlimit (0, 23, (int) (hitY * 24.0f));
                const int startIndex = juce::jlimit (0, (int) notes.size() - 1, 23 - lane);
                std::vector<int> run;
                for (int k = 0; k < 6 && startIndex + k < (int) notes.size(); ++k)
                    run.push_back (notes[(size_t) (startIndex + k)]);

                // Brightness sets the dynamic, exactly as it does for a
                // sequenced note (see processSequenceMode's velocity jmap):
                // shooting a bright star hits harder than a dim one.
                const auto velocity = (uint8_t) juce::jlimit (60, 120, (int) (hitBrightness * 127.0f / 255.0f));
                scheduleInvaderRun (run, velocity, 0.25, 0.125);

                invaderBullets.erase (invaderBullets.begin() + (long) i);
                continue;
            }
            if (offscreen || expired) {
                // A miss — release the flight note and break the streak.
                addMidiMessage (b.note, 0, false);
                invaderStreak.store (0);
                invaderBullets.erase (invaderBullets.begin() + (long) i);
                continue;
            }
            ++i;
        }
    }

    // --- Grazing: fly through brightness instead of shooting it -----------
    // Sampled at the ship itself, against the same damaged `gray` the
    // sequencer reads — so a region already shot out is silent to graze over
    // too. Non-destructive: this leaves the mask alone, which is the whole
    // point of having a second verb. Rate-limited to one note every ~120ms
    // and then quantised to 1/16, so holding a line through a bright nebula
    // arpeggiates it instead of spraying.
    {
        const int px = juce::jlimit (0, grayW - 1, (int) (shipX.load() * (float) grayW));
        const int py = juce::jlimit (0, grayH - 1, (int) (shipY.load() * (float) grayH));
        constexpr int grazeRadius = 5;
        float brightest = 0.0f;
        for (int oy = -grazeRadius; oy <= grazeRadius; ++oy) {
            for (int ox = -grazeRadius; ox <= grazeRadius; ++ox) {
                const int sx = px + ox, sy = py + oy;
                if (sx < 0 || sx >= grayW || sy < 0 || sy >= grayH) continue;
                brightest = juce::jmax (brightest, (float) gray.at<uchar> (sy, sx));
            }
        }

        shipIsGrazing = brightest > threshold;
        ++ticksSinceLastGraze;
        constexpr int grazePeriodTicks = 4;   // ~120ms at run()'s ~30ms cadence
        if (shipIsGrazing && ticksSinceLastGraze >= grazePeriodTicks) {
            ticksSinceLastGraze = 0;
            const auto velocity = (uint8_t) juce::jlimit (45, 100, (int) (brightest * 110.0f / 255.0f));
            // One note, from the ship's own altitude — flying a curve through
            // a bright region plays that curve as a line.
            scheduleInvaderRun ({ invaderNoteForY (shipY.load(), 0) }, velocity, 0.25, 0.25);
        }
    }

    auto toRgbPoint = [&] (float nx, float ny) {
        return cv::Point ((int) (nx * (float) rgbW), (int) (ny * (float) rgbH));
    };

    // Every invader sprite (ship, exhaust, bullets) is sized from this rather
    // than from a pixel count, for two reasons:
    //
    //  - It's a FRACTION OF THE FRAME, so the ship is the same size relative
    //    to the picture whatever the source is. A fixed 30px triangle is fine
    //    on a 640x480 camera, but a loaded file is routinely 4000px wide (a
    //    stacked astro frame, a full-res photo), and there the same 30px was
    //    a speck — and then shrunk again by the downscale to the editor's
    //    preview, which is where "the ship is very small with a file source"
    //    came from. 480 is the reference short side that keeps the camera
    //    looking exactly as it did.
    //  - It does NOT depend on zoom. rgbFrame here is already back at ~native
    //    resolution at any zoom level (run() upscales the zoom crop by `zoom`
    //    right before overlays are drawn), so a constant fraction of it is a
    //    constant size on screen. Dividing by zoom, as this used to, made the
    //    ship shrink the further you zoomed in — the opposite of holding
    //    steady. The picture magnifies under it; the sprite doesn't.
    const float spriteScale = (float) juce::jmin (rgbW, rgbH) / 480.0f;
    auto spritePx = [&] (float pixelsAt480, float minimum) {
        return juce::jmax (minimum, pixelsAt480 * spriteScale);
    };

    // Ship: a small triangle pointing in its last movement direction.
    {
        cv::Point centre = toRgbPoint (shipX.load(), shipY.load());
        float size = spritePx (30.0f * invaderShipScale, 4.0f * invaderShipScale);
        float angle = std::atan2 (shipFacingY.load(), shipFacingX.load());
        float c = std::cos (angle), s = std::sin (angle);
        auto rotatedPoint = [&] (float localX, float localY) {
            return cv::Point (centre.x + (int) (localX * c - localY * s),
                              centre.y + (int) (localX * s + localY * c));
        };
        std::vector<cv::Point> tri {
            rotatedPoint (size, 0.0f),
            rotatedPoint (-size * 0.7f, size * 0.6f),
            rotatedPoint (-size * 0.7f, -size * 0.6f)
        };
        cv::fillConvexPoly (rgbFrame, tri, cv::Scalar (80, 220, 255), cv::LINE_AA);
        cv::polylines (rgbFrame, tri, true, cv::Scalar (255, 255, 255),
                       (int) spritePx (2.0f * invaderShipScale, 1.0f), cv::LINE_AA);

        // No grazing halo: a green ring used to be drawn around the hull
        // while shipIsGrazing, but it cluttered the ship and hid the picture
        // it was flying through. Grazing is still audible (see the graze run
        // scheduled above) — it just isn't ringed any more.
    }

    // Exhaust trails: two flickering flame licks trailing behind the ship —
    // each drawn as two tapered, overlapping triangles (a dimmer red/orange
    // outer envelope, a smaller brighter yellow-white core) pointing away
    // from the ship along the direction fixed at spawn, with a small
    // per-particle sinusoidal wobble so they read as licking flame rather
    // than a rigid wedge. Shrink and dim together as they age. Same
    // frame-relative sizing as the ship above, for the same reason — the
    // flames have to stay attached to a hull that's now sized from the frame.
    {
        float exhaustZoomScale = spriteScale * invaderShipScale;
        for (size_t i = 0; i < invaderExhaustParticles.size(); ) {
            auto& p = invaderExhaustParticles[i];
            float t = 1.0f - (float) p.framesRemaining / 14.0f;
            cv::Point originI = toRgbPoint (p.x, p.y);
            cv::Point2f origin ((float) originI.x, (float) originI.y);
            cv::Point2f dir (p.dirX, p.dirY);
            cv::Point2f perp (-p.dirY, p.dirX);
            float wobble = std::sin (p.flickerPhase + t * 9.0f) * 0.3f;

            auto flameLayer = [&] (float length, float width, cv::Scalar color) {
                cv::Point2f tip   = origin + dir * length + perp * (wobble * width * 0.5f);
                cv::Point2f baseL = origin + perp * (width * 0.5f) - dir * (width * 0.2f);
                cv::Point2f baseR = origin - perp * (width * 0.5f) - dir * (width * 0.2f);
                std::vector<cv::Point> pts {
                    cv::Point ((int) tip.x, (int) tip.y),
                    cv::Point ((int) baseL.x, (int) baseL.y),
                    cv::Point ((int) baseR.x, (int) baseR.y)
                };
                cv::fillConvexPoly (rgbFrame, pts, color, cv::LINE_AA);
            };

            float brightness = 1.0f - t;
            float outerLen = (15.0f - t * 8.0f) * exhaustZoomScale;
            float outerWid = (7.0f - t * 3.0f) * exhaustZoomScale;
            flameLayer (outerLen, outerWid,
                       cv::Scalar (255.0f * brightness, 90.0f * brightness, 15.0f * brightness));
            flameLayer (outerLen * 0.55f, outerWid * 0.5f,
                       cv::Scalar (255.0f * brightness, 210.0f * brightness, 110.0f * brightness));

            if (--p.framesRemaining <= 0) { invaderExhaustParticles.erase (invaderExhaustParticles.begin() + (long) i); continue; }
            ++i;
        }
    }

    {
        const juce::ScopedLock sl (invaderLock);
        for (auto& b : invaderBullets) {
            cv::Point p = toRgbPoint (b.x, b.y);
            // Frame-relative like the ship, and for the same reason: at 3px a
            // shot was invisible on a full-resolution file source. (Multiplying
            // by zoom, as this did, also grew the bullets while the ship was
            // shrinking — the two now track each other at every zoom level.)
            cv::circle (rgbFrame, p, (int) spritePx (3.0f, 2.0f), cv::Scalar (255, 230, 120), -1, cv::LINE_AA);
        }
    }

    // Plasma balls, drawn last so they bloom over the ship, the bullets and the
    // crater underneath. The largest one a hit can draw is HALF the frame's
    // shorter side — that one is meant to take the screen over for a moment,
    // not decorate a corner of it — but each explosion picks its own
    // `sizeScale` at spawn, anywhere from a small pop up to that, so no two
    // hits land at the same scale.
    const float explosionFullRadius = (float) juce::jmin (rgbW, rgbH) * 0.25f;
    for (size_t i = 0; i < invaderExplosionFlashes.size(); ) {
        auto& ex = invaderExplosionFlashes[i];
        const float age = 1.0f - (float) ex.framesRemaining / (float) invaderExplosionFrames;
        drawPlasmaBall (rgbFrame, toRgbPoint (ex.x, ex.y),
                        (int) (explosionFullRadius * ex.sizeScale), age, ex.seed);
        if (--ex.framesRemaining <= 0) { invaderExplosionFlashes.erase (invaderExplosionFlashes.begin() + (long) i); continue; }
        ++i;
    }
}

void VisionMidiProcessor::switchSource (int sourceId) {
    // Stop any running stream thread BEFORE taking captureLock below — that
    // thread takes the same lock itself (briefly, between reads, to publish
    // a frame) via streamCaptureLoop(), so joining it while already holding
    // the lock here would deadlock. Bounded by the read timeout that thread
    // sets on the capture (see streamCaptureLoop()), not by anything here
    // forcibly interrupting it.
    stopStreamThread.store (true);
    if (streamCaptureThread.joinable())
        streamCaptureThread.join();

    const juce::ScopedLock sl (captureLock);
    if (capture->isOpened()) capture->release();
    isStaticImage = false;
    isNetworkStream = false;
    streamReconnectCooldown = 0;
    lastStreamFrameTimeMs = 0;
    lastGoodFrame.release();
    staticImageFrame.release();
    // Every new source starts unzoomed and centred rather than carrying
    // over whatever rectangle a previous, differently-framed source had.
    viewZoom.store (1.0f);
    viewCenterX.store (0.5f);
    viewCenterY.store (0.5f);
    viewRotationDegrees.store (0.0f);

    if (sourceId == 997) {
        // Sky map mode — no capture device needed. Star-based MIDI sampling
        // (findBrightestStar/findStarsInStrip) never touches detectionThreshold,
        // so it would otherwise sit frozen at whatever a previous camera/file
        // session last left it at — and the display-side threshold crush in
        // run() (which darkens anything below it, regardless of source) would
        // then unpredictably eat into the star field. Zero it here so nothing
        // gets crushed and the sky renders exactly as SkyMapRenderer drew it.
        activeSourceId = sourceId;
        activeFilePath = "";
        detectionThreshold.store (0.0f);
    } else if (sourceId == 999) {
        juce::String customPath = appProperties->getValue ("lastFilePath");
        activeFilePath = customPath;
        if (customPath.isNotEmpty()) {
            juce::File file (customPath);
            juce::String ext = file.getFileExtension().toLowerCase();
            if (ext == ".jpg" || ext == ".jpeg" || ext == ".png") {
                staticImageFrame = loadImageFileAsMat (file);
                if (!staticImageFrame.empty()) { isStaticImage = true; activeSourceId = sourceId; }
            } else if (ext == ".fits" || ext == ".fit" || ext == ".fts") {
                staticImageFrame = loadFitsAsMat (customPath);
                if (!staticImageFrame.empty()) { isStaticImage = true; activeSourceId = sourceId; }
            } else if (customPath.contains ("://")) {
                // A network stream (rtsp://, rtmp://, http(s)://…). Marked
                // active unconditionally and handed to its own thread rather
                // than opened here inline — opening/reading it can block for
                // as long as the network needs, and that must never stall
                // this (captureLock-holding) call or run()'s 30ms MIDI-
                // generation loop. See streamCaptureLoop().
                isNetworkStream = true;
                activeSourceId = sourceId;
                stopStreamThread.store (false);
                streamCaptureThreadFinished.store (false);
                streamCaptureThread = std::thread (&VisionMidiProcessor::streamCaptureLoop, this);
            } else {
                capture->open (customPath.toStdString());
                if (capture->isOpened()) { activeSourceId = sourceId; }
            }
        }
    } else {
        activeFilePath = "";
        int cameraIndex = sourceId - 1;
        capture->open (cameraIndex, cv::CAP_AVFOUNDATION);
        if (capture->isOpened()) { activeSourceId = sourceId; }
    }
}

void VisionMidiProcessor::streamCaptureLoop() {
    // Owns `capture` exclusively for as long as this thread runs — the only
    // other code that touches it (switchSource) always joins this thread
    // first, so there's never a second thread doing so concurrently. Only
    // the state other threads actually read (lastGoodFrame and friends)
    // needs captureLock, and only for the moment it's being published, not
    // for the read itself — that's the whole point of this thread existing.
    //
    // A local copy of the shared_ptr, taken once, not `this->capture`
    // touched directly throughout — if a stalled read forces this thread to
    // be abandoned (detached) rather than joined on quit (see
    // stopAndJoinStreamCaptureThread()), `this` and its `capture` member
    // can be destroyed while this loop is still running; holding this own
    // reference keeps the real (FFmpeg-backed) object alive for as long as
    // this thread still needs it instead of it being freed out from under
    // an in-flight call — a real, reproduced SIGSEGV inside FFmpeg before
    // this fix.
    std::shared_ptr<cv::VideoCapture> captureRef = capture;

    while (! stopStreamThread.load()) {
        bool isOpen;
        { juce::ScopedLock sl (captureLock); isOpen = captureRef->isOpened(); }

        if (! isOpen) {
            if (streamReconnectCooldown <= 0) {
                juce::ScopedLock sl (captureLock);
                // CAP_PROP_READ_TIMEOUT_MSEC below only bounds a stalled
                // READ once already connected — nothing bounded the initial
                // CONNECTION attempt itself, so a dead/unreachable address
                // (exactly what a stale persisted stream URL usually is)
                // could leave open() blocked for however long the OS's own
                // TCP connect timeout is — far longer than the destructor's
                // join() of this thread waits, hanging shutdown/quit
                // indefinitely. Both are "open-only" properties (must be
                // set before open() to take effect), so this one has to be
                // set here rather than alongside the other, after.
                captureRef->set (cv::CAP_PROP_OPEN_TIMEOUT_MSEC, 3000.0);
                captureRef->open (activeFilePath.toStdString());
                if (captureRef->isOpened()) {
                    // Bounds how long a single capture>>frame below can
                    // block waiting on the next frame. Kept short — this
                    // only governs how promptly "no frame ready yet" gets
                    // reported back (real frames return long before this
                    // regardless, and the separate 30s stall threshold
                    // above tolerates a genuinely slow source just fine) —
                    // because it's also the worst-case delay the plugin
                    // destructor can be stuck waiting through on host quit,
                    // via switchSource()'s join() of this thread. That used
                    // to be 4000ms against a 2000ms destructor timeout, so
                    // the destructor could give up on run() *before* an
                    // in-flight switchSource() call here had finished, then
                    // race it to join() the same std::thread — undefined
                    // behaviour, and a very plausible crash-on-quit cause.
                    captureRef->set (cv::CAP_PROP_READ_TIMEOUT_MSEC, 1000.0);
                    lastStreamFrameTimeMs = juce::Time::getMillisecondCounter();
                }
                streamReconnectCooldown = captureRef->isOpened() ? 0 : 10;   // ~1s at this loop's 100ms poll
            } else {
                --streamReconnectCooldown;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
            continue;
        }

        cv::Mat freshFrame;
        *captureRef >> freshFrame;   // the only genuinely blocking call in this whole thread

        if (! freshFrame.empty()) {
            const juce::ScopedLock sl (captureLock);
            lastGoodFrame = freshFrame;
            lastStreamFrameTimeMs = juce::Time::getMillisecondCounter();
        } else if (juce::Time::getMillisecondCounter() - lastStreamFrameTimeMs > 30000) {
            // An empty read this long after the last real frame is a stall,
            // not a slow-but-healthy source — drop the connection so the
            // top of the loop reconnects fresh next time round.
            const juce::ScopedLock sl (captureLock);
            captureRef->release();
        }
    }
    streamCaptureThreadFinished.store (true);
}

std::vector<int> VisionMidiProcessor::getScaleNotes (MusicalMode mode, int rootNote, int numNotes) {
    std::vector<int> intervals;
    switch (mode) {
        case MusicalMode::Lydian:           intervals = {0, 2, 4, 6, 7, 9, 11}; break;
        case MusicalMode::MajorIonian:      intervals = {0, 2, 4, 5, 7, 9, 11}; break;
        case MusicalMode::MinorAeolian:     intervals = {0, 2, 3, 5, 7, 8, 10}; break;
        case MusicalMode::Dorian:           intervals = {0, 2, 3, 5, 7, 9, 10}; break;
        case MusicalMode::Mixolydian:       intervals = {0, 2, 4, 5, 7, 9, 10}; break;
        case MusicalMode::MajorPentatonic:  intervals = {0, 2, 4, 7, 9}; break;
        case MusicalMode::MinorPentatonic:  intervals = {0, 3, 5, 7, 10}; break;
        case MusicalMode::Chromatic:        intervals = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}; break;
        case MusicalMode::Locrian:          intervals = {0, 1, 3, 5, 6, 8, 10}; break;
        case MusicalMode::Phrygian:         intervals = {0, 1, 3, 5, 7, 8, 10}; break;
        case MusicalMode::HarmonicMinor:    intervals = {0, 2, 3, 5, 7, 8, 11}; break;
        case MusicalMode::MelodicMinor:     intervals = {0, 2, 3, 5, 7, 9, 11}; break;
        case MusicalMode::WholeTone:        intervals = {0, 2, 4, 6, 8, 10}; break;
        case MusicalMode::BluesScale:       intervals = {0, 3, 5, 6, 7, 10}; break;
        case MusicalMode::DoubleHarmonic:   intervals = {0, 1, 4, 5, 7, 8, 11}; break;
        case MusicalMode::Enigmatic:        intervals = {0, 1, 4, 6, 8, 10, 11}; break;
        case MusicalMode::HungarianMinor:   intervals = {0, 2, 3, 6, 7, 8, 11}; break;
        case MusicalMode::NeapolitanMajor:  intervals = {0, 1, 3, 5, 7, 9, 11}; break;
        case MusicalMode::NeapolitanMinor:  intervals = {0, 1, 3, 5, 7, 8, 11}; break;
        case MusicalMode::Prometheus:       intervals = {0, 2, 4, 6, 9, 10}; break;
        case MusicalMode::TritoneScale:     intervals = {0, 1, 4, 6, 7, 10}; break;
        case MusicalMode::InSen:            intervals = {0, 1, 5, 7, 10}; break;
        case MusicalMode::Hirajoshi:        intervals = {0, 2, 3, 7, 8}; break;
        case MusicalMode::Iwato:            intervals = {0, 1, 5, 6, 10}; break;
        case MusicalMode::Kumoi:            intervals = {0, 2, 3, 7, 9}; break;
        case MusicalMode::Pelog:            intervals = {0, 1, 3, 7, 8}; break;
        case MusicalMode::SpanishGypsy:     intervals = {0, 1, 4, 5, 7, 8, 10}; break;
        case MusicalMode::Balinese:         intervals = {0, 1, 3, 7, 8}; break;
        case MusicalMode::Byzantine:        intervals = {0, 1, 4, 5, 7, 8, 11}; break;
        case MusicalMode::OvertoneScale:    intervals = {0, 2, 4, 6, 7, 9, 10}; break;
        case MusicalMode::AlteredScale:     intervals = {0, 1, 3, 4, 6, 8, 10}; break;
        case MusicalMode::LydianAugmented:  intervals = {0, 2, 4, 6, 8, 9, 11}; break;
        case MusicalMode::LydianDominant:   intervals = {0, 2, 4, 6, 7, 9, 10}; break;
        case MusicalMode::Locrian6th:       intervals = {0, 1, 3, 5, 6, 9, 10}; break;
        case MusicalMode::SuperLocrianBb7:  intervals = {0, 1, 3, 4, 6, 8, 9}; break;
    }
    std::vector<int> notes;
    int intervalCount = (int)intervals.size();
    for (size_t i = 0; i < numNotes; ++i) {
        int octave = (int)i / intervalCount;
        int deg = (int)i % intervalCount;
        int midiNote = rootNote + (octave * 12) + intervals[deg];
        if (midiNote <= 127) notes.push_back (midiNote);
    }
    while (notes.size() < (size_t)numNotes) { notes.push_back (notes.back() + 1); }
    return notes;
}

float VisionMidiProcessor::computeSmoothedAutoThreshold (const cv::Mat& grayFrame) {
    static float smoothedAutoThresh = 128.0f;
    cv::Mat otsuDst;
    double otsuThresh = cv::threshold (grayFrame, otsuDst, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    cv::Scalar meanScalar = cv::mean (grayFrame);
    float instantThresh = (float)(otsuThresh * 0.7 + meanScalar[0] * 0.5 + 25.0);
    smoothedAutoThresh = smoothedAutoThresh * 0.95f + instantThresh * 0.05f;
    return juce::jlimit (20.0f, 250.0f, smoothedAutoThresh);
}

void VisionMidiProcessor::recordNoteTrigger (int newNote, double currentPpqVal, int velocity)
{
    const juce::ScopedLock sl (pianoRollLock);

    // Release currently active note markers
    for (auto& n : pianoRollNotes) {
        if (n.releasePpq < 0) {
            n.releasePpq = currentPpqVal;
        }
    }

    // Append newly triggered note starting at current continuous position
    if (newNote >= 0) {
        NoteEvent ev;
        ev.noteNumber = newNote;
        ev.triggerPpq = currentPpqVal;
        ev.releasePpq = -1.0;
        // Sweeps most of the hue wheel (red through violet) by pitch, rather
        // than the narrow ember band this used to sit in — jmap interpolates
        // hue linearly with no wraparound, so this stays a one-way climb up
        // to 0.85 rather than looping back through red.
        float noteT = juce::jlimit (0.0f, 1.0f, juce::jmap ((float)newNote, 36.0f, 84.0f, 0.0f, 1.0f));
        ev.colour = juce::Colour::fromHSV (
            juce::jmap (noteT, 0.0f, 0.85f),
            0.85f,
            juce::jmap (noteT, 0.65f, 1.0f),
            1.0f
        );
        pianoRollNotes.push_back (ev);

        float newLevel = std::max (currentMidiLevel.load(), (float)velocity / 127.0f);
        currentMidiLevel.store (newLevel);
        midiPeakLevel.store (std::max (midiPeakLevel.load(), newLevel));
    }

    // Prune events older than the visible window length
    double minKeepPpq = currentPpqVal - (sequenceLoopBeats.load() * 1.5);
    pianoRollNotes.erase (
        std::remove_if (pianoRollNotes.begin(), pianoRollNotes.end(),
            [minKeepPpq] (const NoteEvent& e) {
                return (e.releasePpq > 0 && e.releasePpq < minKeepPpq);
            }),
        pianoRollNotes.end()
    );
}

// Circle keeps the original procedural rainbow ring; every other type
// composites a real photograph (see Resources/celestial/, embedded via
// CMake's juce_add_binary_data — decodedCelestialImage()/compositeRGBAOnto()
// above) scaled to match radius, rather than following ringColour — a
// rainbow-cycling Jupiter wouldn't read as Jupiter. The Moon shows one fixed
// phase photo per sequencer loop step — loopStepIndex/loopStepCount map the
// current step onto the 8 real phase photos, spreading one full new-to-full-
// to-new cycle evenly across the loop, changing only at step boundaries
// (never blending or drifting between two steps); Earth instead darkens its
// own current night side by real time; every other body gets a slow spin by
// `step`, a simplified stand-in for it rotating between triggers.
void VisionMidiProcessor::drawOccultingObject (cv::Mat& rgbFrame, OccultingObjectType type, cv::Point centre,
                                               int radius, const cv::Scalar& ringColour, int ringThickness,
                                               int step, int loopStepIndex, int loopStepCount)
{
    radius = juce::jmax (2, radius);

    if (type == OccultingObjectType::Circle) {
        cv::circle (rgbFrame, centre, radius, ringColour, ringThickness, cv::LINE_AA);
        return;
    }

    cv::Mat source;
    if (type == OccultingObjectType::Moon) {
        int stepCount = juce::jmax (1, loopStepCount);
        int stepIndex = ((loopStepIndex % stepCount) + stepCount) % stepCount;
        int phaseIndex = (stepIndex * numMoonPhases) / stepCount;
        source = decodedCelestialImage (type, phaseIndex % numMoonPhases);
    } else {
        source = decodedCelestialImage (type, 0);
    }

    if (source.empty()) {
        // An asset failed to decode for some reason — still show
        // something rather than silently nothing.
        cv::circle (rgbFrame, centre, radius, ringColour, ringThickness, cv::LINE_AA);
        return;
    }

    // Saturn's rings aren't radially symmetric about the disc's own centre,
    // so spinning it like the plain-disc bodies below would make the ring
    // texture visibly swim; Sun/Moon/Earth have their own per-type treatment
    // (a steady corona, real phase photos, and the night terminator) rather
    // than a fake rotation on top.
    bool spins = (type != OccultingObjectType::Saturn && type != OccultingObjectType::Sun
                 && type != OccultingObjectType::Moon && type != OccultingObjectType::Earth);
    cv::Mat spun = spins ? rotateAboutCentre (source, (float) step * 14.0f) : source;

    cv::Mat resized;
    if (type == OccultingObjectType::Saturn) {
        // Saturn's source image is wider than tall (rings extend well past
        // the disc) — scaled so the *disc* portion, not the whole image,
        // matches every other body's radius convention. The extra 0.8
        // makes the whole ringed icon smaller overall, as asked for.
        float scale = (float) (radius * 2) * 0.8f / ((float) spun.rows * 0.65f);
        cv::resize (spun, resized, cv::Size(), scale, scale, cv::INTER_AREA);
    } else {
        cv::resize (spun, resized, cv::Size (radius * 2, radius * 2), 0, 0, cv::INTER_AREA);
    }

    if (type == OccultingObjectType::Earth)
        applyEarthTerminator (resized);   // mutates its own resized copy, not the shared cache

    compositeRGBAOnto (rgbFrame, resized, centre);
}

void VisionMidiProcessor::processSequenceMode (const cv::Mat& gray, bool isHostPlaying,
                                              const ViewTransform& view) {
    double rawPpq = 0.0;
    if (wrapperType == juce::AudioProcessor::wrapperType_Standalone) {
        rawPpq = standalonePpqPosition.load();
    } else {
        // Sampled by processBlock() on the audio thread — never queried here,
        // since this runs on run()'s thread where getPlayHead() isn't valid.
        rawPpq = hostPpqPosition.load();
    }

    if (rawPpq < lastRawPpq - 1.0) loopCount++;
    lastRawPpq = rawPpq;
    // Was hardcoded to 16 — in Standalone this is exactly the same wrap
    // point rawPpq itself now uses (see processBlock/timerCallback's fix),
    // so this stayed consistent with it there; in a host, it assumes the
    // host's own loop region matches the current Steps length, which is
    // the only length a loop-back here could meaningfully represent.
    double continuousPpqVal = rawPpq + (loopCount * sequenceLoopBeats.load());
    continuousPpq.store (continuousPpqVal);

    if (!isHostPlaying) {
        sequenceCursorX.store (-1);
        if (lastTriggeredNote != -1) {
            addMidiMessage ((uint8_t)lastTriggeredNote, 0, false);
            recordNoteTrigger (-1, continuousPpqVal);
            lastTriggeredNote = -1; lastPlayedNote.store (-1); lastStarPos = cv::Point(-1, -1);
        }
        return;
    }

    double loopBeats = sequenceLoopBeats.load();
    double barProgress = std::fmod (rawPpq, loopBeats) / loopBeats;

    int scaleIdx = currentScale.load();
    int root = currentRootNote.load();
    auto scaleNotes = getScaleNotes ((MusicalMode)scaleIdx, root, 24);

    int triggeredNote = -1; uint8_t triggeredVelocity = 0;
    lastStarPos = cv::Point(-1, -1);

    if (activeSourceId == 997) {
        // Sky map mode: sample stars along a vertical strip at the cursor's
        // current RA. Both the strip's width and height are derived from the
        // actual visible field (frame size * zoom), so what triggers a note
        // always matches what's on screen instead of a fixed guess.
        float ra = skyMapRaDegrees.load();
        float dec = skyMapDecDegrees.load();
        float zoomDegPerPixel = skyMapZoomDegPerPixel.load();
        float cosDec = std::max (0.15f, std::cos (dec * juce::MathConstants<float>::pi / 180.0f));

        float visibleHeightDegrees = (float) skyMapFrameHeight * zoomDegPerPixel;
        // Was hardcoded to 16 regardless of the Steps selector, so the
        // per-beat sampling cell was the wrong width for any other step
        // count (too narrow at 32/64, oversized at 8/4/2/1).
        float beatWidthDegrees = ((float) skyMapFrameWidth / (float) juce::jmax (1.0, loopBeats)) * zoomDegPerPixel / cosDec;
        float rowHeightPixels = (float) skyMapFrameHeight / 24.0f;
        float cellRadiusDegrees = juce::jmax (beatWidthDegrees, rowHeightPixels * zoomDegPerPixel) * 0.6f;

        // The vertical cursor line is drawn straight into the DISPLAYED frame
        // — i.e. AFTER run()'s own crop-rotate step (see the rotation applied
        // to viewFrame, far above) — so it's rotated together with the sky
        // map image whenever viewRotationDegrees is nonzero. Naively reading
        // that straight into RA/Dec (as if screen-x were always RA and
        // screen-y were always Dec) silently ignored that rotation: rotating
        // the view changed what's drawn on screen but not which stars got
        // sampled or where the overlay landed. Fixed by walking the cursor's
        // 24 pitch rows in DISPLAYED pixel space, rotating each one back by
        // -viewRotationDegrees about the frame's centre (the exact inverse
        // of the warpAffine rotation the display went through) to recover
        // the point in the renderer's own unrotated RA/Dec-aligned pixel
        // space, and only then converting that to RA/Dec — so whichever
        // star is visually under a given row after rotation is the one
        // sampled there, and the overlay (built from these same displayed
        // coordinates) lands back on exactly that star.
        float rotationRad = viewRotationDegrees.load() * juce::MathConstants<float>::pi / 180.0f;
        float cosR = std::cos (rotationRad), sinR = std::sin (rotationRad);
        float dxDisplayed = (barProgress - 0.5f) * (float) skyMapFrameWidth;

        int bestRow = -1;
        float bestMagnitude = 10.0f;
        for (int row = 0; row < 24; ++row) {
            float dyDisplayed = (row + 0.5f) * rowHeightPixels - (float) skyMapFrameHeight * 0.5f;

            // Inverse rotation (displayed -> the renderer's own unrotated
            // pixel space): transpose of the pure-rotation matrix
            // getRotationMatrix2D builds — that matrix M maps an unrotated
            // point to (α·x+β·y, -β·x+α·y) with α=cosθ, β=sinθ, which is
            // exactly what warpAffine applied going the other way (unrotated
            // -> displayed); since M is orthonormal, its inverse is its
            // transpose: (α·x-β·y, β·x+α·y).
            float dxCanon = cosR * dxDisplayed - sinR * dyDisplayed;
            float dyCanon = sinR * dxDisplayed + cosR * dyDisplayed;

            float raOffsetDeg  = dxCanon * zoomDegPerPixel / cosDec;
            float decOffsetDeg = -dyCanon * zoomDegPerPixel;   // dec increases upward, i.e. negative y

            auto [magnitude, found] = skyMapRenderer.findBrightestStar (
                ra / 15.0f + raOffsetDeg / 15.0f, dec + decOffsetDeg, cellRadiusDegrees);

            // Destructive play reaches the star catalog too: a star that's
            // been shot out is skipped here exactly as if it weren't there,
            // which is what removes its note from the loop (see
            // isViewPositionDamaged). The position tested is the DISPLAYED one
            // this row was built from, matching where the bullet actually hit.
            if (found && isViewPositionDamaged ((float) barProgress, ((float) row + 0.5f) / 24.0f, view))
                found = false;

            if (found && magnitude < bestMagnitude) {
                bestMagnitude = magnitude;
                bestRow = row;
            }
        }

        if (bestRow >= 0) {
            // bestRow runs screen top (0) -> bottom (23); scaleNotes wants
            // top = high pitch (see the fixed double-inversion note this
            // replaced), i.e. top (row 0) -> index 23.
            triggeredNote = scaleNotes[23 - bestRow];
            triggeredVelocity = juce::jlimit (30, 127, (int)(100.0f - bestMagnitude * 10.0f));

            // Already in DISPLAYED pixel space — no further rotation needed
            // before drawing, unlike the old decOffset-based position, which
            // was computed in unrotated space and would have landed in the
            // wrong place on the (now possibly rotated) display frame.
            lastStarPos = cv::Point ((int) (barProgress * gray.cols),
                                     (int) (((bestRow + 0.5f) * rowHeightPixels) / (float) skyMapFrameHeight * gray.rows));
        }

        sequenceCursorX.store ((int)(barProgress * (float) skyMapFrameWidth));
    } else if (! gray.empty()) {
        // Standard video/image mode: detect bright pixels along cursor line
        int cursorX = juce::jlimit (0, gray.cols - 1, (int)(barProgress * (double)gray.cols));
        sequenceCursorX.store (cursorX);

        float activeThresh = detectionThreshold.load();
        if (isAutoThresholdEnabled.load()) {
            activeThresh = computeSmoothedAutoThreshold (gray);
            detectionThreshold.store (activeThresh);
        }

        int maxIntensity = 0;
        float rowHeight = (float)gray.rows / 24.0f;

        for (int i = 0; i < 24; ++i) {
            int midY = (int)((i + 0.5f) * rowHeight);
            if (cursorX >= 0 && cursorX < gray.cols && midY >= 0 && midY < gray.rows) {
                int intensity = (int)gray.at<uchar>(midY, cursorX);
                if ((float)intensity > activeThresh && intensity > maxIntensity) {
                    maxIntensity = intensity;
                    triggeredNote = scaleNotes[23 - i];
                    triggeredVelocity = (uint8_t)juce::jmap(intensity, 0, 255, 30, 127);
                    lastStarPos = cv::Point(cursorX, midY);
                }
            }
        }
    } else {
        // No frame has ever arrived for this source yet — nothing to sample,
        // but continuousPpq above has already ticked and triggeredNote stays
        // -1, so the note-off logic below still runs normally instead of
        // this tick just silently doing nothing.
        sequenceCursorX.store (-1);
    }

    if (triggeredNote != lastTriggeredNote) {
        if (lastTriggeredNote != -1) addMidiMessage ((uint8_t)lastTriggeredNote, 0, false);
        if (triggeredNote != -1) {
            addMidiMessage ((uint8_t)triggeredNote, triggeredVelocity, true);
            recordNoteTrigger (triggeredNote, continuousPpqVal, triggeredVelocity);
            lastTriggeredNote = triggeredNote; lastPlayedNote.store (triggeredNote); lastPlayedVelocity.store (triggeredVelocity);
            occultingObjectStep.fetch_add (1);
        } else {
            recordNoteTrigger (-1, continuousPpqVal);
            lastTriggeredNote = -1; lastPlayedNote.store (-1); lastStarPos = cv::Point(-1, -1);
        }
    } else if (triggeredNote == -1 && lastTriggeredNote != -1) {
        addMidiMessage ((uint8_t)lastTriggeredNote, 0, false);
        recordNoteTrigger (-1, continuousPpqVal);
        lastTriggeredNote = -1; lastPlayedNote.store (-1); lastStarPos = cv::Point(-1, -1);
    }
}

void VisionMidiProcessor::run() {
    cv::Mat frame;
    while (!threadShouldExit() && isRunning) {
        if (playbackResetRequested.exchange (false)) {
            continuousPpq.store (0.0);
            lastRawPpq = 0.0;
            loopCount = 0;
            const juce::ScopedLock sl (pianoRollLock);
            pianoRollNotes.clear();
        }

        int currentTargetPath = targetSourceId.load();
        if (currentTargetPath != activeSourceId || pendingSourceChange.exchange (false)) {
            switchSource (currentTargetPath);
        }

        bool isHostPlaying = false;
        if (wrapperType == juce::AudioProcessor::wrapperType_Standalone) {
            isHostPlaying = standalonePlaying.load();
        } else {
            // Same as above: read what processBlock() sampled rather than
            // calling getPlayHead() off the audio thread.
            isHostPlaying = hostIsPlaying.load();
        }

        bool frameCaptured = false;
        {
            const juce::ScopedLock sl (captureLock);
            if (activeSourceId == 997) {
                // Sky map mode: render a sky map frame at the current RA/Dec/zoom.
                //
                // Rotation is applied HERE rather than being left to the
                // generic warp further down, because the sky is procedural:
                // we can just render a bigger piece of it and rotate that,
                // so every corner lands on real stars. The generic path can
                // only ever rotate the pixels it was given, which left the
                // swung-past corners black — visible as black wedges down
                // the sides once zoomed in and rotated. Rendering the
                // overscan costs nothing but a slightly larger draw, and the
                // result is cropped straight back to the normal frame size
                // so nothing downstream sees a rotation-dependent size.
                float rotationNow = viewRotationDegrees.load();
                if (std::abs (rotationNow) > 0.01f) {
                    float rad = rotationNow * juce::MathConstants<float>::pi / 180.0f;
                    float c = std::abs (std::cos (rad)), s = std::abs (std::sin (rad));
                    int bigW = (int) std::ceil (skyMapFrameWidth * c + skyMapFrameHeight * s);
                    int bigH = (int) std::ceil (skyMapFrameWidth * s + skyMapFrameHeight * c);

                    cv::Mat big = skyMapRenderer.renderFrame (skyMapRaDegrees.load(), skyMapDecDegrees.load(),
                                                              skyMapZoomDegPerPixel.load(),
                                                              bigW, bigH, rotationNow);
                    if (! big.empty()) {
                        cv::Point2f bigCentre ((float) big.cols / 2.0f, (float) big.rows / 2.0f);
                        cv::Mat rot = cv::getRotationMatrix2D (bigCentre, (double) rotationNow, 1.0);
                        rot.at<double> (0, 2) += (double) skyMapFrameWidth / 2.0 - (double) bigCentre.x;
                        rot.at<double> (1, 2) += (double) skyMapFrameHeight / 2.0 - (double) bigCentre.y;
                        cv::warpAffine (big, frame, rot, cv::Size (skyMapFrameWidth, skyMapFrameHeight),
                                        cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar (0, 0, 0, 0));
                    }
                } else {
                    frame = skyMapRenderer.renderFrame (skyMapRaDegrees.load(), skyMapDecDegrees.load(),
                                                        skyMapZoomDegPerPixel.load(),
                                                        skyMapFrameWidth, skyMapFrameHeight,
                                                        rotationNow);
                }
                if (!frame.empty()) frameCaptured = true;
            } else if (isStaticImage) {
                // cv::Mat's assignment is a shared-buffer refcount bump, not a copy — safe
                // here since `frame` is only ever read (cvtColor'd into other Mats), never
                // written in place. Cloning a multi-megapixel still every 30ms was pure waste.
                if (!staticImageFrame.empty()) { frame = staticImageFrame; frameCaptured = true; }
            } else if (activeSourceId == 999 && isNetworkStream) {
                // Nothing to do here — streamCaptureLoop() (its own thread,
                // started from switchSource()) owns reading this source
                // entirely, specifically so a blocking capture>>frame can
                // never stall this loop's 30ms cadence the way it used to
                // when read inline right here. It keeps lastGoodFrame
                // updated directly; the fallback just below picks it up.
            } else if (capture->isOpened()) {
                *capture >> frame;
                if (frame.empty()) { capture->set (cv::CAP_PROP_POS_FRAMES, 0); *capture >> frame; }
                if (!frame.empty()) frameCaptured = true;
            }

            if (frameCaptured) {
                lastGoodFrame = frame;
            } else if (! lastGoodFrame.empty()) {
                // No fresh frame this iteration (typically the slow-stream
                // case above) — tick MIDI generation off the last one we
                // did get rather than skipping this iteration entirely, so
                // the sequencer cursor and continuousPpq tracking (both
                // driven from inside the block below) keep advancing every
                // ~30ms regardless of how sparsely the source itself updates.
                frame = lastGoodFrame;
                frameCaptured = true;
            }
        }

        // Interactive pixel-rectangle zoom/pan — same mechanism for every
        // source, sky map included, since it crops whatever frame already
        // came out of the block above rather than caring how it got there.
        // A cheap ROI view, not a copy: cvtColor below makes its own
        // contiguous Mat from it either way. Sequencer/detection sample
        // this cropped view, and it's exactly what's displayed too, so
        // what triggers a note is always what's actually on screen.
        cv::Mat viewFrame = frame;
        ViewTransform view;   // identity until the zoom/rotate path below fills it in
        if (frameCaptured && frame.cols > 0 && frame.rows > 0) {
            float zoom = viewZoom.load();
            float rotation = viewRotationDegrees.load();
            bool zooming = zoom > 1.0001f;
            // Sky map frames arrive already rotated (see the render above,
            // which can draw the overscan rotation needs instead of leaving
            // black corners) — rotating again here would both double the
            // angle and reintroduce exactly those corners.
            bool rotating = std::abs (rotation) > 0.01f && activeSourceId != 997;

            // The view size is a function of ZOOM ALONE — rotation never
            // changes it. It used to: rotation shrank the target by whatever
            // factor made the overscan it wanted fit inside the frame, which
            // avoided black corners but did it by silently zooming in on the
            // user's behalf. That read as "rotating activates the zoom", and
            // because the shrink also changed the output frame's dimensions
            // it moved the image's aspect ratio, which the editor then
            // followed by resizing the whole plugin window mid-rotation.
            // Keeping this rotation-independent fixes both at once: corners
            // the rotation swings past the available pixels are simply left
            // black, which is the honest result of rotating a rectangle.
            float targetW = zooming ? (float) frame.cols / zoom : (float) frame.cols;
            float targetH = zooming ? (float) frame.rows / zoom : (float) frame.rows;

            int targetWi = juce::jmax (1, (int) targetW);
            int targetHi = juce::jmax (1, (int) targetH);
            int sourceWi = targetWi, sourceHi = targetHi;
            if (rotating) {
                float rad = rotation * juce::MathConstants<float>::pi / 180.0f;
                float c = std::abs (std::cos (rad)), s = std::abs (std::sin (rad));
                sourceWi = juce::jmin (frame.cols, (int) std::ceil (targetW * c + targetH * s));
                sourceHi = juce::jmin (frame.rows, (int) std::ceil (targetW * s + targetH * c));
            }

            if (zooming || rotating) {
                int x = juce::jlimit (0, frame.cols - sourceWi,
                                      (int) (viewCenterX.load() * (float) frame.cols - (float) sourceWi * 0.5f));
                int y = juce::jlimit (0, frame.rows - sourceHi,
                                      (int) (viewCenterY.load() * (float) frame.rows - (float) sourceHi * 0.5f));
                viewFrame = frame (cv::Rect (x, y, sourceWi, sourceHi));

                // Remember where this view came from, so Invader mode's damage
                // can be stored against the source image rather than the
                // screen (see ViewTransform / addInvaderDamage).
                view.cropX0 = (float) x / (float) frame.cols;
                view.cropY0 = (float) y / (float) frame.rows;
                view.cropW  = (float) sourceWi / (float) frame.cols;
                view.cropH  = (float) sourceHi / (float) frame.rows;
            }
            view.rotationDegrees = rotating ? rotation : 0.0f;

            // Rotation about the crop's own centre — after the zoom crop,
            // not before, so the rotation pivot is always the middle of
            // what's currently on screen rather than the middle of the
            // full, unzoomed source frame. warpAffine always produces a
            // fresh Mat (never an aliased ROI like the crop above), so
            // this is safe to reassign into viewFrame even though frame
            // itself is untouched.
            if (rotating) {
                // Warped straight into a targetWi x targetHi output rather
                // than rotated at the source crop's size and cropped back
                // down after: the output dimensions are then exactly the
                // zoom's, every frame, whatever the angle. That's what stops
                // the image's aspect ratio wobbling as it turns (and with it
                // the window resizing itself). The extra translation shifts
                // the source crop's centre onto the output's centre, since
                // the overscanned crop is generally larger than the target.
                cv::Point2f centre ((float) viewFrame.cols / 2.0f, (float) viewFrame.rows / 2.0f);
                cv::Mat rot = cv::getRotationMatrix2D (centre, (double) rotation, 1.0);
                rot.at<double> (0, 2) += (double) targetWi / 2.0 - (double) centre.x;
                rot.at<double> (1, 2) += (double) targetHi / 2.0 - (double) centre.y;

                cv::Mat rotated;
                cv::warpAffine (viewFrame, rotated, rot, cv::Size (targetWi, targetHi), cv::INTER_LINEAR,
                                cv::BORDER_CONSTANT, cv::Scalar (0, 0, 0, 0));
                viewFrame = rotated;
            }
        }

        // Converted once here and shared by sequence processing and the
        // display threshold-crush below, instead of each re-deriving its
        // own grayscale copy of the same frame. Left empty when !frameCaptured
        // (no source active yet, or a network stream that's never produced a
        // single frame) rather than skipping the call below entirely —
        // processSequenceMode ticks continuousPpq and the sequencer cursor
        // unconditionally and just skips pixel sampling on an empty Mat, so
        // the sequencer's timeline can never be blocked by a video source
        // failing to deliver frames, however it fails to.
        cv::Mat gray;
        if (frameCaptured) {
            if (viewFrame.channels() == 3) cv::cvtColor (viewFrame, gray, cv::COLOR_BGR2GRAY);
            else if (viewFrame.channels() == 4) cv::cvtColor (viewFrame, gray, cv::COLOR_BGRA2GRAY);
            // .clone(), not an alias: a single-channel source (a mono FITS
            // stack, say) would otherwise share pixels with viewFrame — and
            // through it with the cached staticImageFrame — so Invader mode
            // subtracting its damage below would permanently eat holes in the
            // stored source image and in what's displayed, not just in what
            // the sequencer samples.
            else gray = viewFrame.clone();
        }

        // Destructive play: whatever's been shot is subtracted from `gray`
        // BEFORE the sequencer samples it, and stays subtracted for the
        // invader's own collision test further down (so a crater can't be shot
        // twice). Only runs while the mode is on, so it costs nothing
        // otherwise.
        if (invaderModeEnabled.load())
            applyInvaderDamage (gray, view);

        processSequenceMode (gray, isHostPlaying, view);

        if (frameCaptured) {
            if (lastPlayedNote.load() >= 0 && lastStarPos.x >= 0) {
                persistentStarPos = lastStarPos; circleHoldFramesRemaining = 45;
            }

            cv::Scalar emberCircleColourRGB (0, 0, 0);
            bool drawEmberCircle = false;
            int emberCircleRadius = 10, emberCircleThickness = 2;
            if (circleHoldFramesRemaining > 0 && persistentStarPos.x >= 0) {
                // Full rainbow sweep, one complete cycle every ~10s at this
                // loop's ~30ms cadence (180 hues / ~333 frames).
                constexpr int framesPerCycle = 333;
                rainbowHueCounter = (rainbowHueCounter + 1) % framesPerCycle;
                int emberHue = (rainbowHueCounter * 180) / framesPerCycle;
                cv::Mat hsvMat (1, 1, CV_8UC3, cv::Scalar (emberHue, 235, 255)), rgbMat;
                cv::cvtColor (hsvMat, rgbMat, cv::COLOR_HSV2RGB);
                cv::Vec3b col = rgbMat.at<cv::Vec3b>(0, 0);
                emberCircleColourRGB = cv::Scalar (col[0], col[1], col[2]);
                drawEmberCircle = true;
                circleHoldFramesRemaining--;

                // A plain constant radius here, not divided by zoom — see the
                // zoom-compensating upscale of rgbFrame just below, which is
                // what now keeps this visually constant-sized on screen.
                // (Dividing by zoom used to be how that was done, directly in
                // viewFrame's own shrunken-by-zoom pixel space, but that meant
                // the real celestial photo got resized down to only a handful
                // of pixels at high zoom before being stretched back up for
                // display — irrecoverably blocky. Drawing it at a fixed size
                // AFTER restoring the frame to native resolution gives it the
                // same real pixel budget regardless of zoom.) Only the
                // +/-30% velocity variation remains here (unifying pixel
                // brightness and star magnitude onto one scale, whichever
                // mode triggered this).
                int velocity = juce::jlimit (0, 127, lastPlayedVelocity.load());
                float magnitudeScale = juce::jmap ((float) velocity, 0.0f, 127.0f, 0.7f, 1.3f);
                float targetRadius = juce::jmax (3.0f, 60.0f * magnitudeScale);
                // Eased toward the target rather than snapped straight to
                // it — every new note's velocity used to make the size
                // jump immediately, which read as jittery/twitchy rather
                // than alive. ~0.15/tick at this loop's ~30ms cadence is
                // about a quarter-second to settle, still responsive.
                smoothedOccultingObjectRadius += (targetRadius - smoothedOccultingObjectRadius) * 0.15f;
                emberCircleRadius = juce::jmax (3, (int) smoothedOccultingObjectRadius);
                emberCircleThickness = 6;
            }

            cv::Mat rgbFrame;
            if (viewFrame.channels() == 3) cv::cvtColor (viewFrame, rgbFrame, cv::COLOR_BGR2RGB);
            else if (viewFrame.channels() == 4) cv::cvtColor (viewFrame, rgbFrame, cv::COLOR_BGRA2RGB);
            else cv::cvtColor (viewFrame, rgbFrame, cv::COLOR_GRAY2RGB);

            // Pixels below the detection threshold are crushed to black so the feed
            // shows exactly what the detector currently treats as "dark".
            float displayThresh = detectionThreshold.load();
            cv::Mat belowThreshMask;
            cv::threshold (gray, belowThreshMask, (double) displayThresh, 255.0, cv::THRESH_BINARY_INV);
            rgbFrame.setTo (cv::Scalar (0, 0, 0), belowThreshMask);

            // viewFrame (and so rgbFrame, still the same size here) shrinks as
            // viewZoom grows — see the crop that built viewFrame, far above.
            // Sequence sampling above needed that actual shrunk
            // frame (so a magnitude/threshold reading always matches what's
            // on screen), but drawing overlays into a canvas that's already
            // been crushed down to a fraction of its native resolution is
            // what made the Occulting Object (and the sequence cursor line)
            // pixelate at high zoom, however good the resize used to render
            // them. Undoing exactly that shrink now, right before overlays
            // go on — a real digital-zoom upscale, not a stretch — restores
            // a full native-resolution canvas to draw them into, so they get
            // the same pixel budget at any zoom level. (The image content
            // itself was already fully sampled at the shrunk resolution, so
            // this can only make it as sharp as it was pre-crop, not sharper.)
            float zoom = juce::jmax (1.0f, viewZoom.load());
            int cursorXNow = sequenceCursorX.load();
            // A local copy to draw at, scaled into the upscaled frame's
            // coordinate space below — persistentStarPos itself must stay in
            // viewFrame's original (unscaled) space, since it's a member
            // that's read again next frame; scaling it in place here would
            // compound every frame zoom is active, drifting the overlay off
            // its note position more with every tick.
            cv::Point drawStarPos = persistentStarPos;
            if (zoom > 1.0001f) {
                cv::Mat upscaledFrame;
                cv::resize (rgbFrame, upscaledFrame, cv::Size(), (double) zoom, (double) zoom, cv::INTER_LINEAR);
                rgbFrame = upscaledFrame;
                if (cursorXNow >= 0) cursorXNow = (int) ((float) cursorXNow * zoom);
                if (drawStarPos.x >= 0) {
                    drawStarPos.x = (int) ((float) drawStarPos.x * zoom);
                    drawStarPos.y = (int) ((float) drawStarPos.y * zoom);
                }
            }

            // Overlay markers are drawn AFTER the crush, directly in RGB, so they
            // stay fully visible regardless of Thresh instead of being crushed
            // away like any other dark-background pixel.
            if (cursorXNow >= 0)
                cv::line (rgbFrame, cv::Point (cursorXNow, 0), cv::Point (cursorXNow, rgbFrame.rows),
                          cv::Scalar (226, 80, 10), (int) juce::jmax (2.0f, 2.0f * zoom), cv::LINE_AA);

            if (drawEmberCircle) {
                // The Moon's phase is one fixed position of the cycle per
                // sequencer loop step, rather than the discrete note-
                // triggered step counter or a continuously-drifting time
                // fraction — loopStepIndex is which beat of the loop is
                // currently playing (constant for that whole beat), spread
                // evenly across the 8 real phase photos over one full loop.
                int loopStepCount = juce::jmax (1, (int) sequenceLoopBeats.load());
                int loopStepIndex = (int) std::floor (std::fmod (continuousPpq.load(), (double) loopStepCount));
                drawOccultingObject (rgbFrame, occultingObject.load(), drawStarPos,
                                     emberCircleRadius, emberCircleColourRGB, emberCircleThickness,
                                     occultingObjectStep.load(), loopStepIndex, loopStepCount);
            }

            // Ship/bullets/explosions — no-ops immediately unless Invader
            // mode is on. Drawn into the same already zoom-upscaled canvas
            // as the overlays above, and collides against `gray` (the
            // shrunk, pre-upscale view) exactly like the sequencer's own
            // star sampling does, so a hit always matches what's on screen.
            updateAndDrawInvaderMode (rgbFrame, gray, view);

            // The editor only ever shows this stretched into a small preview panel
            // (see VisionMidiEditor::paint), so building and re-copying it into a
            // CoreGraphics image at full capture resolution (e.g. 1920x1080 from a
            // FITS stack) every ~30ms burns CPU for detail nobody can see on screen.
            // Downscale once here, after overlay drawing, so both this copy and the
            // per-repaint CGImage rebuild on the UI thread work on far fewer pixels.
            constexpr int maxDisplayDim = 960;
            int longestEdge = std::max (rgbFrame.cols, rgbFrame.rows);
            if (longestEdge > maxDisplayDim) {
                double scale = (double) maxDisplayDim / (double) longestEdge;
                cv::Mat scaledFrame;
                cv::resize (rgbFrame, scaledFrame, cv::Size(), scale, scale, cv::INTER_AREA);
                rgbFrame = scaledFrame;
            }

            {
                const juce::ScopedLock sl (imageLock);
                juceImage = juce::Image (juce::Image::RGB, rgbFrame.cols, rgbFrame.rows, true);
                juce::Image::BitmapData data (juceImage, juce::Image::BitmapData::readWrite);

                // Native image formats (e.g. CoreGraphics on macOS) may store pixels as
                // 4-byte ARGB even when Image::RGB is requested, so a flat 3-bytes-per-pixel
                // memcpy of each row silently truncates the right edge of every frame. Check
                // the stride once and use OpenCV's vectorized conversion + a per-row memcpy
                // instead of a manual scalar loop over every pixel (the previous approach
                // burned a full CPU core at typical camera/FITS resolutions).
                //
                // JUCE's PixelARGB (the 4-byte case) is NOT byte-order R,G,B,A on a
                // little-endian desktop build — juce_PixelFormats.h's non-Android,
                // non-big-endian branch sets indexB=0, indexG=1, indexR=2, indexA=3, i.e.
                // the real in-memory order is B,G,R,A. (PixelRGB, the 3-byte case below,
                // really is R,G,B on JUCE_MAC, so that branch is unaffected.) Converting
                // with COLOR_RGB2RGBA and memcpy'ing that straight in swapped red and blue
                // on every pixel with red != blue — invisible on the mostly-grey video/star
                // feed, but glaring on any saturated colour, which is exactly why the
                // Occulting Object photos (a yellow Sun rendering as light blue, i.e.
                // Uranus's actual colour with red and blue swapped) exposed it.
                // COLOR_RGB2BGRA produces the B,G,R,A byte order this format actually needs.
                if (data.pixelStride == 4) {
                    cv::Mat rgbaFrame;
                    cv::cvtColor (rgbFrame, rgbaFrame, cv::COLOR_RGB2BGRA);
                    for (int y = 0; y < rgbaFrame.rows; ++y)
                        std::memcpy (data.getLinePointer (y), rgbaFrame.ptr (y), (size_t) rgbaFrame.cols * 4);
                } else {
                    for (int y = 0; y < rgbFrame.rows; ++y)
                        std::memcpy (data.getLinePointer (y), rgbFrame.ptr (y), (size_t) rgbFrame.cols * 3);
                }
            }
        }

        // Ship velocity integration/drag — unconditional like the meter
        // decay below, so it keeps drifting smoothly through a stalled or
        // missing frame instead of freezing.
        updateShipPhysics();

        // Meter decay lives here (not in the editor's UI timer) so it keeps
        // decaying at the same real-world rate whether or not the editor is
        // open to look at it.
        currentMidiLevel.store (currentMidiLevel.load() * 0.82f);
        midiPeakLevel.store (midiPeakLevel.load() * 0.95f);
        currentAudioLevel.store (currentAudioLevel.load() * 0.82f);
        audioPeakLevel.store (audioPeakLevel.load() * 0.95f);

        // Drains the invader layer's scheduled notes — impacts, grazes and the
        // engine hum (see scheduleInvaderRun). Quantised events come due by
        // PPQ, against the very same timeline the sequencer runs on;
        // unquantised ones (queued with no transport to snap to) still step a
        // tick at a time. The laser no longer appears here at all: its note is
        // held for its bullet's whole flight and released when that bullet
        // resolves.
        {
            const bool playing = transportRunning();
            const double nowPpq = continuousPpq.load();
            for (size_t i = 0; i < pendingInvaderNotes.size(); ) {
                auto& ev = pendingInvaderNotes[i];
                bool due;
                if (ev.firePpq >= 0.0) {
                    if (! playing) {
                        // The transport stopped while this was still waiting,
                        // so its PPQ will never arrive. Drop pending note-ONS
                        // (their moment has gone) but still send the OFFs, or
                        // whatever was already sounding stays on forever.
                        if (! ev.isOn) addMidiMessage (ev.note, 0, false);
                        pendingInvaderNotes.erase (pendingInvaderNotes.begin() + (long) i);
                        continue;
                    }
                    due = nowPpq >= ev.firePpq;
                } else {
                    due = ev.ticksUntilFire <= 0;
                    if (! due) --ev.ticksUntilFire;
                }

                if (due) {
                    addMidiMessage (ev.note, ev.velocity, ev.isOn);
                    pendingInvaderNotes.erase (pendingInvaderNotes.begin() + (long) i);
                    continue;
                }
                ++i;
            }
        }

        std::this_thread::sleep_for (std::chrono::milliseconds(30));
    }
    // Skipped for a network stream: `capture` belongs exclusively to
    // streamCaptureLoop() in that case (still running independently right
    // now — this only runs once run()'s own loop exits, which happens
    // before the destructor stops that thread), and calling release() here
    // concurrently with its blocking capture>>frame read is a real data
    // race on the same cv::VideoCapture object — reproduced as a SIGSEGV
    // inside FFmpeg. streamCaptureLoop()/stopAndJoinStreamCaptureThread()
    // own tearing it down instead.
    if (! isNetworkStream) {
        const juce::ScopedLock sl (captureLock);
        if (capture->isOpened()) capture->release();
    }
}

//==============================================================================
bool VisionMidiProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* VisionMidiProcessor::createEditor() { return new VisionMidiEditor (*this); }

//==============================================================================
juce::File VisionMidiProcessor::getPresetDirectory() const
{
    // userApplicationDataDirectory is ~/Library on macOS, NOT
    // ~/Library/Application Support — PropertiesFile appends that itself,
    // which is why Occultation.settings lands there while this needs to add
    // it explicitly to sit beside it rather than dumping a folder straight
    // into ~/Library.
    auto root = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    root = root.getChildFile ("Application Support");
   #endif
    return root.getChildFile ("Occultation").getChildFile ("Presets");
}

juce::StringArray VisionMidiProcessor::getPresetNames() const
{
    juce::StringArray names;
    auto dir = getPresetDirectory();
    if (dir.isDirectory())
        for (const auto& f : dir.findChildFiles (juce::File::findFiles, false,
                                                  juce::String ("*") + presetFileExtension))
            names.add (f.getFileNameWithoutExtension());

    names.sortNatural();
    return names;
}

bool VisionMidiProcessor::savePreset (const juce::String& name)
{
    auto trimmed = name.trim();
    // Factory presets are the built-in table, not files — a user preset is
    // never allowed to shadow one, or the same name would mean two things.
    if (trimmed.isEmpty() || isFactoryPreset (trimmed)) return false;

    auto dir = getPresetDirectory();
    if (! dir.isDirectory() && ! dir.createDirectory()) return false;

    auto state = apvts.copyState();
    auto xml = state.createXml();
    if (xml == nullptr) return false;

    auto file = dir.getChildFile (juce::File::createLegalFileName (trimmed) + presetFileExtension);
    if (! xml->writeTo (file)) return false;

    currentPresetName = trimmed;
    return true;
}

juce::StringArray VisionMidiProcessor::getFactoryPresetNames() const
{
    juce::StringArray names;
    for (int i = 0; i < (int) SynthEngine::SynthType::NumTypes; ++i)
        names.add (SynthEngine::getTypeName ((SynthEngine::SynthType) i));
    return names;
}

bool VisionMidiProcessor::isFactoryPreset (const juce::String& name) const
{
    return getFactoryPresetNames().contains (name.trim());
}

juce::StringArray VisionMidiProcessor::getAllPresetNames() const
{
    auto names = getFactoryPresetNames();
    names.addArray (getPresetNames());
    return names;
}

bool VisionMidiProcessor::loadPreset (const juce::String& name)
{
    auto trimmed = name.trim();

    // Factory presets aren't files — they're the built-in table, pushed into
    // the parameters so every knob reflects the voice that was picked.
    int factoryIndex = getFactoryPresetNames().indexOf (trimmed);
    if (factoryIndex >= 0) {
        const auto& p = SynthEngine::getTypePreset ((SynthEngine::SynthType) factoryIndex);

        auto setChoice = [this] (const char* id, int index) {
            if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id)))
                *param = index;
        };
        auto setFloat = [this] (const char* id, float value) {
            if (auto* param = apvts.getParameter (id))
                param->setValueNotifyingHost (param->convertTo0to1 (value));
        };

        setChoice ("waveform", (int) p.waveform);   setFloat ("unisonDetune", p.unisonDetuneCents);
        setFloat ("attack", p.attack);              setFloat ("decay", p.decay);
        setFloat ("sustain", p.sustain);            setFloat ("release", p.release);
        setChoice ("filterType", (int) p.filterType);
        setFloat ("filterCutoff", p.filterCutoffHz);
        setFloat ("filterResonance", p.filterResonance01);
        setFloat ("filterEnvAmount", p.filterEnvAmount01);
        setFloat ("distortionAmount", p.distortionAmount01);
        setFloat ("chorusAmount", p.chorusAmount01);
        setFloat ("delayAmount", p.delayAmount01);
        setFloat ("delayTime", p.delayTimeSeconds);
        setFloat ("reverbAmount", p.reverbAmount01);

        currentPresetName = trimmed;
        return true;
    }

    auto file = getPresetDirectory().getChildFile (juce::File::createLegalFileName (trimmed)
                                                    + presetFileExtension);
    if (! file.existsAsFile()) return false;

    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return false;

    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    currentPresetName = trimmed;
    return true;
}

bool VisionMidiProcessor::deletePreset (const juce::String& name)
{
    if (isFactoryPreset (name)) return false;

    auto file = getPresetDirectory().getChildFile (juce::File::createLegalFileName (name.trim())
                                                    + presetFileExtension);
    if (! file.existsAsFile() || ! file.deleteFile()) return false;

    if (currentPresetName == name.trim()) currentPresetName = {};
    return true;
}

//==============================================================================
void VisionMidiProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState().createXml())
        copyXmlToBinary (*state, destData);
}

void VisionMidiProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// One AudioParameterChoice per SynthEngine enum, plus one float parameter
// per knob — kept as a flat list (not nested groups) since APVTS attachments
// in the editor just need a plain parameter ID to bind to. Ranges chosen to
// match what SynthEngine's setters expect directly, so applyParametersToSynth()
// below is a straight passthrough with no rescaling.
juce::AudioProcessorValueTreeState::ParameterLayout VisionMidiProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // No "synthType" parameter: the built-in voices are factory presets now
    // (see loadPreset), so the parameter list is purely the knobs a preset
    // actually stores. A Type parameter alongside them was a second source
    // of truth that overwrote whatever a preset had just loaded.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "waveform", "Waveform", juce::StringArray { "Sine", "Saw", "Square", "Triangle", "Noise" }, 1));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "unisonDetune", "Unison Detune", juce::NormalisableRange<float> (0.0f, 25.0f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "attack", "Attack", juce::NormalisableRange<float> (0.001f, 5.0f, 0.0f, 0.4f), 0.01f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "decay", "Decay", juce::NormalisableRange<float> (0.001f, 5.0f, 0.0f, 0.4f), 0.15f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "sustain", "Sustain", juce::NormalisableRange<float> (0.0f, 1.0f), 0.7f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "release", "Release", juce::NormalisableRange<float> (0.001f, 5.0f, 0.0f, 0.4f), 0.3f));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "filterType", "Filter Type", juce::StringArray { "State Variable", "Moog Ladder" }, 1));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "filterCutoff", "Filter Cutoff", juce::NormalisableRange<float> (20.0f, 18000.0f, 0.0f, 0.3f), 2000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "filterResonance", "Filter Resonance", juce::NormalisableRange<float> (0.0f, 1.0f), 0.2f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "filterEnvAmount", "Filter Env Amount", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "masterVolume", "Master Volume", juce::NormalisableRange<float> (0.0f, 1.0f), 0.7f));
    // Portamento, in seconds, off by default. Skewed hard towards the bottom
    // of the range: everything musically useful happens in the first 200ms or
    // so, and a linear 0-1s knob would bury all of it in the first fifth of
    // the travel.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "glide", "Glide", juce::NormalisableRange<float> (0.0f, 1.0f, 0.0f, 0.35f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "distortionAmount", "Distortion", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "chorusAmount", "Chorus", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "delayAmount", "Delay Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "delayTime", "Delay Time", juce::NormalisableRange<float> (0.02f, 2.0f, 0.0f, 0.4f), 0.25f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "reverbAmount", "Reverb", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    return { params.begin(), params.end() };
}

// Called once per processBlock (see there) rather than only on parameter
// change — every setter here is a cheap plain-float assignment in
// SynthEngine, and reading current APVTS values unconditionally avoids
// needing a separate change-listener/dirty-flag mechanism entirely.
void VisionMidiProcessor::applyParametersToSynth()
{

    synthEngine.setWaveform ((SynthEngine::Waveform) dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("waveform"))->getIndex());
    synthEngine.setUnisonDetuneCents (apvts.getRawParameterValue ("unisonDetune")->load());
    synthEngine.setAttack (apvts.getRawParameterValue ("attack")->load());
    synthEngine.setDecay (apvts.getRawParameterValue ("decay")->load());
    synthEngine.setSustain (apvts.getRawParameterValue ("sustain")->load());
    synthEngine.setRelease (apvts.getRawParameterValue ("release")->load());
    synthEngine.setFilterType ((SynthEngine::FilterType) dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("filterType"))->getIndex());
    synthEngine.setFilterCutoffHz (apvts.getRawParameterValue ("filterCutoff")->load());
    synthEngine.setFilterResonance01 (apvts.getRawParameterValue ("filterResonance")->load());
    synthEngine.setFilterEnvAmount01 (apvts.getRawParameterValue ("filterEnvAmount")->load());
    synthEngine.setMasterVolume01 (apvts.getRawParameterValue ("masterVolume")->load());
    synthEngine.setGlideSeconds (apvts.getRawParameterValue ("glide")->load());
    synthEngine.setDistortionAmount01 (apvts.getRawParameterValue ("distortionAmount")->load());
    synthEngine.setChorusAmount01 (apvts.getRawParameterValue ("chorusAmount")->load());
    synthEngine.setDelayAmount01 (apvts.getRawParameterValue ("delayAmount")->load());
    synthEngine.setDelayTimeSeconds (apvts.getRawParameterValue ("delayTime")->load());
    synthEngine.setReverbAmount01 (apvts.getRawParameterValue ("reverbAmount")->load());
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VisionMidiProcessor();
}

// ==============================================================================
// Custom Standalone Application Overrides (Prevents hardware device reconfiguration)
// ==============================================================================
#if JUCE_BUILD_STANDALONE_FILTER
#include <juce_audio_plugin_client/standalone/juce_StandaloneFilterWindow.h>

class CustomStandaloneApplication : public juce::StandaloneFilterApplication
{
public:
    CustomStandaloneApplication() : juce::StandaloneFilterApplication() {}

    void init() override
    {
        juce::StandaloneFilterApplication::init();

        // Retrieve top-level StandaloneFilterWindow and configure its AudioDeviceManager
        if (auto* window = std::dynamic_pointer_cast<juce::StandaloneFilterWindow>(
                juce::Desktop::getInstance().getTopLevelComp(0)))
        {
            auto& deviceManager = window->getDeviceManager();

            juce::AudioDeviceManager::AudioDeviceSetup setup;
            deviceManager.getAudioDeviceSetup(setup);

            // Sample rate of 0 tells JUCE to adopt the hardware device's current rate
            setup.sampleRate = 0;

            // Re-initialize with current settings without reconfiguring hardware
            deviceManager.initialise(0, 2, nullptr, true, {}, &setup);
        }
    }
};

START_JUCE_APPLICATION (CustomStandaloneApplication)
#endif
