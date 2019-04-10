#include "../shared.hpp"
#define SDL_MAIN_HANDLED
#include "SDL.h"

#include <Windows.h>
#include <stdio.h>
#include <assert.h>
#include <gl/glew.h>
#include <vector>


static const char* const vertexShader = R"alrightythen(
#version 330

layout (location = 0) in vec2 vaPosition;
layout (location = 1) in vec2 vaTexCoords;
layout (location = 2) in vec4 vaScaleBias;
layout (location = 3) in vec4 vaGlyphBandScale;
layout (location = 4) in uvec4 vaBandMaxTexCoords;
out vec2 texCoords;
flat out vec4 glyphBandScale;
flat out uvec4 bandMaxTexCoords;

void main()
{
	gl_Position =  vec4(vaPosition * vaScaleBias.xy + vaScaleBias.zw, 0.0, 1.0);
	texCoords = vaTexCoords;
	glyphBandScale = vaGlyphBandScale;
	bandMaxTexCoords = vaBandMaxTexCoords;
}
)alrightythen";

static const char* const fragmentShader = R"alrightythen(
#version 330

in vec2 texCoords;
flat in vec4 glyphBandScale;
flat in uvec4 bandMaxTexCoords;
out vec4 fragmentColor;

uniform sampler2DRect curvesTex;
uniform usampler2DRect bandsTex;

const float epsilon = 0.0001;

#define glyphScale     glyphBandScale.xy
#define bandScale      glyphBandScale.zw
#define bandMax        bandMaxTexCoords.xy
#define bandsTexCoords bandMaxTexCoords.zw
#define p1x            p12.x
#define p1y            p12.y
#define p2x            p12.z
#define p2y            p12.w
#define p3x            p3.x
#define p3y            p3.y

void main()
{
	float coverageX = 0.0;
	float coverageY = 0.0;

	vec2 pixelsPerEm = vec2(1.0 / fwidth(texCoords.x), 1.0 / fwidth(texCoords.y));

	// compute indices for horizontal and vertical bands
	// x : vertical band index
	// y : horizontal band index
	uvec2 bandIndex = uvec2(clamp(uvec2(texCoords * bandScale), uvec2(0U, 0U), bandMax));

	// get the descriptor of the horizontal band we're in
	// x : curve count
	// y : absolute texel offset into the bands texture
	uint hBandOffset = bandsTexCoords.y * 4096U + bandsTexCoords.x + bandIndex.y;
	uvec2 hBandData = texelFetch(bandsTex, ivec2(hBandOffset & 0xFFFU, hBandOffset >> 12U)).xy;

	// shoot a horizontal ray
	for(uint curve = 0U; curve < hBandData.x; ++curve)
	{
		uint curveOffset = hBandData.y + curve;
		ivec2 curveLoc = ivec2(texelFetch(bandsTex, ivec2(curveOffset & 0xFFFU, curveOffset >> 12U)).xy);

		vec4 p12 = texelFetch(curvesTex, curveLoc) / vec4(glyphScale, glyphScale) - vec4(texCoords, texCoords);
		vec2 p3 = texelFetch(curvesTex, ivec2(curveLoc.x + 1, curveLoc.y)).xy / glyphScale - texCoords;
		if(max(max(p1x, p2x), p3x) * pixelsPerEm.x < -0.5)
		{
			// the right-most curve point is on this fragment's left
			// we can bail because the curves are sorted
			break;
		}

		// generate the classification code
		uint code = (0x2E74U >> (((p1y > 0.0) ? 2U : 0U) + ((p2y > 0.0) ? 4U : 0U) + ((p3y > 0.0) ? 8U : 0U))) & 3U;
		if(code == 0U)
		{
			// we're not intersecting this curve
			continue;
		}

		// we solve the quadratic equation: a*t*t - 2*b*t + c = 0
		float ax = p1x - p2x * 2.0 + p3x;
		float ay = p1y - p2y * 2.0 + p3y;
		float bx = p1x - p2x;
		float by = p1y - p2y;
		float c = p1y;
		float ayr = 1.0 / ay;
		float d = sqrt(max(by * by - ay * c, 0.0));
		float t1 = (by - d) * ayr;
		float t2 = (by + d) * ayr;

		if(abs(ay) < epsilon)
		{
			// a is too close to 0, so we solve this linear equation instead: c - 2*b*t = 0
			t1 = t2 = c / (2.0 * by);
		}

		if((code & 1U) != 0U)
		{
			float x1 = (ax * t1 - bx * 2.0) * t1 + p1x;
			float c = clamp(x1 * pixelsPerEm.x + 0.5, 0.0, 1.0);
			coverageX += c;
		}

		if(code > 1U)
		{
			float x2 = (ax * t2 - bx * 2.0) * t2 + p1x;
			float c = clamp(x2 * pixelsPerEm.x + 0.5, 0.0, 1.0);
			coverageX -= c;
		}
	}

	// get the descriptor of the vertical band we're in
	// x : curve count
	// y : absolute texel offset into the bands texture
	uint vBandOffset = bandsTexCoords.y * 4096U + bandsTexCoords.x + bandMax.y + 1U + bandIndex.x;
	uvec2 vBandData = texelFetch(bandsTex, ivec2(vBandOffset & 0xFFFU, vBandOffset >> 12U)).xy;

	// shoot a vertical ray
	for(uint curve = 0U; curve < vBandData.x; ++curve)
	{
		uint curveOffset = vBandData.y + curve;
		ivec2 curveLoc = ivec2(texelFetch(bandsTex, ivec2(curveOffset & 0xFFFU, curveOffset >> 12U)).xy);

		vec4 p12 = texelFetch(curvesTex, curveLoc) / vec4(glyphScale, glyphScale) - vec4(texCoords, texCoords);
		vec2 p3 = texelFetch(curvesTex, ivec2(curveLoc.x + 1, curveLoc.y)).xy / glyphScale - texCoords;
		if(max(max(p1y, p2y), p3y) * pixelsPerEm.y < -0.5)
		{
			// the highest curve point is below this fragment
			// we can bail because the curves are sorted
			break;
		}

		// generate the classification code
		uint code = (0x2E74U >> (((p1x > 0.0) ? 2U : 0U) + ((p2x > 0.0) ? 4U : 0U) + ((p3x > 0.0) ? 8U : 0U))) & 3U;
		if(code == 0U)
		{
			// we're not intersecting this curve
			continue;
		}

		// we solve the quadratic equation: a*t*t - 2*b*t + c = 0
		float ax = p1x - p2x * 2.0 + p3x;
		float ay = p1y - p2y * 2.0 + p3.y;
		float bx = p1x - p2x;
		float by = p1y - p2y;
		float c = p1x;
		float axr = 1.0 / ax;
		float d = sqrt(max(bx * bx - ax * c, 0.0));
		float t1 = (bx - d) * axr;
		float t2 = (bx + d) * axr;

		if(abs(ax) < epsilon)
		{
			// a is too close to 0, so we solve this linear equation instead: c - 2*b*t = 0
			t1 = t2 = c / (2.0 * bx);
		}

		if((code & 1U) != 0U)
		{
			float y1 = (ay * t1 - by * 2.0) * t1 + p1y;
			float c = clamp(y1 * pixelsPerEm.y + 0.5, 0.0, 1.0);
			coverageY += c;
		}

		if(code > 1U)
		{
			float y2 = (ay * t2 - by * 2.0) * t2 + p1y;
			float c = clamp(y2 * pixelsPerEm.y + 0.5, 0.0, 1.0);
			coverageY -= c;
		}
	}

	coverageX = min(abs(coverageX), 1.0);
	coverageY = min(abs(coverageY), 1.0);
	fragmentColor = vec4(1.0, 1.0, 1.0, (coverageX + coverageY) * 0.5);
}
)alrightythen";


struct System
{
	u64 frameDurationsUS[1 << 12];
	u32 frameCount;
	SDL_Window* window;
	SDL_GLContext glContext;
	int displayWidth;
	int displayHeight;
	bool quit;
};

struct GLSL_Program
{
	GLuint p;  // linked program
	GLuint vs; // vertex shader
	GLuint fs; // fragment shader
};

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

#define MAX_GLYPHS 64

struct OpenGL
{
	// general
	std::vector<SluggishCodePoint> codePoints;
	f32 zoomOffsetX, zoomOffsetY, zoom;
	int cursorX, cursorY;
	bool drawText = true;

	// GL handles
	GLSL_Program program;
	GLuint curvesTex, bandsTex;
	GLuint quadVBO, quadVAO;
	GLuint scaleBiasVBO, glyphBandScaleVBO, bandMaxTexCoordsVBO;

	// data for a single draw call
	f32 scaleAndBias[MAX_GLYPHS][4];
	f32 glyphBandScale[MAX_GLYPHS][4];
	u32 bandMaxTexCoords[MAX_GLYPHS][4];
	u32 glyphCount;
};

static System sys;
static OpenGL gl;
static char g_text[256];


static const char* GL_ErrorString(GLenum error)
{
	switch(error)
	{
		case GL_NO_ERROR:          return "No error";
		case GL_INVALID_ENUM:      return "Invalid enum";
		case GL_INVALID_VALUE:     return "Invalid value";
		case GL_INVALID_OPERATION: return "Invalid operation";
		case GL_STACK_OVERFLOW:    return "Stack overflow";
		case GL_STACK_UNDERFLOW:   return "Stack underflow";
		case GL_OUT_OF_MEMORY:     return "Out of memory";
		default:                   return "Unknown error";
	}
}

static void GL_CheckErrors()
{
	GLenum error = glGetError();
	if(error == GL_NO_ERROR)
	{
		return;
	}

	for(;;)
	{
		PrintError("GL error: %s\n", GL_ErrorString(error));
		error = glGetError();
		if(error == GL_NO_ERROR)
		{
			break;
		}
	}

#if defined(_MSC_VER)
	if(IsDebuggerPresent())
	{
		__debugbreak();
	}
#endif
	FatalError("OpenGL error(s)!");
}

static void Font_Load(const char* inputPath)
{
	File file;
	if(!file.Open(inputPath, "rb"))
	{
		FatalError("Failed to open font file: %s\n", inputPath);
	}

	char header[SLUGGISH_HEADER_LEN + 1];
	file.Read(header, SLUGGISH_HEADER_LEN);
	header[SLUGGISH_HEADER_LEN] = '\0';
	if(strcmp(header, SLUGGISH_HEADER_DATA) != 0)
	{
		FatalError("Invalid header found (%s instead of %s): %s\n", header, SLUGGISH_HEADER_DATA, inputPath);
	}

	u16 codePointCount = 0;
	file.Read(&codePointCount, sizeof(codePointCount));
	if(codePointCount == 0)
	{
		FatalError("No code points found: %s\n", inputPath);
	}

	gl.codePoints.resize((size_t)codePointCount);
	file.Read(&gl.codePoints[0], gl.codePoints.size() * sizeof(SluggishCodePoint));

	u16 curveTextureWidth;
	u16 curveTextureHeight;
	u32 curveTextureBytes;
	file.Read(&curveTextureWidth, sizeof(curveTextureWidth));
	file.Read(&curveTextureHeight, sizeof(curveTextureHeight));
	file.Read(&curveTextureBytes, sizeof(curveTextureBytes));
	if(curveTextureWidth == 0 || curveTextureHeight == 0 || curveTextureBytes == 0 || curveTextureWidth != TEXTURE_WIDTH)
	{
		FatalError("Invalid curves texture dimensions: %s\n", inputPath);
	}

	const size_t curveTexTexels = (size_t)curveTextureWidth * (size_t)curveTextureHeight;
	std::vector<float4> curvesTexture;
	curvesTexture.resize(curveTexTexels);
	memset(&curvesTexture[0], 0xCD, curveTexTexels * sizeof(curvesTexture[0])); // @TODO: fix up constant
	file.Read(&curvesTexture[0], (size_t)curveTextureBytes);

	u16 bandsTextureWidth;
	u16 bandsTextureHeight;
	u32 bandsTextureBytes;
	file.Read(&bandsTextureWidth, sizeof(bandsTextureWidth));
	file.Read(&bandsTextureHeight, sizeof(bandsTextureHeight));
	file.Read(&bandsTextureBytes, sizeof(bandsTextureBytes));
	if(bandsTextureWidth == 0 || bandsTextureHeight == 0 || bandsTextureBytes == 0 || bandsTextureWidth != TEXTURE_WIDTH)
	{
		FatalError("Invalid bands texture dimensions: %s\n", inputPath);
	}

	const size_t bandsTexTexels = (size_t)bandsTextureWidth * (size_t)bandsTextureHeight;
	std::vector<ushort2> bandsTexture;
	bandsTexture.resize(bandsTexTexels);
	memset(&bandsTexture[0], 0xAB, bandsTexTexels * sizeof(bandsTexture[0])); // @TODO: fix up constant
	file.Read(&bandsTexture[0], (size_t)bandsTextureBytes);

	PrintInfo("Creating bands textures...\n");
	glGenTextures(1, &gl.bandsTex);
	glBindTexture(GL_TEXTURE_RECTANGLE, gl.bandsTex);
	glTexImage2D(GL_TEXTURE_RECTANGLE, 0, GL_RG16UI, bandsTextureWidth, bandsTextureHeight, 0, GL_RG_INTEGER, GL_UNSIGNED_SHORT, &bandsTexture[0]);
	glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MIN_FILTER, GL_NEAREST); // required for integer textures
	glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MAG_FILTER, GL_NEAREST); // required for integer textures
	GL_CheckErrors();
	
	PrintInfo("Creating curves textures...\n");
	glGenTextures(1, &gl.curvesTex);
	glBindTexture(GL_TEXTURE_RECTANGLE, gl.curvesTex);
	glTexImage2D(GL_TEXTURE_RECTANGLE, 0, GL_RGBA32F, curveTextureWidth, curveTextureHeight, 0, GL_RGBA, GL_FLOAT, &curvesTexture[0]);
	glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	GL_CheckErrors();

	glBindTexture(GL_TEXTURE_RECTANGLE, 0);
}

static void GL_PrintShaderLog(GLuint shader, GLenum shaderType)
{
	GLint logLength = 0;
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);

	static char log[4096]; // I've seen logs over 3 KB in size.
	glGetShaderInfoLog(shader, sizeof(log), NULL, log);
	PrintError("% shader log: %s\n", shaderType == GL_VERTEX_SHADER ? "Vertex" : "Fragment", log);
}

static void GL_PrintProgramLog(GLuint program)
{
	GLint logLength = 0;
	glGetShaderiv(program, GL_INFO_LOG_LENGTH, &logLength);

	static char log[4096]; // I've seen logs over 3 KB in size.
	glGetProgramInfoLog(program, sizeof(log), NULL, log);
	PrintError("Program log: %s\n", log);
}

static void GL_BindProgram(const GLSL_Program& prog)
{
	assert(prog.p);
	glUseProgram(prog.p);
	GL_CheckErrors();
}

static void GL_UnbindProgram()
{
	glUseProgram(0);
}

static bool GL_CreateShader(GLuint* shaderPtr, GLenum shaderType, const char* shaderSource)
{
	GLuint shader = glCreateShader(shaderType);
	glShaderSource(shader, 1, &shaderSource, NULL);
	glCompileShader(shader);

	GLint result = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &result);
	if(result == GL_TRUE)
	{
		*shaderPtr = shader;
		return true;
	}

	GL_PrintShaderLog(shader, shaderType);
	return false;
}

static bool GL_CreateProgram(GLSL_Program& prog, const char* vs, const char* fs)
{
	if(!GL_CreateShader(&prog.vs, GL_VERTEX_SHADER, vs))
	{
		return false;
	}
		
	if(!GL_CreateShader(&prog.fs, GL_FRAGMENT_SHADER, fs))
	{
		return false;
	}

	prog.p = glCreateProgram();
	glAttachShader(prog.p, prog.vs);
	glAttachShader(prog.p, prog.fs);
	glLinkProgram(prog.p);

	GLint success = GL_FALSE;
	glGetProgramiv(prog.p, GL_LINK_STATUS, &success);
	if(success != GL_TRUE)
	{
		GL_PrintProgramLog(prog.p);
		return false;
	}

	return true;
}

static void GL_RenderAllGlyphs(const GLSL_Program& program)
{
	const u32 glyphCount = gl.glyphCount;
	gl.glyphCount = 0;

	GL_BindProgram(program);

	glBindBuffer(GL_ARRAY_BUFFER, gl.scaleBiasVBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, 16 * glyphCount, gl.scaleAndBias);
	GL_CheckErrors();

	glBindBuffer(GL_ARRAY_BUFFER, gl.glyphBandScaleVBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, 16 * glyphCount, gl.glyphBandScale);
	GL_CheckErrors();

	glBindBuffer(GL_ARRAY_BUFFER, gl.bandMaxTexCoordsVBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, 16 * glyphCount, gl.bandMaxTexCoords);
	GL_CheckErrors();

	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glActiveTexture(GL_TEXTURE0 + 0);
	glEnable(GL_TEXTURE_RECTANGLE);
	glBindTexture(GL_TEXTURE_RECTANGLE, gl.curvesTex);
	glUniform1i(glGetUniformLocation(program.p, "curvesTex"), 0);
	GL_CheckErrors();

	glActiveTexture(GL_TEXTURE0 + 1);
	glEnable(GL_TEXTURE_RECTANGLE);
	glBindTexture(GL_TEXTURE_RECTANGLE, gl.bandsTex);
	glUniform1i(glGetUniformLocation(program.p, "bandsTex"), 1);
	GL_CheckErrors();

	glBindVertexArray(gl.quadVAO);
	GL_CheckErrors();
	glDrawArraysInstanced(GL_QUADS, 0, 4, glyphCount);
	GL_CheckErrors();

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindTexture(GL_TEXTURE_RECTANGLE, 0);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindTexture(GL_TEXTURE_RECTANGLE, 0);

	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindTexture(GL_TEXTURE_RECTANGLE, 0);

	GL_UnbindProgram();
	GL_CheckErrors();
}

static void GL_RenderGlyph(u32 codePoint, f32 x, f32 y, f32 w, f32 h)
{
	SluggishCodePoint cp = { 0 };
	bool found = false;
	for(const auto& c : gl.codePoints)
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
		return;
	}

	const u32 gi = gl.glyphCount;
	const f32 dw = (f32)sys.displayWidth;
	const f32 dh = (f32)sys.displayHeight;
	const f32 sx = w / dw;
	const f32 sy = h / dh;
	gl.scaleAndBias[gi][0] = sx;
	gl.scaleAndBias[gi][1] = sy;
	gl.scaleAndBias[gi][2] = 2.0f * (x / dw) - 1.0f + sx;
	gl.scaleAndBias[gi][3] = 2.0f * (y / dh) - 1.0f + sy;

	gl.glyphBandScale[gi][0] = (f32)cp.width;
	gl.glyphBandScale[gi][1] = (f32)cp.height;
	gl.glyphBandScale[gi][2] = (f32)cp.width / (f32)cp.bandDimX;
	gl.glyphBandScale[gi][3] = (f32)cp.height / (f32)cp.bandDimY;

	gl.bandMaxTexCoords[gi][0] = cp.bandCount - 1;
	gl.bandMaxTexCoords[gi][1] = cp.bandCount - 1;
	gl.bandMaxTexCoords[gi][2] = cp.bandsTexCoordX;
	gl.bandMaxTexCoords[gi][3] = cp.bandsTexCoordY;

	++gl.glyphCount;
	if(gl.glyphCount == MAX_GLYPHS)
	{
		GL_RenderAllGlyphs(gl.program);
	}
}

static void App_Init(const char* fontPath)
{
	glViewport(0, 0, sys.displayWidth, sys.displayHeight);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, sys.displayWidth, 0, sys.displayHeight, 0, 1);

	if(!GL_CreateProgram(gl.program, vertexShader, fragmentShader))
	{
		FatalError("Failed to build shader");
	}

	Font_Load(fontPath);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	const float vertices[] =
	{
		-1.0f, -1.0f, 0.0f, 0.0f,
		-1.0f, 1.0f, 0.0f, 1.0f,
		1.0f, 1.0f, 1.0f, 1.0f,
		1.0f, -1.0f, 1.0f, 0.0f
	};

	glGenVertexArrays(1, &gl.quadVAO);
	glGenBuffers(1, &gl.quadVBO);
	glBindVertexArray(gl.quadVAO);
	glBindBuffer(GL_ARRAY_BUFFER, gl.quadVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
	GL_CheckErrors();
	
	glGenBuffers(1, &gl.scaleBiasVBO);
	glBindBuffer(GL_ARRAY_BUFFER, gl.scaleBiasVBO);
	glBufferData(GL_ARRAY_BUFFER, 16 * MAX_GLYPHS, NULL, GL_DYNAMIC_DRAW);
	GL_CheckErrors();
	
	glGenBuffers(1, &gl.glyphBandScaleVBO);
	glBindBuffer(GL_ARRAY_BUFFER, gl.glyphBandScaleVBO);
	glBufferData(GL_ARRAY_BUFFER, 16 * MAX_GLYPHS, NULL, GL_DYNAMIC_DRAW);
	GL_CheckErrors();
	
	glGenBuffers(1, &gl.bandMaxTexCoordsVBO);
	glBindBuffer(GL_ARRAY_BUFFER, gl.bandMaxTexCoordsVBO);
	glBufferData(GL_ARRAY_BUFFER, 16 * MAX_GLYPHS, NULL, GL_DYNAMIC_DRAW);
	GL_CheckErrors();

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	// float2 positions
	glEnableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, gl.quadVBO);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

	// float2 texture coordinates
	glEnableVertexAttribArray(1);
	glBindBuffer(GL_ARRAY_BUFFER, gl.quadVBO);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
	
	// float4 vertex scale and bias
	glEnableVertexAttribArray(2);
	glBindBuffer(GL_ARRAY_BUFFER, gl.scaleBiasVBO);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 0, (void*)0);
	GL_CheckErrors();
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glVertexAttribDivisor(2, 1);
	GL_CheckErrors();
	
	// float4 glyph scale and bands scale
	glEnableVertexAttribArray(3);
	glBindBuffer(GL_ARRAY_BUFFER, gl.glyphBandScaleVBO);
	glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 0, (void*)0);
	GL_CheckErrors();
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glVertexAttribDivisor(3, 1);
	GL_CheckErrors();
	
	// uint4 band max and tex coords
	glEnableVertexAttribArray(4);
	glBindBuffer(GL_ARRAY_BUFFER, gl.bandMaxTexCoordsVBO);
	glVertexAttribIPointer(4, 4, GL_UNSIGNED_INT, 0, (void*)0);
	GL_CheckErrors();
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glVertexAttribDivisor(4, 1);
	GL_CheckErrors();
	
	gl.zoom = 1.0f;
	gl.zoomOffsetX = 0.0f;
	gl.zoomOffsetY = 0.0f;
}

static void App_Frame()
{
	glClearColor(0.0f, 0.25f, 0.25f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

#if 1
	glColor4f(1.0f, 1.0f, 0.25f, 0.125f);
	{
		glBegin(GL_QUADS);
		glVertex2f(0.0f, 0.0f);
		glVertex2f(0.0f, (GLfloat)sys.displayHeight);
		glVertex2f((GLfloat)sys.displayWidth, (GLfloat)sys.displayHeight);
		glVertex2f((GLfloat)sys.displayWidth, 0.0f);
	}
	glEnd();
#endif

	if(gl.drawText)
	{
		const f32 top = (f32)sys.displayHeight;
		const f32 s = 300.0f;
		const f32 d = 25.0f;
		f32 y = top - s - d;
		f32 x = d;
		GL_RenderGlyph(g_text[0], x, y, s, s); x += d + s;
		GL_RenderGlyph(g_text[1], x, y, s, s); x += d + s;
		GL_RenderGlyph(g_text[2], x, y, s, s); x = d; y -= d + s;
		GL_RenderGlyph(g_text[3], x, y, s, s); x += d + s;
		GL_RenderGlyph(g_text[4], x, y, s, s); x += d + s;
		GL_RenderGlyph(g_text[5], x, y, s, s);
		GL_RenderAllGlyphs(gl.program);
	}

#if 1
	// @TODO: render on screen
	static LARGE_INTEGER lastFrame = { 0 };
	static bool firstFrame = true;
	if(firstFrame)
	{
		firstFrame = false;
		QueryPerformanceCounter(&lastFrame);
		return;
	}
	LARGE_INTEGER now, freq;
	QueryPerformanceCounter(&now);
	QueryPerformanceFrequency(&freq);
	const u64 elapsedUS = (u64)(1000000 * (now.QuadPart - lastFrame.QuadPart) / freq.QuadPart);
	const u32 maxFrameCount = (u32)(sizeof(sys.frameDurationsUS) / sizeof(sys.frameDurationsUS[0]));
	sys.frameDurationsUS[sys.frameCount++] = elapsedUS;
	lastFrame.QuadPart = now.QuadPart;
	if(sys.frameCount == maxFrameCount)
	{
		u64 frameTimeUS = 0;
		for(u32 i = 0; i < sys.frameCount; ++i)
		{
			frameTimeUS += sys.frameDurationsUS[i];
		}
		frameTimeUS /= (u64)sys.frameCount;
		PrintInfo("Frame time: %u us\n", (unsigned int)frameTimeUS);
		sys.frameCount = 0;
	}
#endif
}

static void Sys_KeyDown(const SDL_KeyboardEvent& event)
{
	switch(event.keysym.sym)
	{
		case SDLK_ESCAPE:
			sys.quit = true;
			break;

		case SDLK_SPACE:
			gl.zoom = 1.0f;
			gl.zoomOffsetX = 0.0f;
			gl.zoomOffsetY = 0.0f;
			glViewport(0, 0, sys.displayWidth, sys.displayHeight);
			break;

		case SDLK_d:
		case SDLK_z:
			PrintInfo("Zoom: %g - X: %g - Y: %g\n", gl.zoom, gl.zoomOffsetX, gl.zoomOffsetY);
			break;

		case SDLK_f:
			gl.drawText = !gl.drawText;
			break;

		default:
			break;
	}
}

static void Sys_ClampZoomOffsets()
{
}

static void Sys_Motion(const SDL_MouseMotionEvent& event)
{
	if(event.state & SDL_BUTTON_LMASK)
	{
		gl.zoomOffsetX += event.xrel;
		gl.zoomOffsetY -= event.yrel;
		Sys_ClampZoomOffsets();
		glViewport((GLsizei)gl.zoomOffsetX, (GLsizei)gl.zoomOffsetY, (GLsizei)(gl.zoom * sys.displayWidth), (GLsizei)(gl.zoom * sys.displayHeight));
	}

	gl.cursorX = event.x;
	gl.cursorY = event.y;
}

static void Sys_Wheel(const SDL_MouseWheelEvent& event, int cx, int cy)
{
	if(event.y == 0)
	{
		return;
	}

	cy = sys.displayHeight - cy;
	const f32 zoomStrength = 1.0 + (1.0 / 32.0);
	const f32 x = (f32)cx;
	const f32 y = (f32)cy;
	const f32 curX = (x - gl.zoomOffsetX) / gl.zoom;
	const f32 curY = (y - gl.zoomOffsetY) / gl.zoom;

	if(event.y > 0)
	{
		gl.zoom *= zoomStrength;
	}
	else
	{
		gl.zoom /= zoomStrength;
	}
	gl.zoom = Clamp(gl.zoom, 1.0f / 16.0f, 16.0f);

	const f32 x1 = (curX * gl.zoom) + gl.zoomOffsetX;
	const f32 y1 = (curY * gl.zoom) + gl.zoomOffsetY;
	gl.zoomOffsetX += x - x1;
	gl.zoomOffsetY += y - y1;
	Sys_ClampZoomOffsets();
	glViewport((GLsizei)gl.zoomOffsetX, (GLsizei)gl.zoomOffsetY, (GLsizei)(gl.zoom * sys.displayWidth), (GLsizei)(gl.zoom * sys.displayHeight));
}

static void Sys_HandleEvent(const SDL_Event& event)
{
	switch(event.type)
	{
		case SDL_QUIT:
			sys.quit = true;
			break;

		case SDL_KEYDOWN:
			Sys_KeyDown(event.key);
			break;

		case SDL_MOUSEMOTION:
			Sys_Motion(event.motion);
			break;

		case SDL_MOUSEWHEEL:
			Sys_Wheel(event.wheel, gl.cursorX, gl.cursorY);
			break;

		default:
			break;
	}
}

int main(int argc, char** argv)
{
	if(ShouldPrintHelp(argc, argv))
	{
		printf("Renders up to 6 glyphs of a Sluggish font to a window using OpenGL\n");
		printf("\n");
		printf("%s <input%s> [text]\n", GetExecutableFileName(argv[0]), SLUGGISH_EXTENSION_NAME);
		return 1337;
	}

	strcpy(g_text, "@#?{B~");
	if(argc >= 3 && argv[2][0] != '\0')
	{
		strncpy(g_text, argv[2], sizeof(g_text));
	}

	if(SDL_Init(SDL_INIT_VIDEO) != 0)
	{
		FatalError("SDL_Init failed: %s", SDL_GetError());
	}

	sys.window = SDL_CreateWindow("Sluggish", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1024, 768, SDL_WINDOW_OPENGL);
	if(sys.window == NULL)
	{
		FatalError("SDL_CreateWindow failed: %s", SDL_GetError());
	}
	SDL_GetWindowSize(sys.window, &sys.displayWidth, &sys.displayHeight);

	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
	SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
	sys.glContext = SDL_GL_CreateContext(sys.window);
	if(sys.glContext == NULL)
	{
		FatalError("SDL_GL_CreateContext failed: %s", SDL_GetError());
	}

	const GLenum glewCode = glewInit();
	if(glewCode != GLEW_OK)
	{
		FatalError("glewInit failed: %s", glewGetErrorString(glewCode));
	}

	App_Init(argv[1]);

	while(!sys.quit)
	{
		SDL_Event event;
		while(SDL_PollEvent(&event))
		{
			Sys_HandleEvent(event);
		}

		App_Frame();

		SDL_GL_SwapWindow(sys.window);
	}

	if(sys.window)
	{
		SDL_DestroyWindow(sys.window);
	}

	SDL_Quit();

	return 0;
}
