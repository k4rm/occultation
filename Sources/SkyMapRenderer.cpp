#include "SkyMapRenderer.h"
#include <opencv2/geometry/2d.hpp>   // getRotationMatrix2D — not pulled in by the umbrella header in OpenCV 5.x

#include <algorithm>
#include <cmath>
#include <iterator>

// ---------------------------------------------------------------------------
// Catalogue
//
// Positions are J2000, RA in decimal hours, Dec in decimal degrees. Every star
// that takes part in a constellation figure is a named constant here, and both
// the star list and the figure list are built from those same constants, so a
// figure can never point somewhere the star isn't.
//
// Array lengths are always taken with std::size() rather than a hand-kept
// count. A previous version declared BRIGHT_STARS[500] with ~30 initialisers,
// which left ~470 zero-filled entries that rendered as a knot of phantom
// magnitude-0 stars at RA 0h/Dec 0.
// ---------------------------------------------------------------------------

namespace {

struct SkyPoint { float ra; float dec; };
struct Star     { SkyPoint p; float mag; };
struct DSO      { SkyPoint p; float mag; };
struct Figure   { const SkyPoint* pts; int count; };
struct ConstellationLabel { SkyPoint anchor; const char* name; };

// --- Orion ---
constexpr SkyPoint Betelgeuse  {5.9195f,   7.4071f};
constexpr SkyPoint Rigel       {5.2423f,  -8.2016f};
constexpr SkyPoint Bellatrix   {5.4188f,   6.3497f};
constexpr SkyPoint Mintaka     {5.5334f,  -0.2991f};
constexpr SkyPoint Alnilam     {5.6036f,  -1.2019f};
constexpr SkyPoint Alnitak     {5.6793f,  -1.9426f};
constexpr SkyPoint Saiph       {5.7959f,  -9.6696f};
constexpr SkyPoint Meissa      {5.5855f,   9.9342f};
constexpr SkyPoint EtaOri      {5.4076f,  -2.3973f};

// --- Ursa Major ---
constexpr SkyPoint Dubhe       {11.0621f, 61.7510f};
constexpr SkyPoint Merak       {11.0307f, 56.3824f};
constexpr SkyPoint Phecda      {11.8972f, 53.6948f};
constexpr SkyPoint Megrez      {12.2571f, 57.0326f};
constexpr SkyPoint Alioth      {12.9005f, 55.9598f};
constexpr SkyPoint Mizar       {13.3988f, 54.9254f};
constexpr SkyPoint Alkaid      {13.7923f, 49.3133f};

// --- Ursa Minor ---
constexpr SkyPoint Polaris     {2.5303f,  89.2641f};
constexpr SkyPoint Kochab      {14.8451f, 74.1555f};
constexpr SkyPoint Pherkad     {15.3455f, 71.8340f};
constexpr SkyPoint DeltaUMi    {17.5369f, 86.5865f};
constexpr SkyPoint EpsilonUMi  {16.7661f, 82.0373f};
constexpr SkyPoint ZetaUMi     {15.7344f, 77.7945f};
constexpr SkyPoint EtaUMi      {16.2919f, 75.7550f};

// --- Cassiopeia ---
constexpr SkyPoint Schedar     {0.6751f,  56.5373f};
constexpr SkyPoint Caph        {0.1530f,  59.1498f};
constexpr SkyPoint GammaCas    {0.9451f,  60.7167f};
constexpr SkyPoint Ruchbah     {1.4304f,  60.2353f};
constexpr SkyPoint EpsilonCas  {1.9066f,  63.6701f};

// --- Cygnus ---
constexpr SkyPoint Deneb       {20.6905f, 45.2803f};
constexpr SkyPoint Albireo     {19.5120f, 27.9597f};
constexpr SkyPoint Sadr        {20.3705f, 40.2567f};
constexpr SkyPoint DeltaCyg    {19.7496f, 45.1308f};
constexpr SkyPoint GienahCyg   {20.7702f, 33.9703f};

// --- Lyra ---
constexpr SkyPoint Vega        {18.6156f, 38.7837f};
constexpr SkyPoint Sheliak     {18.8347f, 33.3627f};
constexpr SkyPoint Sulafat     {18.9824f, 32.6896f};
constexpr SkyPoint ZetaLyr     {18.7461f, 37.6051f};

// --- Aquila ---
constexpr SkyPoint Altair      {19.8464f,  8.8683f};
constexpr SkyPoint Alshain     {19.9219f,  6.4068f};
constexpr SkyPoint Tarazed     {19.7709f, 10.6133f};
constexpr SkyPoint DeltaAql    {19.4249f,  3.1148f};
constexpr SkyPoint ZetaAql     {19.0902f, 13.8637f};
constexpr SkyPoint ThetaAql    {20.1883f, -0.8215f};
constexpr SkyPoint LambdaAql   {19.0656f, -4.8825f};

// --- Scorpius ---
constexpr SkyPoint Antares     {16.4901f, -26.4320f};
constexpr SkyPoint Graffias    {16.0906f, -19.8054f};
constexpr SkyPoint Dschubba    {16.0055f, -22.6217f};
constexpr SkyPoint PiSco       {15.9810f, -26.1141f};
constexpr SkyPoint SigmaSco    {16.3536f, -25.5928f};
constexpr SkyPoint TauSco      {16.5981f, -28.2160f};
constexpr SkyPoint EpsilonSco  {16.8361f, -34.2933f};
constexpr SkyPoint MuSco       {16.8641f, -38.0475f};
constexpr SkyPoint ZetaSco     {16.9109f, -42.3612f};
constexpr SkyPoint EtaSco      {17.2028f, -43.2392f};
constexpr SkyPoint ThetaSco    {17.6222f, -42.9978f};
constexpr SkyPoint IotaSco     {17.7932f, -40.1270f};
constexpr SkyPoint KappaSco    {17.7083f, -39.0299f};
constexpr SkyPoint Shaula      {17.5601f, -37.1038f};
constexpr SkyPoint Lesath      {17.5121f, -37.2958f};

// --- Leo ---
constexpr SkyPoint Regulus     {10.1395f, 11.9672f};
constexpr SkyPoint Denebola    {11.8177f, 14.5720f};
constexpr SkyPoint Algieba     {10.3329f, 19.8415f};
constexpr SkyPoint Zosma       {11.2351f, 20.5237f};
constexpr SkyPoint EpsilonLeo  {9.7645f,  23.7743f};
constexpr SkyPoint ZetaLeo     {10.2785f, 23.4173f};
constexpr SkyPoint EtaLeo      {10.1222f, 16.7627f};
constexpr SkyPoint ThetaLeo    {11.2372f, 15.4296f};
constexpr SkyPoint MuLeo       {9.8797f,  26.0069f};

// --- Taurus ---
constexpr SkyPoint Aldebaran   {4.5987f,  16.5093f};
constexpr SkyPoint Elnath      {5.4382f,  28.6075f};
constexpr SkyPoint GammaTau    {4.3299f,  15.6276f};
constexpr SkyPoint DeltaTau    {4.3819f,  17.5425f};
constexpr SkyPoint EpsilonTau  {4.4776f,  19.1804f};
constexpr SkyPoint ZetaTau     {5.6274f,  21.1425f};
constexpr SkyPoint LambdaTau   {4.0111f,  12.4903f};
constexpr SkyPoint Alcyone     {3.7914f,  24.1051f};

// --- Gemini ---
constexpr SkyPoint Castor      {7.5766f,  31.8883f};
constexpr SkyPoint Pollux      {7.7553f,  28.0262f};
constexpr SkyPoint Alhena      {6.6285f,  16.3993f};
constexpr SkyPoint DeltaGem    {7.3353f,  21.9823f};
constexpr SkyPoint EpsilonGem  {6.7323f,  25.1311f};
constexpr SkyPoint ZetaGem     {7.0685f,  20.5703f};
constexpr SkyPoint EtaGem      {6.2479f,  22.5068f};
constexpr SkyPoint MuGem       {6.3827f,  22.5136f};
constexpr SkyPoint XiGem       {6.7549f,  12.8961f};
constexpr SkyPoint LambdaGem   {7.4287f,  16.5404f};

// --- Canis Major ---
constexpr SkyPoint Sirius      {6.7525f,  -16.7161f};
constexpr SkyPoint Murzim      {6.3783f,  -17.9559f};
constexpr SkyPoint Wezen       {7.1399f,  -26.3932f};
constexpr SkyPoint Adhara      {6.9771f,  -28.9721f};
constexpr SkyPoint ZetaCMa     {6.3400f,  -30.0634f};
constexpr SkyPoint Aludra      {7.4014f,  -29.3031f};
constexpr SkyPoint Omicron2CMa {7.0501f,  -23.8334f};

// --- Bootes ---
constexpr SkyPoint Arcturus    {14.2610f, 19.1824f};
constexpr SkyPoint Nekkar      {15.0322f, 40.3906f};
constexpr SkyPoint Seginus     {14.5340f, 38.3082f};
constexpr SkyPoint DeltaBoo    {15.2582f, 33.3149f};
constexpr SkyPoint Izar        {14.7498f, 27.0742f};
constexpr SkyPoint ZetaBoo     {14.6852f, 13.7283f};
constexpr SkyPoint Muphrid     {13.9114f, 18.3977f};
constexpr SkyPoint RhoBoo      {14.5303f, 30.3714f};

// --- Crux ---
constexpr SkyPoint Acrux       {12.4433f, -63.0991f};
constexpr SkyPoint Mimosa      {12.7953f, -59.6888f};
constexpr SkyPoint Gacrux      {12.5194f, -57.1132f};
constexpr SkyPoint DeltaCru    {12.2525f, -58.7489f};

// --- Pegasus / Andromeda ---
constexpr SkyPoint Markab      {23.0793f, 15.2053f};
constexpr SkyPoint Scheat      {23.0629f, 28.0828f};
constexpr SkyPoint Algenib     {0.2206f,  15.1836f};
constexpr SkyPoint Alpheratz   {0.1398f,  29.0904f};
constexpr SkyPoint Enif        {21.7364f,  9.8750f};
constexpr SkyPoint ZetaPeg     {22.6910f, 10.8313f};
constexpr SkyPoint EtaPeg      {22.7169f, 30.2211f};
constexpr SkyPoint ThetaPeg    {22.1699f,  6.1978f};
constexpr SkyPoint Mirach      {1.1622f,  35.6206f};
constexpr SkyPoint Almach      {2.0650f,  42.3297f};
constexpr SkyPoint DeltaAnd    {0.6553f,  30.8611f};

// --- Perseus ---
constexpr SkyPoint Mirfak      {3.4054f,  49.8612f};
constexpr SkyPoint Algol       {3.1361f,  40.9556f};
constexpr SkyPoint GammaPer    {3.0796f,  53.5065f};
constexpr SkyPoint DeltaPer    {3.7154f,  47.7876f};
constexpr SkyPoint EpsilonPer  {3.9643f,  40.0102f};
constexpr SkyPoint ZetaPer     {3.9022f,  31.8836f};

// --- Auriga ---
constexpr SkyPoint Capella     {5.2782f,  45.9980f};
constexpr SkyPoint Menkalinan  {5.9922f,  44.9474f};
constexpr SkyPoint ThetaAur    {5.9953f,  37.2126f};
constexpr SkyPoint IotaAur     {4.9497f,  33.1661f};
constexpr SkyPoint EpsilonAur  {5.0329f,  43.8233f};

// --- Sagittarius (the Teapot) ---
constexpr SkyPoint Alnasl      {18.0966f, -30.4241f};
constexpr SkyPoint KausMedia   {18.3499f, -29.8281f};
constexpr SkyPoint KausAust    {18.4029f, -34.3846f};
constexpr SkyPoint ZetaSgr     {19.0436f, -29.8803f};
constexpr SkyPoint KausBorealis{18.4661f, -25.4217f};
constexpr SkyPoint Nunki       {18.9211f, -26.2967f};
constexpr SkyPoint TauSgr      {19.1156f, -27.6704f};
constexpr SkyPoint PhiSgr      {18.7460f, -26.9909f};

// --- Virgo ---
constexpr SkyPoint Spica       {13.4199f, -11.1613f};
constexpr SkyPoint Zavijava    {11.8447f,   1.7647f};
constexpr SkyPoint Porrima     {12.6943f,  -1.4494f};
constexpr SkyPoint Auva        {12.9266f,   3.3975f};
constexpr SkyPoint Vindemiatrix{13.0362f,  10.9591f};
constexpr SkyPoint ZetaVir     {13.5786f,  -0.5959f};

// --- Cepheus ---
constexpr SkyPoint Alderamin   {21.3097f, 62.5856f};
constexpr SkyPoint BetaCep     {21.4776f, 70.5607f};
constexpr SkyPoint GammaCep    {23.6558f, 77.6323f};
constexpr SkyPoint ZetaCep     {22.1810f, 58.2012f};
constexpr SkyPoint IotaCep     {22.8281f, 66.2005f};

// --- Draco ---
constexpr SkyPoint Thuban      {14.0731f, 64.3758f};
constexpr SkyPoint Rastaban    {17.5072f, 52.3014f};
constexpr SkyPoint Eltanin     {17.9434f, 51.4889f};
constexpr SkyPoint DeltaDra    {19.2093f, 67.6615f};
constexpr SkyPoint ZetaDra     {17.1465f, 65.7147f};
constexpr SkyPoint EtaDra      {16.3999f, 61.5141f};
constexpr SkyPoint IotaDra     {15.4155f, 58.9661f};
constexpr SkyPoint XiDra       {17.8925f, 56.8726f};

// --- Canis Minor / Aries / Centaurus / Carina etc. ---
constexpr SkyPoint Procyon     {7.6550f,   5.2250f};
constexpr SkyPoint Gomeisa     {7.4527f,   8.2894f};
constexpr SkyPoint Hamal       {2.1195f,  23.4624f};
constexpr SkyPoint Sheratan    {1.9106f,  20.8081f};
constexpr SkyPoint Mesarthim   {1.8846f,  19.2939f};
constexpr SkyPoint RigilKent   {14.6601f, -60.8340f};
constexpr SkyPoint Hadar       {14.0637f, -60.3730f};
constexpr SkyPoint Canopus     {6.3992f,  -52.6957f};

const Star BRIGHT_STARS[] = {
    // Orion
    {Betelgeuse, 0.50f}, {Rigel, 0.13f}, {Bellatrix, 1.64f}, {Mintaka, 2.23f},
    {Alnilam, 1.69f}, {Alnitak, 1.77f}, {Saiph, 2.09f}, {Meissa, 3.39f}, {EtaOri, 3.36f},
    {{4.8307f, 6.9611f}, 3.19f}, {{4.8511f, 5.6050f}, 3.69f}, {{4.9046f, 2.4413f}, 3.72f},

    // Ursa Major
    {Dubhe, 1.79f}, {Merak, 2.37f}, {Phecda, 2.44f}, {Megrez, 3.31f},
    {Alioth, 1.77f}, {Mizar, 2.27f}, {Alkaid, 1.86f},
    {{9.5259f, 63.0619f}, 3.00f}, {{8.5041f, 60.7181f}, 3.36f}, {{9.8489f, 59.0389f}, 3.45f},

    // Ursa Minor
    {Polaris, 1.98f}, {Kochab, 2.08f}, {Pherkad, 3.05f}, {DeltaUMi, 4.36f},
    {EpsilonUMi, 4.23f}, {ZetaUMi, 4.32f}, {EtaUMi, 4.95f},

    // Cassiopeia
    {Schedar, 2.24f}, {Caph, 2.28f}, {GammaCas, 2.47f}, {Ruchbah, 2.68f}, {EpsilonCas, 3.38f},

    // Cygnus
    {Deneb, 1.25f}, {Albireo, 3.08f}, {Sadr, 2.23f}, {DeltaCyg, 2.87f}, {GienahCyg, 2.48f},
    {{21.2149f, 30.2265f}, 3.20f}, {{19.2851f, 53.3685f}, 3.79f},

    // Lyra
    {Vega, 0.03f}, {Sheliak, 3.52f}, {Sulafat, 3.24f}, {ZetaLyr, 4.36f},
    {{18.9128f, 36.8986f}, 4.30f},

    // Aquila
    {Altair, 0.77f}, {Alshain, 3.71f}, {Tarazed, 2.72f}, {DeltaAql, 3.36f},
    {ZetaAql, 2.99f}, {ThetaAql, 3.23f}, {LambdaAql, 3.44f},

    // Scorpius
    {Antares, 1.06f}, {Graffias, 2.62f}, {Dschubba, 2.32f}, {PiSco, 2.89f},
    {SigmaSco, 2.89f}, {TauSco, 2.82f}, {EpsilonSco, 2.29f}, {MuSco, 3.00f},
    {ZetaSco, 3.62f}, {EtaSco, 3.32f}, {ThetaSco, 1.86f}, {IotaSco, 3.03f},
    {KappaSco, 2.41f}, {Shaula, 1.62f}, {Lesath, 2.69f},

    // Leo
    {Regulus, 1.36f}, {Denebola, 2.14f}, {Algieba, 2.08f}, {Zosma, 2.56f},
    {EpsilonLeo, 2.98f}, {ZetaLeo, 3.44f}, {EtaLeo, 3.52f}, {ThetaLeo, 3.33f}, {MuLeo, 3.88f},

    // Taurus
    {Aldebaran, 0.85f}, {Elnath, 1.65f}, {GammaTau, 3.65f}, {DeltaTau, 3.76f},
    {EpsilonTau, 3.53f}, {ZetaTau, 3.00f}, {LambdaTau, 3.47f}, {Alcyone, 2.87f},
    {{4.4785f, 15.8709f}, 3.40f}, {{3.7476f, 24.1134f}, 3.62f},

    // Gemini
    {Castor, 1.58f}, {Pollux, 1.14f}, {Alhena, 1.93f}, {DeltaGem, 3.53f},
    {EpsilonGem, 2.98f}, {ZetaGem, 3.79f}, {EtaGem, 3.28f}, {MuGem, 2.87f},
    {XiGem, 3.36f}, {LambdaGem, 3.58f},

    // Canis Major
    {Sirius, -1.46f}, {Murzim, 1.98f}, {Wezen, 1.83f}, {Adhara, 1.50f},
    {ZetaCMa, 3.02f}, {Aludra, 2.45f}, {Omicron2CMa, 3.02f}, {{7.0637f, -15.6333f}, 4.11f},

    // Bootes
    {Arcturus, -0.05f}, {Nekkar, 3.49f}, {Seginus, 3.03f}, {DeltaBoo, 3.47f},
    {Izar, 2.37f}, {ZetaBoo, 3.78f}, {Muphrid, 2.68f}, {RhoBoo, 3.58f},

    // Crux & southern
    {Acrux, 0.77f}, {Mimosa, 1.25f}, {Gacrux, 1.63f}, {DeltaCru, 2.79f},
    {{12.3565f, -60.4012f}, 3.59f}, {RigilKent, -0.27f}, {Hadar, 0.61f}, {Canopus, -0.72f},
    {{1.6286f, -57.2367f}, 0.46f},   // Achernar
    {{22.9608f, -29.6222f}, 1.16f},  // Fomalhaut

    // Pegasus / Andromeda
    {Markab, 2.48f}, {Scheat, 2.42f}, {Algenib, 2.83f}, {Alpheratz, 2.06f},
    {Enif, 2.39f}, {ZetaPeg, 3.40f}, {EtaPeg, 2.94f}, {ThetaPeg, 3.53f},
    {Mirach, 2.06f}, {Almach, 2.10f}, {DeltaAnd, 3.27f},

    // Perseus
    {Mirfak, 1.79f}, {Algol, 2.12f}, {GammaPer, 2.93f}, {DeltaPer, 3.01f},
    {EpsilonPer, 2.89f}, {ZetaPer, 2.85f}, {{2.8446f, 55.8955f}, 3.76f},

    // Auriga
    {Capella, 0.08f}, {Menkalinan, 1.90f}, {ThetaAur, 2.62f}, {IotaAur, 2.69f},
    {EpsilonAur, 3.03f}, {{5.1082f, 41.2340f}, 3.17f}, {{5.0407f, 41.0757f}, 3.75f},

    // Sagittarius
    {Alnasl, 2.99f}, {KausMedia, 2.70f}, {KausAust, 1.85f}, {ZetaSgr, 2.60f},
    {KausBorealis, 2.81f}, {Nunki, 2.05f}, {TauSgr, 3.32f}, {PhiSgr, 3.17f},
    {{19.3982f, -40.6161f}, 3.97f}, {{19.3778f, -44.4580f}, 3.96f},

    // Virgo
    {Spica, 0.98f}, {Zavijava, 3.61f}, {Porrima, 2.74f}, {Auva, 3.38f},
    {Vindemiatrix, 2.83f}, {ZetaVir, 3.38f},

    // Cepheus
    {Alderamin, 2.45f}, {BetaCep, 3.23f}, {GammaCep, 3.21f}, {ZetaCep, 3.35f},
    {IotaCep, 3.52f}, {{22.4907f, 58.4152f}, 4.07f},

    // Draco
    {Thuban, 3.65f}, {Rastaban, 2.79f}, {Eltanin, 2.23f}, {DeltaDra, 3.07f},
    {ZetaDra, 3.17f}, {EtaDra, 2.73f}, {IotaDra, 3.29f}, {XiDra, 3.75f},

    // Canis Minor, Aries, and other naked-eye anchors
    {Procyon, 0.34f}, {Gomeisa, 2.89f},
    {Hamal, 2.00f}, {Sheratan, 2.64f}, {Mesarthim, 3.86f},
    {{0.7264f, -17.9866f}, 2.04f},   // Deneb Kaitos
    {{3.0380f,   4.0897f}, 2.53f},   // Menkar
    {{2.3224f,  -2.9776f}, 3.04f},   // Mira
    {{22.1077f, -46.9610f}, 1.74f},  // Alnair
    {{20.4265f, -56.7351f}, 1.92f},  // Peacock
    {{17.5822f,  12.5600f}, 2.08f},  // Rasalhague
    {{17.2446f,  14.3903f}, 2.08f},  // Kornephoros region
    {{16.6146f,  21.4896f}, 2.78f},  // Zeta Herculis area
    {{15.5799f,  26.7147f}, 2.23f},  // Alphecca
    {{16.0092f,  -3.6942f}, 2.75f},  // Yed Prior
    {{17.1727f, -15.7250f}, 2.43f},  // Sabik
    {{15.7350f,  26.2957f}, 3.66f},
    {{9.4597f,  -8.6586f},  1.98f},  // Alphard
    {{8.9226f,   5.9455f},  3.11f},  // Acubens region
    {{8.7787f,  28.7603f},  4.66f},  // Praesepe field star
    {{10.7150f, -49.4204f}, 2.21f},  // Aspidiske region
    {{9.2200f, -55.0108f},  1.86f},  // Avior
    {{8.3752f, -59.5095f},  1.75f},  // Miaplacidus
    {{6.3992f, -17.0559f},  3.02f},
    {{4.9535f, -8.7540f},   3.19f},  // Beta Eridani
    {{3.7208f, -9.7633f},   3.52f},
    {{2.9711f, -8.8983f},   3.42f},
    {{1.6996f, -10.3350f},  3.56f},
    {{0.9390f, -8.8235f},   4.44f},
    {{23.6428f, 77.6323f},  3.21f},
    {{21.8971f, 70.5607f},  3.23f},
    {{12.5695f, -23.3967f}, 2.94f},  // Gienah Corvi
    {{12.4979f, -16.5151f}, 2.65f},  // Algorab
    {{12.1685f, -22.6197f}, 2.58f},  // Kraz
    {{12.1402f, -17.5419f}, 4.02f},
    {{13.9256f, -47.2884f}, 2.30f},  // Menkent
    {{11.2351f, -54.4910f}, 2.55f},
    {{10.2851f, -61.3320f}, 2.74f},
};

// Constellation figures as polylines: consecutive points are joined, so one
// array traces a whole asterism in a single stroke.
constexpr SkyPoint FIG_ORION[]       = {Meissa, Betelgeuse, Bellatrix, Meissa};
constexpr SkyPoint FIG_ORION_BELT[]  = {Betelgeuse, Alnitak, Alnilam, Mintaka, Bellatrix};
constexpr SkyPoint FIG_ORION_LEGS[]  = {Saiph, Alnitak};
constexpr SkyPoint FIG_ORION_LEG2[]  = {Rigel, Mintaka};
constexpr SkyPoint FIG_ORION_ETA[]   = {Rigel, EtaOri, Bellatrix};

constexpr SkyPoint FIG_UMA[]         = {Alkaid, Mizar, Alioth, Megrez, Phecda, Merak, Dubhe, Megrez};
constexpr SkyPoint FIG_UMI[]         = {Polaris, DeltaUMi, EpsilonUMi, ZetaUMi, Kochab, Pherkad, EtaUMi, ZetaUMi};
constexpr SkyPoint FIG_CAS[]         = {Caph, Schedar, GammaCas, Ruchbah, EpsilonCas};

constexpr SkyPoint FIG_CYG_SPINE[]   = {Deneb, Sadr, Albireo};
constexpr SkyPoint FIG_CYG_WINGS[]   = {DeltaCyg, Sadr, GienahCyg};

constexpr SkyPoint FIG_LYR[]         = {Vega, ZetaLyr, Sheliak, Sulafat, ZetaLyr};
constexpr SkyPoint FIG_AQL[]         = {ThetaAql, DeltaAql, Altair, Tarazed};
constexpr SkyPoint FIG_AQL2[]        = {Alshain, Altair, ZetaAql, LambdaAql};

constexpr SkyPoint FIG_SCO_HEAD[]    = {Graffias, Dschubba, PiSco};
constexpr SkyPoint FIG_SCO_BODY[]    = {Dschubba, SigmaSco, Antares, TauSco, EpsilonSco,
                                        MuSco, ZetaSco, EtaSco, ThetaSco, IotaSco, KappaSco, Shaula, Lesath};

constexpr SkyPoint FIG_LEO[]         = {Regulus, EtaLeo, Algieba, ZetaLeo, MuLeo, EpsilonLeo};
constexpr SkyPoint FIG_LEO_BODY[]    = {Regulus, ThetaLeo, Denebola, Zosma, Algieba};

constexpr SkyPoint FIG_TAU_HORNS[]   = {Elnath, EpsilonTau, Aldebaran, ZetaTau};
constexpr SkyPoint FIG_TAU_FACE[]    = {EpsilonTau, DeltaTau, GammaTau, LambdaTau};

constexpr SkyPoint FIG_GEM_CASTOR[]  = {Castor, EpsilonGem, MuGem, EtaGem};
constexpr SkyPoint FIG_GEM_POLLUX[]  = {Pollux, DeltaGem, LambdaGem, XiGem};
constexpr SkyPoint FIG_GEM_LINK[]    = {Castor, Pollux};
constexpr SkyPoint FIG_GEM_FOOT[]    = {DeltaGem, ZetaGem, Alhena};

constexpr SkyPoint FIG_CMA[]         = {Murzim, Sirius, Omicron2CMa, Wezen, Aludra};
constexpr SkyPoint FIG_CMA2[]        = {Adhara, Wezen};
constexpr SkyPoint FIG_CMA3[]        = {ZetaCMa, Adhara};

constexpr SkyPoint FIG_BOO[]         = {Arcturus, Izar, Seginus, Nekkar, DeltaBoo, Izar};
constexpr SkyPoint FIG_BOO2[]        = {Arcturus, Muphrid};
constexpr SkyPoint FIG_BOO3[]        = {Arcturus, ZetaBoo};

constexpr SkyPoint FIG_CRUX1[]       = {Acrux, Gacrux};
constexpr SkyPoint FIG_CRUX2[]       = {Mimosa, DeltaCru};

constexpr SkyPoint FIG_PEG[]         = {Markab, Scheat, Alpheratz, Algenib, Markab};
constexpr SkyPoint FIG_PEG2[]        = {Markab, ThetaPeg, Enif};
constexpr SkyPoint FIG_PEG3[]        = {Scheat, EtaPeg};
constexpr SkyPoint FIG_AND[]         = {Alpheratz, DeltaAnd, Mirach, Almach};

constexpr SkyPoint FIG_PER[]         = {Mirfak, GammaPer, DeltaPer, Mirfak, Algol, ZetaPer};
constexpr SkyPoint FIG_PER2[]        = {Mirfak, EpsilonPer};

constexpr SkyPoint FIG_AUR[]         = {Capella, Menkalinan, ThetaAur, Elnath, IotaAur, EpsilonAur, Capella};

constexpr SkyPoint FIG_SGR[]         = {Alnasl, KausMedia, KausAust, ZetaSgr, TauSgr, Nunki,
                                        PhiSgr, ZetaSgr};
constexpr SkyPoint FIG_SGR2[]        = {KausMedia, KausBorealis, PhiSgr};

constexpr SkyPoint FIG_VIR[]         = {Spica, ZetaVir, Porrima, Zavijava};
constexpr SkyPoint FIG_VIR2[]        = {Porrima, Auva, Vindemiatrix};

constexpr SkyPoint FIG_CEP[]         = {Alderamin, BetaCep, GammaCep, IotaCep, ZetaCep, Alderamin};

constexpr SkyPoint FIG_DRA[]         = {Eltanin, Rastaban, XiDra, DeltaDra, ZetaDra, EtaDra,
                                        IotaDra, Thuban};

constexpr SkyPoint FIG_ARI[]         = {Hamal, Sheratan, Mesarthim};
constexpr SkyPoint FIG_CMI[]         = {Procyon, Gomeisa};

const Figure FIGURES[] = {
    {FIG_ORION,      (int) std::size (FIG_ORION)},
    {FIG_ORION_BELT, (int) std::size (FIG_ORION_BELT)},
    {FIG_ORION_LEGS, (int) std::size (FIG_ORION_LEGS)},
    {FIG_ORION_LEG2, (int) std::size (FIG_ORION_LEG2)},
    {FIG_ORION_ETA,  (int) std::size (FIG_ORION_ETA)},
    {FIG_UMA,        (int) std::size (FIG_UMA)},
    {FIG_UMI,        (int) std::size (FIG_UMI)},
    {FIG_CAS,        (int) std::size (FIG_CAS)},
    {FIG_CYG_SPINE,  (int) std::size (FIG_CYG_SPINE)},
    {FIG_CYG_WINGS,  (int) std::size (FIG_CYG_WINGS)},
    {FIG_LYR,        (int) std::size (FIG_LYR)},
    {FIG_AQL,        (int) std::size (FIG_AQL)},
    {FIG_AQL2,       (int) std::size (FIG_AQL2)},
    {FIG_SCO_HEAD,   (int) std::size (FIG_SCO_HEAD)},
    {FIG_SCO_BODY,   (int) std::size (FIG_SCO_BODY)},
    {FIG_LEO,        (int) std::size (FIG_LEO)},
    {FIG_LEO_BODY,   (int) std::size (FIG_LEO_BODY)},
    {FIG_TAU_HORNS,  (int) std::size (FIG_TAU_HORNS)},
    {FIG_TAU_FACE,   (int) std::size (FIG_TAU_FACE)},
    {FIG_GEM_CASTOR, (int) std::size (FIG_GEM_CASTOR)},
    {FIG_GEM_POLLUX, (int) std::size (FIG_GEM_POLLUX)},
    {FIG_GEM_LINK,   (int) std::size (FIG_GEM_LINK)},
    {FIG_GEM_FOOT,   (int) std::size (FIG_GEM_FOOT)},
    {FIG_CMA,        (int) std::size (FIG_CMA)},
    {FIG_CMA2,       (int) std::size (FIG_CMA2)},
    {FIG_CMA3,       (int) std::size (FIG_CMA3)},
    {FIG_BOO,        (int) std::size (FIG_BOO)},
    {FIG_BOO2,       (int) std::size (FIG_BOO2)},
    {FIG_BOO3,       (int) std::size (FIG_BOO3)},
    {FIG_CRUX1,      (int) std::size (FIG_CRUX1)},
    {FIG_CRUX2,      (int) std::size (FIG_CRUX2)},
    {FIG_PEG,        (int) std::size (FIG_PEG)},
    {FIG_PEG2,       (int) std::size (FIG_PEG2)},
    {FIG_PEG3,       (int) std::size (FIG_PEG3)},
    {FIG_AND,        (int) std::size (FIG_AND)},
    {FIG_PER,        (int) std::size (FIG_PER)},
    {FIG_PER2,       (int) std::size (FIG_PER2)},
    {FIG_AUR,        (int) std::size (FIG_AUR)},
    {FIG_SGR,        (int) std::size (FIG_SGR)},
    {FIG_SGR2,       (int) std::size (FIG_SGR2)},
    {FIG_VIR,        (int) std::size (FIG_VIR)},
    {FIG_VIR2,       (int) std::size (FIG_VIR2)},
    {FIG_CEP,        (int) std::size (FIG_CEP)},
    {FIG_DRA,        (int) std::size (FIG_DRA)},
    {FIG_ARI,        (int) std::size (FIG_ARI)},
    {FIG_CMI,        (int) std::size (FIG_CMI)},
};

constexpr SkyPoint GeminiMidpoint {(Castor.ra + Pollux.ra) * 0.5f, (Castor.dec + Pollux.dec) * 0.5f};

// One label per constellation covered by FIGURES above, anchored to a
// bright, recognisable star near the middle of its figure.
const ConstellationLabel CONSTELLATION_LABELS[] = {
    {Betelgeuse,     "Orion"},
    {Megrez,         "Ursa Major"},
    {Polaris,        "Ursa Minor"},
    {GammaCas,       "Cassiopeia"},
    {Sadr,           "Cygnus"},
    {Vega,           "Lyra"},
    {Altair,         "Aquila"},
    {Antares,        "Scorpius"},
    {Regulus,        "Leo"},
    {Aldebaran,      "Taurus"},
    {GeminiMidpoint, "Gemini"},
    {Sirius,         "Canis Major"},
    {Arcturus,       "Bootes"},
    {Acrux,          "Crux"},
    {Scheat,         "Pegasus"},
    {Mirach,         "Andromeda"},
    {Mirfak,         "Perseus"},
    {Capella,        "Auriga"},
    {KausMedia,      "Sagittarius"},
    {Spica,          "Virgo"},
    {Alderamin,      "Cepheus"},
    {Eltanin,        "Draco"},
    {Hamal,          "Aries"},
    {Procyon,        "Canis Minor"},
};

const DSO MESSIER_OBJECTS[] = {
    {{5.5755f,  22.0145f},  8.4f},   // M1  Crab Nebula
    {{13.7033f, 28.3773f},  5.8f},   // M3  globular
    {{16.3933f, -26.5256f}, 5.4f},   // M4  globular
    {{17.6713f, -32.2417f}, 4.2f},   // M6  Butterfly Cluster
    {{17.8971f, -34.7931f}, 3.3f},   // M7  Ptolemy Cluster
    {{18.0606f, -24.3867f}, 6.0f},   // M8  Lagoon Nebula
    {{17.3011f, -26.2678f}, 7.6f},   // M10 globular
    {{18.3111f, -13.7867f}, 6.3f},   // M11 Wild Duck Cluster
    {{16.6949f, 36.4603f},  5.8f},   // M13 Hercules Cluster
    {{18.3406f, -16.1717f}, 6.0f},   // M17 Swan Nebula
    {{18.0333f, -23.0300f}, 6.9f},   // M20 Trifid Nebula
    {{18.6100f, -22.5033f}, 5.1f},   // M22 globular
    {{19.9935f, 22.7211f},  7.4f},   // M27 Dumbbell Nebula
    {{0.7123f,  41.2692f},  3.4f},   // M31 Andromeda Galaxy
    {{0.7115f,  40.8652f},  8.1f},   // M32
    {{1.5641f,  30.6602f},  5.7f},   // M33 Triangulum Galaxy
    {{5.5352f,  34.1367f},  5.1f},   // M36 open cluster
    {{5.4711f,  32.5533f},  6.4f},   // M38 open cluster
    {{5.5881f, -5.3911f},   4.0f},   // M42 Orion Nebula
    {{6.1150f,  24.3367f},  6.1f},   // M35 open cluster
    {{7.6533f, -14.8067f},  5.8f},   // M47 open cluster
    {{12.4994f, 8.0000f},   9.3f},   // M49
    {{13.4979f, 47.1953f},  8.4f},   // M51 Whirlpool Galaxy
    {{18.8933f, 33.0283f},  6.9f},   // M56 globular
    {{18.8932f, 33.0347f},  8.8f},   // M57 Ring Nebula
    {{12.6612f, 11.5486f},  8.8f},   // M60
    {{18.3106f, -32.3483f}, 7.7f},   // M69 globular
    {{20.8969f, -12.5372f}, 8.2f},   // M72 globular
    {{1.7053f,  51.5753f},  10.1f},  // M76 Little Dumbbell
    {{13.6169f, -29.8656f}, 7.5f},   // M83 Southern Pinwheel
    {{12.5138f, 12.3911f},  8.6f},   // M87 Virgo A
    {{12.6668f, -11.6231f}, 8.0f},   // M104 Sombrero Galaxy
    {{0.7302f,  41.6853f},  8.0f},   // M110
};

// Warm/cool tint applied on top of the magnitude brightness, so the field
// isn't a flat grey wash. BGR, because that's what OpenCV wants.
cv::Scalar starColour (float magnitude, int brightness)
{
    const float b = (float) brightness;
    if (magnitude < 0.5f)  return { b, b * 0.97f, b * 0.90f };  // brightest: faint blue-white
    if (magnitude < 2.0f)  return { b * 0.95f, b * 0.96f, b };  // slightly warm
    return { b * 0.92f, b * 0.94f, b * 0.98f };
}

// Draws label text pre-counter-rotated by -rotationDegrees about its own
// centre, so that once the caller (run(), in PluginProcessor.cpp) applies
// its own +rotationDegrees whole-frame rotation on top of this renderer's
// output, the two cancel out and the text ends up upright on screen —
// while its ANCHOR position (not pre-rotated, only its internal glyph
// orientation) still gets carried along by that later whole-frame
// rotation, so the label keeps tracking the constellation it names as the
// view turns, rather than staying pinned to a fixed screen position.
// cv::putText itself has no notion of rotation, hence drawing into a small
// scratch patch and warpAffine-ing that instead.
void putRotatedLabel (cv::Mat& frame, const std::string& text, cv::Point anchorPixel,
                       int fontFace, double fontScale, const cv::Scalar& colour, int thickness,
                       float rotationDegrees)
{
    int baseline = 0;
    cv::Size textSize = cv::getTextSize (text, fontFace, fontScale, thickness, &baseline);
    cv::Point labelCentre (anchorPixel.x + 10 + textSize.width / 2,
                          anchorPixel.y - 10 - textSize.height / 2);

    if (std::abs (rotationDegrees) < 0.05f) {
        cv::putText (frame, text, {labelCentre.x - textSize.width / 2, labelCentre.y + textSize.height / 2},
                    fontFace, fontScale, colour, thickness, cv::LINE_AA);
        return;
    }

    // Square and big enough (the text's own diagonal, plus a margin) that
    // the rotated glyphs can never clip against the patch's own edges.
    int patchSize = (int) std::ceil (std::sqrt ((double) (textSize.width * textSize.width
                                                          + textSize.height * textSize.height))) + 8;
    cv::Mat patch (patchSize, patchSize, frame.type(), cv::Scalar (0, 0, 0));
    cv::putText (patch, text, {patchSize / 2 - textSize.width / 2, patchSize / 2 + textSize.height / 2},
                fontFace, fontScale, colour, thickness, cv::LINE_AA);

    cv::Point2f patchCentre ((float) patchSize / 2.0f, (float) patchSize / 2.0f);
    cv::Mat rot = cv::getRotationMatrix2D (patchCentre, (double) -rotationDegrees, 1.0);
    cv::Mat rotatedPatch;
    cv::warpAffine (patch, rotatedPatch, rot, patch.size(), cv::INTER_LINEAR,
                    cv::BORDER_CONSTANT, cv::Scalar (0, 0, 0));

    // Per-channel max against the existing frame rather than a straight
    // overwrite: the patch's background is pure black, so this leaves every
    // pixel outside the rotated glyphs' own silhouette untouched, with no
    // need for a separate alpha mask.
    int x0 = labelCentre.x - patchSize / 2, y0 = labelCentre.y - patchSize / 2;
    int srcX0 = std::max (0, -x0), srcY0 = std::max (0, -y0);
    int dstX0 = std::max (0, x0),  dstY0 = std::max (0, y0);
    int overlapW = std::min (patchSize - srcX0, frame.cols - dstX0);
    int overlapH = std::min (patchSize - srcY0, frame.rows - dstY0);
    if (overlapW <= 0 || overlapH <= 0) return;

    cv::Mat srcRoi = rotatedPatch (cv::Rect (srcX0, srcY0, overlapW, overlapH));
    cv::Mat dstRoi = frame (cv::Rect (dstX0, dstY0, overlapW, overlapH));
    cv::max (dstRoi, srcRoi, dstRoi);
}

} // namespace

// ---------------------------------------------------------------------------

std::vector<SkyMapRenderer::ConstellationInfo> SkyMapRenderer::listConstellations()
{
    std::vector<ConstellationInfo> result;
    result.reserve (std::size (CONSTELLATION_LABELS));
    for (const auto& label : CONSTELLATION_LABELS)
        result.push_back ({label.name, label.anchor.ra, label.anchor.dec});
    return result;
}

bool SkyMapRenderer::raDecToPixel (float raHours, float decDeg,
                                   float centreRaDeg, float centreDecDeg, float cosCentreDec,
                                   float zoomDegPerPixel, int width, int height,
                                   float& pixelX, float& pixelY)
{
    float raOffset = raHours * 15.0f - centreRaDeg;
    while (raOffset >  180.0f) raOffset -= 360.0f;
    while (raOffset < -180.0f) raOffset += 360.0f;

    // Scaled by THIS point's own cos(dec), not the view centre's
    // (cosCentreDec, still taken by every caller — kept as a parameter so
    // none of them need updating — is unused here now). A single shared
    // cos(centreDec) is a fair local approximation for points close to the
    // centre, but degrades badly for anything far from it in declination:
    // near a pole, cos(dec) changes by an order of magnitude over just a
    // few tens of degrees, so a whole-frame constant scale badly warps the
    // RA spacing of anything that isn't near the view centre's own
    // latitude — most visibly Ursa Minor, which straddles the pole itself
    // (Polaris at dec 89.26°, its bowl down at dec ~72-74°) and so almost
    // never sits entirely at one consistent declination. Each point using
    // its own cos(dec) keeps every point's RA-to-pixel scale correct for
    // its own latitude circle regardless of where the view happens to be
    // centred or zoomed.
    float cosPointDec = std::cos (decDeg * (float) CV_PI / 180.0f);
    pixelX = (float) width  * 0.5f + (raOffset * cosPointDec) / zoomDegPerPixel;
    pixelY = (float) height * 0.5f - (decDeg - centreDecDeg) / zoomDegPerPixel;

    // Generous margin so a star just off-frame still contributes its glow and
    // a figure line with one endpoint outside still gets drawn.
    const float margin = 64.0f;
    return pixelX > -margin && pixelX < (float) width  + margin
        && pixelY > -margin && pixelY < (float) height + margin;
}

int SkyMapRenderer::magnitudeToBrightness (float magnitude)
{
    // -1.5 (Sirius) maps to full white, +6 (naked-eye limit) to a still-
    // clearly-visible glow rather than fading into the background.
    const float normalised = std::clamp ((magnitude + 1.5f) / 7.5f, 0.0f, 1.0f);
    return (int) std::lround (255.0f - 130.0f * normalised);
}

cv::Mat SkyMapRenderer::renderFrame (float raDegrees, float decDegrees, float zoomDegPerPixel,
                                     int width, int height, float rotationDegrees)
{
    // Wrap over the poles rather than clamping there — see panSkyMap() in
    // PluginProcessor.cpp, which is the only normal caller and already
    // stores raDegrees/decDegrees pre-wrapped this same way; matched here
    // too since this is a public entry point other callers could hit
    // directly with an out-of-range value.
    while (decDegrees >  90.0f) { decDegrees =  180.0f - decDegrees; raDegrees += 180.0f; }
    while (decDegrees < -90.0f) { decDegrees = -180.0f - decDegrees; raDegrees += 180.0f; }

    while (raDegrees <    0.0f) raDegrees += 360.0f;
    while (raDegrees >= 360.0f) raDegrees -= 360.0f;

    zoomDegPerPixel = std::max (1.0e-4f, zoomDegPerPixel);

    cv::Mat frame (height, width, CV_8UC3, cv::Scalar (0, 0, 0));

    // Flattening RA by cos(dec) keeps figures from smearing sideways as the
    // view climbs toward a pole. Floored so the scale can't collapse to zero
    // at dec = +/-90.
    const float cosCentreDec = std::max (0.15f, std::cos (decDegrees * (float) CV_PI / 180.0f));

    float px = 0.0f, py = 0.0f, px2 = 0.0f, py2 = 0.0f;

    // --- Equatorial grid ---------------------------------------------------
    // The projection is linear in both axes, so meridians are vertical and
    // parallels horizontal; no need to walk each curve.
    const cv::Scalar gridColour (46, 34, 26);
    for (int decLine = -75; decLine <= 75; decLine += 15) {
        raDecToPixel (raDegrees / 15.0f, (float) decLine, raDegrees, decDegrees, cosCentreDec,
                      zoomDegPerPixel, width, height, px, py);
        if (py >= 0.0f && py < (float) height)
            cv::line (frame, {0, (int) std::lround (py)}, {width, (int) std::lround (py)},
                      gridColour, 1, cv::LINE_AA);
    }
    for (int raLine = 0; raLine < 24; ++raLine) {
        if (raDecToPixel ((float) raLine, decDegrees, raDegrees, decDegrees, cosCentreDec,
                          zoomDegPerPixel, width, height, px, py)
            && px >= 0.0f && px < (float) width)
            cv::line (frame, {(int) std::lround (px), 0}, {(int) std::lround (px), height},
                      gridColour, 1, cv::LINE_AA);
    }

    // --- Constellation figures --------------------------------------------
    const cv::Scalar figureColour (88, 66, 48);
    for (const Figure& fig : FIGURES) {
        for (int i = 0; i + 1 < fig.count; ++i) {
            const bool a = raDecToPixel (fig.pts[i].ra, fig.pts[i].dec, raDegrees, decDegrees,
                                         cosCentreDec, zoomDegPerPixel, width, height, px, py);
            const bool b = raDecToPixel (fig.pts[i + 1].ra, fig.pts[i + 1].dec, raDegrees, decDegrees,
                                         cosCentreDec, zoomDegPerPixel, width, height, px2, py2);
            if (a || b)
                cv::line (frame, {(int) std::lround (px),  (int) std::lround (py)},
                                 {(int) std::lround (px2), (int) std::lround (py2)},
                          figureColour, 1, cv::LINE_AA);
        }
    }

    // --- Deep-sky objects --------------------------------------------------
    for (const DSO& dso : MESSIER_OBJECTS) {
        if (! raDecToPixel (dso.p.ra, dso.p.dec, raDegrees, decDegrees, cosCentreDec,
                            zoomDegPerPixel, width, height, px, py))
            continue;

        const cv::Point centre {(int) std::lround (px), (int) std::lround (py)};
        const int radius = std::clamp ((int) std::lround (9.0f - dso.mag * 0.6f), 3, 9);
        const int minorRadius = std::clamp ((int) std::lround ((float) radius * 0.62f), 2, radius);
        cv::ellipse (frame, centre, {radius, minorRadius},
                     0.0, 0.0, 360.0, cv::Scalar (128, 74, 96), 1, cv::LINE_AA);
    }

    // --- Stars -------------------------------------------------------------
    for (const Star& star : BRIGHT_STARS) {
        if (! raDecToPixel (star.p.ra, star.p.dec, raDegrees, decDegrees, cosCentreDec,
                            zoomDegPerPixel, width, height, px, py))
            continue;

        const cv::Point centre {(int) std::lround (px), (int) std::lround (py)};

        // Rendering-only brightness boost: every star draws as if 0.4
        // magnitudes brighter than cataloged. Feeds both the size and
        // brightness formulas below so the two stay proportional to each
        // other — findBrightestStar/findStarsInStrip (MIDI sampling) still
        // read star.mag directly, so this is purely cosmetic.
        const float renderMag = star.mag - 0.4f;

        const int brightness = magnitudeToBrightness (renderMag);
        const int radius     = std::clamp ((int) std::lround (4.4f - renderMag * 0.55f), 2, 7);

        // Bright stars get a soft halo drawn underneath the core so they read
        // as brighter rather than merely bigger.
        if (renderMag < 3.0f) {
            const double haloScale = 0.4;
            cv::circle (frame, centre, radius + 4,
                        starColour (renderMag, (int) (brightness * haloScale)), -1, cv::LINE_AA);
        }
        cv::circle (frame, centre, radius, starColour (renderMag, brightness), -1, cv::LINE_AA);
    }

    // --- Constellation names ------------------------------------------------
    // Drawn last so the label always sits on top of the grid/figure lines
    // it's naming.
    const cv::Scalar labelColour (150, 118, 92);
    for (const ConstellationLabel& label : CONSTELLATION_LABELS) {
        if (! raDecToPixel (label.anchor.ra, label.anchor.dec, raDegrees, decDegrees, cosCentreDec,
                            zoomDegPerPixel, width, height, px, py))
            continue;
        if (px < 0.0f || px >= (float) width || py < 0.0f || py >= (float) height)
            continue;

        putRotatedLabel (frame, label.name, {(int) std::lround (px), (int) std::lround (py)},
                        cv::FONT_HERSHEY_SIMPLEX, 0.85, labelColour, 2, rotationDegrees);
    }

    return frame;
}

std::pair<float, bool> SkyMapRenderer::findBrightestStar (float raHours, float decDegrees,
                                                          float radiusDegrees) const
{
    const float raDeg = raHours * 15.0f;
    float bestMagnitude = 0.0f;
    bool  found = false;

    for (const Star& star : BRIGHT_STARS) {
        float raOffset = star.p.ra * 15.0f - raDeg;
        while (raOffset >  180.0f) raOffset -= 360.0f;
        while (raOffset < -180.0f) raOffset += 360.0f;

        const float decOffset = star.p.dec - decDegrees;
        const float distance  = std::sqrt (raOffset * raOffset + decOffset * decOffset);

        if (distance <= radiusDegrees && (! found || star.mag < bestMagnitude)) {
            bestMagnitude = star.mag;
            found = true;
        }
    }

    return {bestMagnitude, found};
}

std::vector<std::pair<float, float>> SkyMapRenderer::findStarsInStrip (float raCenterHours,
                                                                      float decCenterDegrees,
                                                                      float raWidthHours,
                                                                      float stripHeightDegrees) const
{
    std::vector<std::pair<float, float>> result;   // {magnitude, dec}
    const float raCentreDeg = raCenterHours * 15.0f;
    const float raHalfWidth = raWidthHours * 15.0f * 0.5f;
    const float decHalf     = stripHeightDegrees * 0.5f;

    for (const Star& star : BRIGHT_STARS) {
        float raOffset = star.p.ra * 15.0f - raCentreDeg;
        while (raOffset >  180.0f) raOffset -= 360.0f;
        while (raOffset < -180.0f) raOffset += 360.0f;

        if (std::abs (raOffset) <= raHalfWidth
            && std::abs (star.p.dec - decCenterDegrees) <= decHalf)
            result.push_back ({star.mag, star.p.dec});
    }

    // North first, so the caller can map declination straight onto pitch.
    std::sort (result.begin(), result.end(),
               [] (const auto& a, const auto& b) { return a.second > b.second; });

    return result;
}
