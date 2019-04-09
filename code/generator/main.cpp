#include "stb_truetype.h"
#include "../shared.hpp"

#include <stdio.h>
#include <assert.h>
#include <vector>
#include <algorithm>


struct Curve
{
	f32 x1, y1;
	f32 x2, y2;
	f32 x3, y3;
	u32 texelIndex; // indexes the curves texture
	bool first; // 1st curve of a shape
};

static stbtt_fontinfo g_font;
static std::vector<Curve> g_curves; // this doesn't get written to the file
static std::vector<SluggishCodePoint> g_codePoints;
static std::vector<u16> g_bandsTextureBandOffsets; // GL_RG16 [curve_count band_offset]
static std::vector<u16> g_bandsTextureCurveOffsets; // GL_RG16 [curve_offset curve_offset]
static std::vector<f32> g_curvesTexture; // GL_RGBA32F [x1 y1 x2 y2]
static u32 g_ignoredCodePoints = 0;
static u32 g_bandCount = 16;


static bool ProcessCodePoint(int codePoint)
{
	const int glyphIdx = stbtt_FindGlyphIndex(&g_font, codePoint);

	stbtt_vertex* vertices;
	const int vertexCount = stbtt_GetGlyphShape(&g_font, glyphIdx, &vertices);
	if(vertexCount == 0)
	{
		PrintWarning("U+%04X has no vertices\n", (unsigned int)codePoint);
		++g_ignoredCodePoints;
		return false;
	}

	// we don't support cubic Bézier curves
	for(int v = 0; v < vertexCount; ++v)
	{
		if(vertices[v].type == STBTT_vcubic)
		{
			PrintWarning("U+%04X has bicubic curves\n", (unsigned int)codePoint);
			++g_ignoredCodePoints;
			return false;
		}
	}

	// get the glyph's visible data bounding box
	int igx1, igy1, igx2, igy2;
	stbtt_GetGlyphBox(&g_font, glyphIdx, &igx1, &igy1, &igx2, &igy2);
	const f32 gx1 = (f32)igx1;
	const f32 gy1 = (f32)igy1;

	//
	// build temporary curve list
	//

	Curve curve = { 0 };
	curve.first = false;
	g_curves.clear();
	for(int v = 0; v < vertexCount; ++v)
	{
		const stbtt_vertex& vert = vertices[v];
		if(vert.type == STBTT_vcurve)
		{
			curve.x1 = curve.x3;
			curve.y1 = curve.y3;
			curve.x2 = (f32)vert.cx - gx1;
			curve.y2 = (f32)vert.cy - gy1;
			curve.x3 = (f32)vert.x - gx1;
			curve.y3 = (f32)vert.y - gy1;
			g_curves.push_back(curve);
			curve.first = false;
		}
		else if(vert.type == STBTT_vline)
		{
			curve.x1 = curve.x3;
			curve.y1 = curve.y3;
			curve.x3 = (f32)vert.x - gx1;
			curve.y3 = (f32)vert.y - gy1;
			curve.x2 = floorf((curve.x1 + curve.x3) / 2.0f);
			curve.y2 = floorf((curve.y1 + curve.y3) / 2.0f);
			g_curves.push_back(curve);
			curve.first = false;
		}
		else if(vert.type == STBTT_vmove)
		{
			curve.first = true;
			curve.x3 = (f32)vert.x - gx1;
			curve.y3 = (f32)vert.y - gy1;
		}
	}

	const f32 fbandDelta = 0.0f;
	const u32 bandsTexelIndex = (u32)(g_bandsTextureBandOffsets.size() / 2);

	//
	// fix up curves where the control point is one of the endpoints
	//

	for(auto& c : g_curves)
	{
		if(c.x2 == c.x1 && c.y2 == c.y1 ||
		   c.x2 == c.x3 && c.y2 == c.y3)
		{
			c.x2 = (c.x1 + c.x3) / 2.0f;
			c.y2 = (c.y1 + c.y3) / 2.0f;
		}
	}

	//
	// write curves texture
	//

	for(auto& c : g_curves)
	{
		// make sure we start a curve at a texel's boundary
		if(c.first && g_curvesTexture.size() % 4 != 0)
		{
			const size_t toAdd = 4 - (g_curvesTexture.size() % 4);
			for(size_t i = 0; i < toAdd; ++i)
			{
				g_curvesTexture.push_back(-1.0f);
			}
		}

		// make sure a curve doesn't cross a row boundary
		const bool newRow = (g_curvesTexture.size() / 4) % TEXTURE_WIDTH == TEXTURE_WIDTH - 1;
		if(newRow)
		{
			const size_t toAdd = 8 - (g_curvesTexture.size() % 4);
			for(size_t i = 0; i < toAdd; ++i)
			{
				g_curvesTexture.push_back(-1.0f);
			}
		}

		// [A1 B1] [C1=A2 B2] [C2=A3 B3] ...
		if(c.first || newRow)
		{
			c.texelIndex = (u32)g_curvesTexture.size() / 4;
			assert(g_curvesTexture.size() % 4 == 0);
			g_curvesTexture.push_back(c.x1);
			g_curvesTexture.push_back(c.y1);
		}
		else
		{
			c.texelIndex = (((u32)g_curvesTexture.size() / 2) - 1) / 2;
		}
		
		assert(g_curvesTexture.size() % 2 == 0);
		g_curvesTexture.push_back(c.x2);
		g_curvesTexture.push_back(c.y2);
		g_curvesTexture.push_back(c.x3);
		g_curvesTexture.push_back(c.y3);
	}

	const u32 sizeX = 1 + (u32)(igx2 - igx1);
	const u32 sizeY = 1 + (u32)(igy2 - igy1);
	u32 bandCount = g_bandCount;
	if(sizeX < bandCount || sizeY < bandCount)
	{
		bandCount = Min(sizeX, sizeY) / 2;
	}

	//
	// horizontal bands
	//
	
	const u32 bandDimY = (sizeY + bandCount - 1) / bandCount;
	const f32 fbandDimY = (f32)bandDimY;
	f32 bandMinY = -fbandDelta;
	f32 bandMaxY = fbandDimY + fbandDelta;
	std::stable_sort(std::begin(g_curves), std::end(g_curves), [](const Curve& a, const Curve& b) { return Max(a.x1, a.x2, a.x3) > Max(b.x1, b.x2, b.x3); });
	for(u32 b = 0; b < bandCount; ++b)
	{
		u16 bandTexelOffset = (u16)(g_bandsTextureCurveOffsets.size() / 2); // 2x 16 bits
		u16 curveCount = 0;

		for(const auto& c : g_curves)
		{
			// reject perfectly horizontal curves
			if(c.y1 == c.y2 && c.y2 == c.y3)
			{
				continue;
			}

			// reject curves that don't cross the band
			const f32 curveMinY = Min(c.y1, c.y2, c.y3);
			const f32 curveMaxY = Max(c.y1, c.y2, c.y3);
			if(curveMinY > bandMaxY || curveMaxY < bandMinY)
			{
				continue;
			}

			// push the curve offsets
			const u32 texelIndex = c.texelIndex;
			const u16 curveOffsetX = (u16)(texelIndex % (u32)TEXTURE_WIDTH);
			const u16 curveOffsetY = (u16)(texelIndex / (u32)TEXTURE_WIDTH);
			g_bandsTextureCurveOffsets.push_back(curveOffsetX);
			g_bandsTextureCurveOffsets.push_back(curveOffsetY);

			++curveCount;
		}

		// @TODO: don't push more data if this band is the same as the previous one

		// push the horizontal band
		g_bandsTextureBandOffsets.push_back(curveCount);
		g_bandsTextureBandOffsets.push_back(bandTexelOffset);

		bandMinY += fbandDimY;
		bandMaxY += fbandDimY;

		if(bandTexelOffset >= 0xFFFF ||
		   g_bandsTextureCurveOffsets.size() / 2 >= 0xFFFF)
		{
			FatalError("Too much data generated to be indexed! Try a lower band count.\n");
		}
	}

	//
	// vertical bands
	//
	
	const u32 bandDimX = (sizeX + bandCount - 1) / bandCount;
	const f32 fbandDimX = (f32)bandDimX;
	f32 bandMinX = -fbandDelta;
	f32 bandMaxX = fbandDimX + fbandDelta;
	std::stable_sort(std::begin(g_curves), std::end(g_curves), [](const Curve& a, const Curve& b) { return Max(a.y1, a.y2, a.y3) > Max(b.y1, b.y2, b.y3); });
	for(u32 b = 0; b < bandCount; ++b)
	{
		u16 bandTexelOffset = (u16)(g_bandsTextureCurveOffsets.size() / 2); // 2x 16 bits
		u16 curveCount = 0;

		for(const auto& c : g_curves)
		{
			// reject perfectly vertical curves
			if(c.x1 == c.x2 && c.x2 == c.x3)
			{
				continue;
			}

			// reject curves that don't cross the band
			const f32 curveMinX = Min(c.x1, c.x2, c.x3);
			const f32 curveMaxX = Max(c.x1, c.x2, c.x3);
			if(curveMinX > bandMaxX || curveMaxX < bandMinX)
			{
				continue;
			}

			// push the curve offsets
			const u32 texelIndex = c.texelIndex;
			const u16 curveOffsetX = (u16)(texelIndex % (u32)TEXTURE_WIDTH);
			const u16 curveOffsetY = (u16)(texelIndex / (u32)TEXTURE_WIDTH);
			g_bandsTextureCurveOffsets.push_back(curveOffsetX);
			g_bandsTextureCurveOffsets.push_back(curveOffsetY);

			++curveCount;
		}

		// @TODO: don't push more data if this band is the same as the previous one

		// push the vertical band
		g_bandsTextureBandOffsets.push_back(curveCount);
		g_bandsTextureBandOffsets.push_back(bandTexelOffset);

		bandMinX += fbandDimX;
		bandMaxX += fbandDimX;

		if(bandTexelOffset >= 0xFFFF ||
		   g_bandsTextureCurveOffsets.size() / 2 >= 0xFFFF)
		{
			FatalError("Too much data generated to be indexed! Try a lower band count.\n");
		}
	}

	//
	// push the code point
	//

	SluggishCodePoint cp;
	cp.codePoint = codePoint;
	cp.width = (u32)(igx2 - igx1);
	cp.height = (u32)(igy2 - igy1);
	cp.bandCount = bandCount;
	cp.bandDimX = bandDimX;
	cp.bandDimY = bandDimY;
	cp.bandsTexCoordX = (u16)(bandsTexelIndex % (u32)TEXTURE_WIDTH);
	cp.bandsTexCoordY = (u16)(bandsTexelIndex / (u32)TEXTURE_WIDTH);
	g_codePoints.push_back(cp);

	if(bandsTexelIndex / (u32)TEXTURE_WIDTH >= 0xFFFF)
	{
		FatalError("Too much curve data generated! :-(\n");
	}

	//
	// check the data's validity
	//

	for(const auto& c : g_curves)
	{
		const bool sameRow = c.texelIndex / TEXTURE_WIDTH == (c.texelIndex + 1) / TEXTURE_WIDTH;
		if(!sameRow)
		{
			PrintWarning("U+%04X encoding failed! Texel indices %u and %u are not in the same row\n",
						 (unsigned int)codePoint, (unsigned int)c.texelIndex, (unsigned int)c.texelIndex + 1);
		}
	}

	return true;
}

static bool ProcessFont(const char* inputPath, const char* outputPath)
{
	Buffer fontFile;
	if(!ReadEntireFile(fontFile, inputPath))
	{
		PrintError("Failed to load file into memory: %s\n", inputPath);
		return false;
	}

	if(!stbtt_InitFont(&g_font, (const unsigned char*)fontFile.buffer, 0))
	{
		PrintError("Failed to parse font file: %s\n", inputPath);
		return false;
	}

	File file;
	if(!file.Open(outputPath, "wb"))
	{
		PrintError("Failed to open output file: %s\n", outputPath);
		return false;
	}

	for(int i = 33; i <= 126; ++i)
	{
		ProcessCodePoint(i);
	}

	if(g_codePoints.empty())
	{
		PrintError("No valid code point found: %s\n", inputPath);
		return false;
	}

	// fix up the bands' texel offsets first
	const u32 bandsTexTexels = (u32)(g_bandsTextureBandOffsets.size() + g_bandsTextureCurveOffsets.size()) / 2;
	const u16 bandHeaderTexels = (u16)(g_bandsTextureBandOffsets.size() / 2);
	for(size_t i = 1; i < g_bandsTextureBandOffsets.size(); i += 2)
	{
		g_bandsTextureBandOffsets[i] += bandHeaderTexels;
		if(g_bandsTextureBandOffsets[i] >= bandsTexTexels)
		{
			FatalError("Too much data generated to be indexed! Try a lower band count.\n");
		}
	}

	file.Write(SLUGGISH_HEADER_DATA, SLUGGISH_HEADER_LEN);

	const u16 codePointCount = (u16)g_codePoints.size();
	file.Write(&codePointCount, sizeof(codePointCount));
	file.Write(&g_codePoints[0], g_codePoints.size() * sizeof(SluggishCodePoint));

	const u16 curvesTexWidth = TEXTURE_WIDTH;
	const u32 curvesTexTexels = (u32)g_curvesTexture.size() / 4;
	const u32 curvesTexBytes = (u32)g_curvesTexture.size() * (u32)sizeof(g_curvesTexture[0]);
	const u16 curvesTexHeight = (u16)((curvesTexTexels + curvesTexWidth - 1) / curvesTexWidth);
	file.Write(&curvesTexWidth, sizeof(curvesTexWidth));
	file.Write(&curvesTexHeight, sizeof(curvesTexHeight));
	file.Write(&curvesTexBytes, sizeof(curvesTexBytes));
	file.Write(&g_curvesTexture[0], curvesTexBytes);

	const u16 bandsTexWidth = TEXTURE_WIDTH;
	
	const u32 bandsTexBytes = bandsTexTexels * (u32)sizeof(u16) * 2;
	const u16 bandsTexHeight = (u16)((bandsTexTexels + bandsTexWidth - 1) / bandsTexWidth);
	file.Write(&bandsTexWidth, sizeof(bandsTexWidth));
	file.Write(&bandsTexHeight, sizeof(bandsTexHeight));
	file.Write(&bandsTexBytes, sizeof(bandsTexBytes));
	file.Write(&g_bandsTextureBandOffsets[0], g_bandsTextureBandOffsets.size() * sizeof(u16));
	file.Write(&g_bandsTextureCurveOffsets[0], g_bandsTextureCurveOffsets.size() * sizeof(u16));

	PrintInfo("'%s' -> '%s' DONE\n", inputPath, outputPath);
	PrintInfo("Code points ignored: %u\n", (unsigned int)g_ignoredCodePoints);

	return true;
}

int main(int argc, char** argv)
{
	if(ShouldPrintHelp(argc, argv))
	{
		printf("Reads a TrueType font file and outputs a Sluggish font file.\n");
		printf("The output %s file will be in the same directory as the input.\n", SLUGGISH_EXTENSION_NAME);
		printf("\n");
		printf("%s <input.ttf> [-bands=x,y]\n", GetExecutableFileName(argv[0]));
		printf("\n");
		printf("bands  The maximum number of horizontal and vertical bands that\n");
		printf("       each glyph will be split into.\n");
		printf("       By default, this number is 16. Allowed range: [1,32].\n");
		return 1337;
	}

	for(int i = 2; i < argc; ++i)
	{
		const char* const arg = argv[i];
		if(strstr(arg, "-bands=") == arg)
		{
			int s;
			if(sscanf(arg, "-bands=%d", &s) == 1 && s >= 1 && s <= 32)
			{
				g_bandCount = (u32)s;
			}
		}
	}

	const char* inputPath = argv[1];
	char outputPath[512];
	strcpy(outputPath, inputPath);
	const size_t l = strlen(outputPath);
	if(l > 4 && strcmp(&inputPath[l - 4], ".ttf") == 0)
	{
		outputPath[l - 4] = '\0';
	}
	strcat(outputPath, SLUGGISH_EXTENSION_NAME);

	return ProcessFont(inputPath, outputPath) ? 0 : 1;
}
