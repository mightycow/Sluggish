# Presentation

This is a toy/experimental project showing off how to get started rendering glyphs with Eric Lengyel's Slug algorithm on both CPUs and GPUs.

I recommend reading at least the slides linked in the reference section before any of the code.

## Requirements

* Windows x64
* Visual C++ 2013 or later for compiling
* OpenGL 3.2 for the hardware renderer

## Project breakdown

| Sub-project | Purpose |
|:--|:--|
| Font generator | Reads a .ttf TrueType font file and outputs a .sluggish file |
| Software renderer | Reads a .sluggish file and outputs a .tga image per specified code point |
| Hardware renderer | Reads a .sluggish file and renders up to 6 specified glyphs using OpenGL |

| Feature | Support |
|:--|:--|
| Curves texture (FP32) | YES |
| Bands texture (U16) | YES |
| Cutting glyphs into bands (performance) | YES |
| Sorting curves (performance) | YES |
| High-quality implementation of anything | NO |
| High performance | NO |
| 16-bit floating point encoding | NO |
| Data de-duplication and compression | NO |
| Text layouting | NO |
| Colored shapes | NO |
| Adaptive super-sampling | NO |
| Gamma correction | NO |
| Bounding polygons | NO |

The goal was to keep things pretty simple while still not having *awful* performance. For instance, cutting the glyphs into bands has been implemented because it's simple and improves performance *massively*.

## Debugging

By default, the debugger's target executable isn't in the output directory but a directory path specified by the `SLUGGISH_APP_DIR` environment variable.

The .exe and .pdb files are copied over to that directory after every build.

To make this process easy, you can launch Visual Studio through a simple batch script:
```
:: This file would sit next to the Sluggish directory
cd Sluggish\makefiles\vs2013
set SLUGGISH_APP_DIR=C:\important_stuff\sluggish
Sluggish.sln
```

## References

To learn more about how the Slug algorithm works, refer to:  
**GPU-Centered Font Rendering Directly from Glyph Outlines** by Eric Lengyel  
Journal of Computer Graphics Techniques (JCGT), vol. 6, no. 2, 31-47, 2017  
Paper: <http://jcgt.org/published/0006/02/02>  
Slides: <http://terathon.com/i3d2018_lengyel.pdf>

# Licenses

| Project/File | License(s) | License File(s) |
|:--|:--|:--|
| This project | Unlicense (public domain) | UNLICENSE.MD |
| SDL 2 | zlib | libs/SDL2/COPYING.txt |
| GLEW | Modified BSD License<br>Mesa 3-D License (MIT)<br>Khronos License (MIT) | libs/GLEW/LICENSE.txt |
| premake 5 | premake 5 | makefiles/PREMAKE_LICENSE.txt |
| stb_truetype.h | Pick-a-license:<br>Unlicense (public domain)<br>MIT | code/generator/stb_truetype.h |
| stb_image_write.h | Pick-a-license:<br>Unlicense (public domain)<br>MIT | code/renderer_sw/stb_image_write.h |

# Patent

While there is no patent on the Slug algorithm as of the time of writing, Eric Lengyel has made an application for one and the process is still on-going...
