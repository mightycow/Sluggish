#pragma once


typedef signed __int8 s8;
typedef signed __int16 s16;
typedef signed __int32 s32;
typedef signed __int64 s64;

typedef unsigned __int8 u8;
typedef unsigned __int16 u16;
typedef unsigned __int32 u32;
typedef unsigned __int64 u64;

typedef signed __int64 sptr;
typedef unsigned __int64 uptr;
typedef unsigned int uint;
typedef float f32;
typedef double f64;


template<typename T>
static T Min(T a, T b)
{
	return a < b ? a : b;
}

template<typename T>
static T Min(T a, T b, T c)
{
	return Min(a, Min(b, c));
}

template<typename T>
static T Max(T a, T b)
{
	return a > b ? a : b;
}

template<typename T>
static T Max(T a, T b, T c)
{
	return Max(a, Max(b, c));
}

template<typename T>
static T Clamp(T x, T a, T b)
{
	return Min(Max(x, a), b);
}

struct Buffer
{
	void* buffer;
	uptr length;
};

struct File
{
	File();
	~File();

	bool Open(const char* filePath, const char* mode);
	bool IsValid();
	bool Read(void* data, size_t bytes);
	bool Write(const void* data, size_t bytes);

	void* file;
};

f32 EvaluateQuadraticBezierCurve(f32 y1, f32 y2, f32 y3, f32 t);
bool AllocBuffer(Buffer& buffer, uptr bytes);
bool ReadEntireFile(Buffer& buffer, const char* filePath);
void PrintInfo(const char* format, ...);
void PrintWarning(const char* format, ...);
void PrintError(const char* format, ...);
void FatalError(const char* format, ...);
bool ShouldPrintHelp(int argc, char** argv);
const char* GetExecutableFileName(char* argv0);


/*
Sluggish font file format

SLUGGISH (8 bytes)
# code points (u16)
array of SluggishCodePoint
curves texture width (u16)
curves texture height (u16)
curves texture bytes (u32)
curves texture data (RGBA 32f)
bands texture width (u16)
bands texture height (u16)
bands texture bytes (u32)
bands texture data (RG 16)
*/

#define SLUGGISH_EXTENSION_NAME ".sluggish"
#define SLUGGISH_EXTENSION_LEN 9

#define SLUGGISH_HEADER_DATA "SLUGGISH"
#define SLUGGISH_HEADER_LEN  8

// if you change this, the pixel shader needs to change too
#define TEXTURE_WIDTH  4096
#define TEXTURE_MASK  0xFFF
#define TEXTURE_SHIFT    12

#pragma pack(push, 1)

struct SluggishCodePoint
{
	u32 codePoint;
	u32 width;
	u32 height;
	u32 bandCount;
	u32 bandDimX;
	u32 bandDimY;
	u16 bandsTexCoordX;
	u16 bandsTexCoordY;
};

#pragma pack(pop)

