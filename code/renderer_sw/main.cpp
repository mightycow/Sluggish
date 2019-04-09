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

struct float4
{
	f32 x, y, z, w;
};

struct ushort2
{
	u16 x, y;
};

#pragma pack(pop)


std::vector<SluggishCodePoint> codePoints;
std::vector<ushort2> bandsTexture;
std::vector<float4> curvesTexture;


#if defined(_DEBUG)
static void CheckCurve(ushort2 b, float4 p12, float4 p3)
{
	if(b.x == BANDS_TAG_2 ||
	   b.y == BANDS_TAG_2)
	{
		PrintWarning("Uninitialized band used.\n");
	}
	if(p12.x == CURVES_TAG_4 ||
	   p12.y == CURVES_TAG_4 ||
	   p12.z == CURVES_TAG_4 ||
	   p12.w == CURVES_TAG_4 ||
	   p3.x == CURVES_TAG_4 ||
	   p3.y == CURVES_TAG_4)
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
		const f32 fy0 = offsetY + (f32)y * scaleY;
		const u32 hBandIdx = (u32)(fy0 / (f32)cp.bandDimY);
		if(hBandIdx >= cp.bandCount)
		{
			continue;
		}

		const ushort2 hBand = bandsTexture[cp.bandsTexCoordY * TEXTURE_WIDTH + cp.bandsTexCoordX + hBandIdx];
		const u32 hBandCurveCount = hBand.x;
		const u32 hBandBandOffset = hBand.y;

		for(u32 x = 0; x < w; ++x)
		{
			const f32 fx0 = offsetX + (f32)x * scaleX;
			const u32 vBandIdx = (u32)(fx0 / (f32)cp.bandDimX);
			if(vBandIdx >= cp.bandCount)
			{
				continue;
			}

			const ushort2 vBand = bandsTexture[cp.bandsTexCoordY * TEXTURE_WIDTH + cp.bandsTexCoordX + bandCount + vBandIdx];
			const u32 vBandCurveCount = vBand.x;
			const u32 vBandBandOffset = vBand.y;

			// shoot a horizontal ray for each curve
			f32 coverageX = 0.0f;
			for(u32 c = 0; c < hBandCurveCount; ++c)
			{
				const ushort2 hBandCurveCoords = bandsTexture[hBandBandOffset + c];
				const u32 curveX = hBandCurveCoords.x;
				const u32 curveY = hBandCurveCoords.y;
				const float4 p12 = curvesTexture[curveY * TEXTURE_WIDTH + curveX + 0];
				const float4 p3 = curvesTexture[curveY * TEXTURE_WIDTH + curveX + 1];
				CHECK_CURVE(hBandCurveCoords, p12, p3);

				const f32 fx1 = p12.x - fx0;
				const f32 fx2 = p12.z - fx0;
				const f32 fx3 = p3.x - fx0;
				if(Max(fx1, fx2, fx3) * pixelsPerEmX < -0.5f)
				{
					// no more curves to intersect with
					break;
				}
				
				const f32 fy1 = p12.y - fy0;
				const f32 fy2 = p12.w - fy0;
				const f32 fy3 = p3.y - fy0;

				const f32 ay = fy1 - 2.0f * fy2 + fy3;
				const f32 by = fy1 - fy2;
				const f32 cy = fy1;
				f32 t1, t2;
				if(fabsf(ay) < 0.001f)
				{
					t1 = t2 = cy / (2.0f * by);
				}
				else
				{
					const f32 rootArg = Max(by*by - ay*cy, 0.0f);
					const f32 root = sqrtf(rootArg);
					t1 = (by - root) / ay;
					t2 = (by + root) / ay;
				}

				const uint input = ((fy1 > 0.0f) ? 2 : 0) + ((fy2 > 0.0f) ? 4 : 0) + ((fy3 > 0.0f) ? 8 : 0);
				const uint output = 0x2E74 >> input;

				const f32 Cx1 = EvaluateQuadraticBezierCurve(fx1, fx2, fx3, t1);
				const f32 Cx2 = EvaluateQuadraticBezierCurve(fx1, fx2, fx3, t2);

				if((output & 1) != 0)
				{
					coverageX += Clamp(0.5f + Cx1 * pixelsPerEmX, 0.0f, 1.0f);
				}

				if((output & 2) != 0)
				{
					coverageX -= Clamp(0.5f + Cx2 * pixelsPerEmX, 0.0f, 1.0f);
				}
			}

#if 1
			// shoot a vertical ray for each curve
			f32 coverageY = 0.0f;
			for(u32 c = 0; c < vBandCurveCount; ++c)
			{
				const ushort2 vBandCurveCoords = bandsTexture[vBandBandOffset + c];
				const u32 curveX = vBandCurveCoords.x;
				const u32 curveY = vBandCurveCoords.y;
				const float4 p12 = curvesTexture[curveY * TEXTURE_WIDTH + curveX + 0];
				const float4 p3 = curvesTexture[curveY * TEXTURE_WIDTH + curveX + 1];
				CHECK_CURVE(vBandCurveCoords, p12, p3);

				const f32 fy0 = offsetY + (f32)y * scaleY;
				const f32 fy1 = p12.y - fy0;
				const f32 fy2 = p12.w - fy0;
				const f32 fy3 = p3.y - fy0;
				if(Max(fy1, fy2, fy3) * pixelsPerEmY < -0.5f)
				{
					// no more curves to intersect with
					break;
				}

				const f32 fx0 = offsetX + (f32)x * scaleX;
				const f32 fx1 = p12.x - fx0;
				const f32 fx2 = p12.z - fx0;
				const f32 fx3 = p3.x - fx0;

				const f32 ax = fx1 - 2.0f * fx2 + fx3;
				const f32 bx = fx1 - fx2;
				const f32 cx = fx1;
				f32 t1, t2;
				if(fabsf(ax) < 0.001f)
				{
					t1 = t2 = cx / (2.0f * bx);
				}
				else
				{
					const f32 rootArg = Max(bx*bx - ax*cx, 0.0f);
					const f32 root = sqrtf(rootArg);
					t1 = (bx - root) / ax;
					t2 = (bx + root) / ax;
				}

				const uint input = ((fx1 > 0.0f) ? 2 : 0) + ((fx2 > 0.0f) ? 4 : 0) + ((fx3 > 0.0f) ? 8 : 0);
				const uint output = 0x2E74 >> input;

				const f32 Cy1 = EvaluateQuadraticBezierCurve(fy1, fy2, fy3, t1);
				const f32 Cy2 = EvaluateQuadraticBezierCurve(fy1, fy2, fy3, t2);

				if((output & 1) != 0)
				{
					coverageY += Clamp(0.5f + Cy1 * pixelsPerEmY, 0.0f, 1.0f);
				}

				if((output & 2) != 0)
				{
					coverageY -= Clamp(0.5f + Cy2 * pixelsPerEmY, 0.0f, 1.0f);
				}
			}

			coverageX = Min(fabsf(coverageX), 1.0f);
			coverageY = Min(fabsf(coverageY), 1.0f);
			const f32 coverage = (coverageX + coverageY) * 0.5f;
			imageData[yi*w + x] = (u8)(coverage * 255.0f);
#else
			coverageX = Min(fabsf(coverageX), 1.0f);
			imageData[yi*w + x] = (u8)(coverageX * 255.0f);
#endif
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
