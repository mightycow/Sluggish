#include "stb_image_write.h"
#include "../shared.hpp"

#include <Windows.h>
#include <math.h>
#include <ctype.h>
#include <vector>


#define BANDS_TAG_1		0xAB
#define BANDS_TAG_2		0xABAB
#define CURVES_TAG_1	0xCD
#define CURVES_TAG_4	0xCDCDCDCD


#pragma pack(push, 1)

struct float2
{
	f32 v[2];
};

struct float4
{
	f32 v[4];
};

struct ushort2
{
	u16 v[2];
};

#pragma pack(pop)


std::vector<SluggishCodePoint> codePoints;
std::vector<ushort2> bandsTexture;
std::vector<float4> curvesTexture;


#if defined(_DEBUG)
static void CheckCurve(ushort2 b, float4 p12, float4 p3)
{
	if(b.v[0] == BANDS_TAG_2 ||
	   b.v[1] == BANDS_TAG_2)
	{
		PrintWarning("Uninitialized band used.\n");
	}
	if(p12.v[0] == CURVES_TAG_4 ||
	   p12.v[1] == CURVES_TAG_4 ||
	   p12.v[2] == CURVES_TAG_4 ||
	   p12.v[3] == CURVES_TAG_4 ||
	   p3.v[0] == CURVES_TAG_4 ||
	   p3.v[1] == CURVES_TAG_4)
	{
		PrintWarning("Uninitialized curve used.\n");
	}
}
#define CHECK_CURVE(b, p12, p3) CheckCurve(b, p12, p3)
#else
#define CHECK_CURVE(b, p12, p3) ((void)0)
#endif


static bool LoadFont(const char* inputPath)
{
	File file;
	if(!file.Open(inputPath, "rb"))
	{
		PrintError("Failed to open font file: %s\n", inputPath);
		return false;
	}

	char header[SLUGGISH_HEADER_LEN + 1];
	file.Read(header, SLUGGISH_HEADER_LEN);
	header[SLUGGISH_HEADER_LEN] = '\0';
	if(strcmp(header, SLUGGISH_HEADER_DATA) != 0)
	{
		PrintError("Invalid header found (%s instead of %s): %s\n", header, SLUGGISH_HEADER_DATA, inputPath);
		return false;
	}

	u16 codePointCount = 0;
	file.Read(&codePointCount, sizeof(codePointCount));
	if(codePointCount == 0)
	{
		PrintError("No code points found: %s\n", inputPath);
		return false;
	}

	codePoints.resize((size_t)codePointCount);
	file.Read(&codePoints[0], codePoints.size() * sizeof(SluggishCodePoint));

	u16 curveTextureWidth;
	u16 curveTextureHeight;
	u32 curveTextureBytes;
	file.Read(&curveTextureWidth, sizeof(curveTextureWidth));
	file.Read(&curveTextureHeight, sizeof(curveTextureHeight));
	file.Read(&curveTextureBytes, sizeof(curveTextureBytes));
	if(curveTextureWidth == 0 || curveTextureHeight == 0 || curveTextureBytes == 0 || curveTextureWidth != TEXTURE_WIDTH)
	{
		PrintError("Invalid curves texture dimensions: %s\n", inputPath);
		return false;
	}

	const size_t curveTexTexels = (size_t)curveTextureWidth * (size_t)curveTextureHeight;
	curvesTexture.resize(curveTexTexels);
	memset(&curvesTexture[0], CURVES_TAG_1, curveTexTexels * sizeof(curvesTexture[0]));
	file.Read(&curvesTexture[0], (size_t)curveTextureBytes);

	u16 bandsTextureWidth;
	u16 bandsTextureHeight;
	u32 bandsTextureBytes;
	file.Read(&bandsTextureWidth, sizeof(bandsTextureWidth));
	file.Read(&bandsTextureHeight, sizeof(bandsTextureHeight));
	file.Read(&bandsTextureBytes, sizeof(bandsTextureBytes));
	if(bandsTextureWidth == 0 || bandsTextureHeight == 0 || bandsTextureBytes == 0 || bandsTextureWidth != TEXTURE_WIDTH)
	{
		PrintError("Invalid bands texture dimensions: %s\n", inputPath);
		return false;
	}

	const size_t bandsTexTexels = (size_t)bandsTextureWidth * (size_t)bandsTextureHeight;
	bandsTexture.resize(bandsTexTexels);
	memset(&bandsTexture[0], BANDS_TAG_1, bandsTexTexels * sizeof(bandsTexture[0]));
	file.Read(&bandsTexture[0], (size_t)bandsTextureBytes);

	return true;
}

static f32 TraceRay(u32 axis0, u32 curveCount, u32 bandOffset, f32 fx0, f32 fy0, f32 pixelsPerEm)
{
	const u32 axis1 = 1 - axis0;
	f32 coverage = 0.0f;

	// run an intersection test against every curve in the selected band
	for(u32 curveIdx = 0; curveIdx < curveCount; ++curveIdx)
	{
		// locate and load the curve data
		const ushort2 curveCoords = bandsTexture[bandOffset + curveIdx];
		const u32 curveX = curveCoords.v[0];
		const u32 curveY = curveCoords.v[1];
		const float4 cp12 = curvesTexture[curveY * TEXTURE_WIDTH + curveX + 0];
		const float4 cp3 = curvesTexture[curveY * TEXTURE_WIDTH + curveX + 1];
		CHECK_CURVE(curveCoords, cp12, cp3);

		// compute the 3 curve points relative to the current pixel (fx0, fy0)
		const float2 p1 = { cp12.v[0] - fx0, cp12.v[1] - fy0 };
		const float2 p2 = { cp12.v[2] - fx0, cp12.v[3] - fy0 };
		const float2 p3 = { cp3.v[0] - fx0, cp3.v[1] - fy0 };
		if(Max(p1.v[axis0], p2.v[axis0], p3.v[axis0]) * pixelsPerEm < -0.5f)
		{
			// the highest coordinate of this curve is lower than this pixel's
			// this means means we have no more curves to intersect with
			// since the curve data is sorted
			break;
		}

		// solve the quadratic equation: a*t*t - 2*b*t + c = 0
		const f32 a = p1.v[axis1] - 2.0f * p2.v[axis1] + p3.v[axis1];
		const f32 b = p1.v[axis1] - p2.v[axis1];
		const f32 c = p1.v[axis1];
		f32 t1, t2;
		if(fabsf(a) < 0.0001f)
		{
			// a is too close to 0, so we solve this linear equation instead: c - 2*b*t = 0
			t1 = t2 = c / (2.0f * b);
		}
		else
		{
			// all is good, we find the 2 roots the usual way
			const f32 rootArg = Max(b*b - a*c, 0.0f);
			const f32 root = sqrtf(rootArg);
			t1 = (b - root) / a;
			t2 = (b + root) / a;
		}

		// generate the curve classification code and update the coverage accordingly
		const uint input = ((p1.v[axis1] > 0.0f) ? 2 : 0) + ((p2.v[axis1] > 0.0f) ? 4 : 0) + ((p3.v[axis1] > 0.0f) ? 8 : 0);
		const uint output = 0x2E74 >> input;
		if((output & 1) != 0)
		{
			const f32 r1 = EvaluateQuadraticBezierCurve(p1.v[axis0], p2.v[axis0], p3.v[axis0], t1);
			coverage += Clamp(0.5f + r1 * pixelsPerEm, 0.0f, 1.0f);
		}
		if((output & 2) != 0)
		{
			const f32 r2 = EvaluateQuadraticBezierCurve(p1.v[axis0], p2.v[axis0], p3.v[axis0], t2);
			coverage -= Clamp(0.5f + r2 * pixelsPerEm, 0.0f, 1.0f);
		}
	}

	return coverage;
}

static bool RenderCodePoint(u32 codePoint, const char* outputPath, u32 w, u32 h, bool preverveAspect)
{
	SluggishCodePoint cp = { 0 };
	bool found = false;
	for(const auto& c : codePoints)
	{
		if(c.codePoint == codePoint)
		{
			found = true;
			cp = c;
			break;
		}
	}

	if(!found)
	{
		PrintError("Failed to find code point U+%04X for file '%s'\n", (unsigned int)codePoint, outputPath);
		return false;
	}

	Buffer image;
	if(!AllocBuffer(image, w * h))
	{
		PrintError("Failed to allocate image buffer for file '%s'\n", outputPath);
		return false;
	}

	LARGE_INTEGER start;
	QueryPerformanceCounter(&start);

	f32 scaleX = (f32)cp.width / (f32)w;
	f32 scaleY = (f32)cp.height / (f32)h;
	if(preverveAspect)
	{
		const f32 s = Max(scaleX, scaleY);
		scaleX = s;
		scaleY = s;
	}

	const f32 offsetX = 0.0f;
	const f32 offsetY = 0.0f;
	const f32 pixelsPerEmX = 1.0f / scaleX;
	const f32 pixelsPerEmY = 1.0f / scaleY;

	u8* imageData = (u8*)image.buffer;
	memset(imageData, 0, (size_t)(w * h));

	const u32 bandCount = cp.bandCount;
	for(u32 y = 0, yi = h - 1; y < h; ++y, --yi)
	{
		// compute this pixel's Y coordinate in em-space
		// compute horizontal band index
		const f32 fy0 = offsetY + (f32)y * scaleY;
		const u32 hBandIdx = (u32)(fy0 / (f32)cp.bandDimY);
		if(hBandIdx >= cp.bandCount)
		{
			// no band contains any curve we could intersect
			continue;
		}

		// locate and load the horizontal band's data
		const ushort2 hBand = bandsTexture[cp.bandsTexCoordY * TEXTURE_WIDTH + cp.bandsTexCoordX + hBandIdx];
		const u32 hBandCurveCount = hBand.v[0];
		const u32 hBandBandOffset = hBand.v[1];

		for(u32 x = 0; x < w; ++x)
		{
			// compute this pixel's X coordinate in em-space
			// compute vertical band index
			const f32 fx0 = offsetX + (f32)x * scaleX;
			const u32 vBandIdx = (u32)(fx0 / (f32)cp.bandDimX);
			if(vBandIdx >= cp.bandCount)
			{
				// no band contains any curve we could intersect
				continue;
			}

			// locate and load the vertical band's data
			const ushort2 vBand = bandsTexture[cp.bandsTexCoordY * TEXTURE_WIDTH + cp.bandsTexCoordX + bandCount + vBandIdx];
			const u32 vBandCurveCount = vBand.v[0];
			const u32 vBandBandOffset = vBand.v[1];

			// trace 2 rays for cheap (but imperfect) AA
			// compute the final coverage
			// write the pixel
			f32 coverageX = TraceRay(0, hBandCurveCount, hBandBandOffset, fx0, fy0, pixelsPerEmX);
			f32 coverageY = TraceRay(1, vBandCurveCount, vBandBandOffset, fx0, fy0, pixelsPerEmY);
			coverageX = Min(fabsf(coverageX), 1.0f);
			coverageY = Min(fabsf(coverageY), 1.0f);
			const f32 coverage = (coverageX + coverageY) * 0.5f;
			imageData[yi*w + x] = (u8)(coverage * 255.0f);
		}
	}

	LARGE_INTEGER end;
	QueryPerformanceCounter(&end);

	if(!stbi_write_tga(outputPath, w, h, 1, imageData))
	{
		PrintError("Failed to write output image file '%s'\n", outputPath);
		return false;
	}

	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	const u64 durationMS = (u64)(((LONGLONG)1000 * (end.QuadPart - start.QuadPart)) / freq.QuadPart);
	printf("Duration: %u ms\n", (unsigned int)durationMS);
	printf("Pixels: %u\n", (unsigned int)(w * h));
	printf("Speed: %.1f ms per megapixel\n", (float)(1000000.0 * ((f64)durationMS / (f64)(w * h))));

	return true;
}

int main(int argc, char** argv)
{
	if(ShouldPrintHelp(argc, argv))
	{
		printf("Renders code points from a Sluggish font file into .tga images.\n");
		printf("\n");
		printf("%s <input%s> [-range=start,end] [-res=width,height] [-stretch]\n", GetExecutableFileName(argv[0]), SLUGGISH_EXTENSION_NAME);
		printf("\n");
		printf("range    The start and end numbers are Unicode code points.\n");
		printf("         e.g. '90' for the letter 'Z'\n");
		printf("         By default, it only renders the letter 'A'.\n");
		printf("res      The width and height in pixels of the output images.\n");
		printf("         By default, the resolution is 1024x1024.\n");
		printf("stretch  Use all the available space to render the glyph.\n");
		printf("         By default, the original aspect ratio is preserved.\n");
		return 1337;
	}

	if(!LoadFont(argv[1]))
	{
		return 1;
	}

	u32 start = 'A';
	u32 end = 'A';
	u32 width = 1024;
	u32 height = 1024;
	bool preserveAspect = true;
	for(int i = 2; i < argc; ++i)
	{
		const char* const arg = argv[i];
		if(strstr(arg, "-range=") == arg)
		{
			u32 s, e;
			if(sscanf(arg, "-range=%u,%u", &s, &e) == 2 && e >= s)
			{
				start = s;
				end = e;
			}
		}
		else if(strstr(arg, "-res=") == arg)
		{
			u32 w, h;
			if(sscanf(arg, "-res=%u,%u", &w, &h) == 2 && w > 16 && h > 16)
			{
				width = w;
				height = h;
			}
		}
		else if(strcmp(arg, "-stretch") == 0)
		{
			preserveAspect = false;
		}
	}

	const char* inputPath = argv[1];
	char outputPathBase[512];
	strcpy(outputPathBase, inputPath);
	const size_t l = strlen(outputPathBase);
	if(l > SLUGGISH_EXTENSION_LEN && strcmp(&inputPath[l - SLUGGISH_EXTENSION_LEN], SLUGGISH_EXTENSION_NAME) == 0)
	{
		outputPathBase[l - SLUGGISH_EXTENSION_LEN] = '\0';
	}

	PrintInfo("Range: U+%04X -> U+%04X\n", start, end);
	PrintInfo("Resolution: %ux%u\n", width, height);

	char fileName[512];
	for(u32 i = start; i <= end; ++i)
	{
		sprintf(fileName, "%s_U+%04X_%ux%u%s.tga", outputPathBase, i, width, height, preserveAspect ? "" : "_stretched");
		RenderCodePoint(i, fileName, width, height, preserveAspect);
	}

	return 0;
}
