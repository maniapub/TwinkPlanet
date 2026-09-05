#pragma once

// RCDATA resource IDs for assets baked directly into the DLL - see Twinkie.rc. Fonts and media
// used to require manually placing files in Documents\TwinkPlanet\, which broke for anyone who
// hadn't done that setup step; embedding them means a fresh install just works.

#define IDR_FONT_CASCADIAMONO       1001
#define IDR_FONT_MANIAICONS         1002
#define IDR_FONT_BRICOLAGEGROTESQUE 1003
#define IDR_FONT_DROIDSANS          1004
#define IDR_FONT_COMICNEUE          1007
#define IDR_MEDIA_TMCOLORS          1005
#define IDR_MEDIA_SHARK             1006
