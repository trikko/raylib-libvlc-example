// raylib colors are C macros with compound literals: ImportC can't see them, so here they are again.
module colors;

import c : Color;

enum : Color
{
    white     = Color(255, 255, 255, 255),
    black     = Color(0, 0, 0, 255),
    rayWhite  = Color(245, 245, 245, 255),
    lightGray = Color(200, 200, 200, 255),
    darkGray  = Color(80, 80, 80, 255),
    blue      = Color(0, 121, 241, 255),
    darkBlue  = Color(0, 82, 172, 255),
}
