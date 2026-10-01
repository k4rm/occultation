#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <utility>
#include <vector>

// Draws an equatorial star chart straight into the output frame: real stars
// sized and brightened by visual magnitude, constellation figures, Messier
// objects and a faint RA/Dec grid.
//
// There is deliberately no tile cache. An earlier tile-based version could
// leave the cache unpopulated on the very first frame (the "has the view
// moved?" test compared against defaults that matched the initial view
// exactly), which rendered an empty sky forever. The catalogue is small
// enough that drawing it per frame costs nothing, and having a single code
// path means the pixels can never disagree with the requested view.
class SkyMapRenderer
{
public:
    SkyMapRenderer() = default;

    // raDegrees 0-360, decDegrees -90..+90, zoomDegPerPixel sets the scale.
    // Returns an 8-bit BGR frame. rotationDegrees is the CALLER's own
    // whole-frame view rotation it's about to apply on top of this frame
    // (PluginProcessor's viewRotationDegrees) — not applied here, only used
    // to pre-counter-rotate constellation label TEXT (see putRotatedLabel in
    // the .cpp) so that once the caller's rotation lands, the labels read
    // upright on screen while still tracking their constellation's rotated
    // position, instead of spinning along with everything else and needing
    // a tilted head to read.
    cv::Mat renderFrame (float raDegrees, float decDegrees, float zoomDegPerPixel, int width, int height,
                        float rotationDegrees = 0.0f);

    // Brightest star within radiusDegrees of a position, as {magnitude, found}.
    std::pair<float, bool> findBrightestStar (float raHours, float decDegrees, float radiusDegrees) const;

    // Stars inside a RA strip, as {magnitude, decDegrees}, north first.
    std::vector<std::pair<float, float>> findStarsInStrip (float raCenterHours, float decCenterDegrees,
                                                           float raWidthHours, float stripHeightDegrees) const;

    struct ConstellationInfo { std::string name; float raHours; float decDegrees; };

    // The constellations drawn by renderFrame, each with the anchor point
    // its name is labelled at — lets UI code (a "jump to constellation"
    // picker) reuse the same list instead of duplicating it.
    static std::vector<ConstellationInfo> listConstellations();

private:
    // Equirectangular projection about the view centre. RA is scaled by
    // cos(centre dec) so constellations keep their shape near the middle of
    // the view instead of smearing sideways at high declination.
    static bool raDecToPixel (float raHours, float decDeg,
                              float centreRaDeg, float centreDecDeg, float cosCentreDec,
                              float zoomDegPerPixel, int width, int height,
                              float& pixelX, float& pixelY);

    static int magnitudeToBrightness (float magnitude);
};
