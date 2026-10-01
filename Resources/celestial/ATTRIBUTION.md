# Celestial body images

Real photographs, sourced from Wikimedia Commons, cropped to a disc and
resized. Used for the "Occulting Object" overlay in Sequence/Detection mode.

| File | Source image | Credit | License |
|---|---|---|---|
| sun.png | [The Sun in white light.jpg](https://commons.wikimedia.org/wiki/File:The_Sun_in_white_light.jpg) | NASA/SOHO | Public domain |
| moon_phases/moon_phase_1.png (new) | [New Moon.jpg](https://commons.wikimedia.org/wiki/File:New_Moon.jpg) | Wikimedia Commons | Public domain |
| moon_phases/moon_phase_2.png (waxing crescent) | [Waxing crescent moon, October 28, 2025.jpg](https://commons.wikimedia.org/wiki/File:Waxing_crescent_moon,_October_28,_2025._Taken_with_with_a_Nikon_Z6II_camera_and_Sigma_150-600mm_f_5-6.3_DG_OS_HSM_lens.jpg) | Wikimedia Commons | CC BY-SA 4.0 |
| moon_phases/moon_phase_3.png (first quarter) | [Daniel Hershman - march moon (by).jpg](https://commons.wikimedia.org/wiki/File:Daniel_Hershman_-_march_moon_(by).jpg) | Daniel Hershman | CC BY 2.0 |
| moon_phases/moon_phase_4.png (waxing gibbous) | [Lune-Nikon-600-F4 Luc Viatour.jpg](https://commons.wikimedia.org/wiki/File:Lune-Nikon-600-F4_Luc_Viatour.jpg) | Luc Viatour | CC BY-SA 3.0 |
| moon_phases/moon_phase_5.png (full) | [Full moon, March 13, 2025.jpg](https://commons.wikimedia.org/wiki/File:Full_moon,_March_13,_2025._Taken_with_a_Nikon_Z6II_camera_and_Sigma_150-600mm_f_5-6.3_DG_OS_HSM_lens.jpg) | Wikimedia Commons | CC BY-SA 4.0 |
| moon_phases/moon_phase_6.png (waning gibbous) | [2013-01-02_00-00-55-Waning-gibbous-moon.jpg](https://commons.wikimedia.org/wiki/File:2013-01-02_00-00-55-Waning-gibbous-moon.jpg) | Wikimedia Commons | CC BY-SA 3.0 |
| moon_phases/moon_phase_7.png (last quarter) | [Waning gibbous moon near last quarter - 23 Sept. 2016.png](https://commons.wikimedia.org/wiki/File:Waning_gibbous_moon_near_last_quarter_-_23_Sept._2016.png) | Wikimedia Commons | CC BY-SA 4.0 |
| moon_phases/moon_phase_8.png (waning crescent) | [Waning Crescent Moon(7Sep15).jpg](https://commons.wikimedia.org/wiki/File:Waning_Crescent_Moon(7Sep15).jpg) | Wikimedia Commons | CC BY-SA 4.0 |
| mercury.png | [Mercury in true color.jpg](https://commons.wikimedia.org/wiki/File:Mercury_in_true_color.jpg) | NASA/JHU APL/CIW | Public domain |
| venus.png | [Venus from Mariner 10.jpg](https://commons.wikimedia.org/wiki/File:Venus_from_Mariner_10.jpg) | NASA/JPL | Public domain |
| earth.png | [Meteosat-12-fci-march-equinox-2025-noon.jpg](https://commons.wikimedia.org/wiki/File:Meteosat-12-fci-march-equinox-2025-noon.jpg) | EUMETSAT | CC BY-SA 4.0 |
| mars.png | [Mars - August 30 2021 - Flickr - Kevin M. Gill.png](https://commons.wikimedia.org/wiki/File:Mars_-_August_30_2021_-_Flickr_-_Kevin_M._Gill.png) | Kevin M. Gill, from NASA/JPL data | CC BY 2.0 |
| jupiter.png | [Jupiter OPAL 2024.png](https://commons.wikimedia.org/wiki/File:Jupiter_OPAL_2024.png) | NASA/ESA/STScI (Hubble OPAL) | Public domain |
| saturn.png | [Saturn global view from Cassini, rings open Better Colour.png](https://commons.wikimedia.org/wiki/File:Saturn_global_view_from_Cassini,_rings_open_Better_Colour.png) | NASA/JPL-Caltech/SSI | Public domain |
| uranus.png | [Uranus Voyager2 color calibrated.png](https://commons.wikimedia.org/wiki/File:Uranus_Voyager2_color_calibrated.png) | NASA/JPL, Voyager 2 | Public domain |
| neptune.png | [Neptune Voyager2 color calibrated.png](https://commons.wikimedia.org/wiki/File:Neptune_Voyager2_color_calibrated.png) | NASA/JPL, Voyager 2 | Public domain |
| pluto.png | [Pluto in True Color - High-Res.png](https://commons.wikimedia.org/wiki/File:Pluto_in_True_Color_-_High-Res.png) | NASA/JHUAPL/SwRI, New Horizons | Public domain |

Processing applied to every file: centre-cropped to square (Saturn kept
wide, to preserve its rings), resized, and the background keyed to
transparency with a soft-edged alpha ramp — either by brightness (full-disc
photos, which are never near-black anywhere on the visible disc) or, for the
Moon phases, a fixed circular mask (since a crescent's unlit limb is
genuinely dark and must stay part of the disc, not become transparent).
Sun, Venus, Earth, Mars, Saturn, and Uranus additionally have a colour grade
applied (a per-channel multiply) for visual clarity at small sizes — no
other alteration to the source imagery.
