
//{{BLOCK(logo)

//======================================================================
//
//	logo, 256x192@8, 
//	+ palette 256 entries, not compressed
//	+ 78 tiles (t|f reduced) not compressed
//	+ regular map (flat), not compressed, 32x24 
//	Total size: 512 + 4992 + 1536 = 7040
//
//	Time-stamp: 2025-06-18, 21:52:47
//	Exported by Cearn's GBA Image Transmogrifier, v0.9.2
//	( http://www.coranac.com/projects/#grit )
//
//======================================================================

#ifndef GRIT_LOGO_H
#define GRIT_LOGO_H

#define logoTilesLen 4992
extern const unsigned short logoTiles[2496];

#define logoMapLen 1536
extern const unsigned short logoMap[768];

#define logoPalLen 512
extern const unsigned short logoPal[256];

#endif // GRIT_LOGO_H

//}}BLOCK(logo)
