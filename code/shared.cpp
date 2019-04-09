#include "shared.hpp"

#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Windows.h>


f32 EvaluateQuadraticBezierCurve(f32 y1, f32 y2, f32 y3, f32 t)
{
	// (1-t)*(1-t)*p1 + 2*t*(1-t)*p2 + t*t*p3
	const f32 it = 1.0f - t;

	return it*it*y1 + 2.0f*t*it*y2 + t*t*y3;
}

bool AllocBuffer(Buffer& buffer, uptr bytes)
{
	buffer.buffer = malloc(bytes);
	if(!buffer.buffer)
	{
		return false;
	}

	buffer.length = bytes;
	return true;
}

bool ReadEntireFile(Buffer& buffer, const char* filePath)
{
	FILE* file = fopen(filePath, "rb");
	if(!file)
	{
		return false;
	}

	fseek(file, 0, SEEK_END);
	const uptr bufferLength = (uptr)ftell(file);
	if(!AllocBuffer(buffer, bufferLength + 1))
	{
		fclose(file);
		return false;
	}

	fseek(file, 0, SEEK_SET);
	if(fread(buffer.buffer, (size_t)bufferLength, 1, file) != 1)
	{
		fclose(file);
		return false;
	}

	fclose(file);
	((char*)buffer.buffer)[bufferLength] = '\0';
	return true;
}

void PrintInfo(const char* format, ...)
{
	char msg[1024];

	va_list ap;
	va_start(ap, format);
	vsprintf(msg, format, ap);
	va_end(ap);

	fprintf(stdout, "%s", msg);
}

void PrintWarning(const char* format, ...)
{
	char msg[1024];

	va_list ap;
	va_start(ap, format);
	vsprintf(msg, format, ap);
	va_end(ap);

	fprintf(stdout, "WARNING: %s", msg);
}

void PrintError(const char* format, ...)
{
	char msg[1024];

	va_list ap;
	va_start(ap, format);
	vsprintf(msg, format, ap);
	va_end(ap);

	fprintf(stderr, "ERROR: %s", msg);
}

void FatalError(const char* format, ...)
{
	char msg[1024];

	va_list ap;
	va_start(ap, format);
	vsprintf(msg, format, ap);
	va_end(ap);

	fprintf(stderr, "\nFATAL ERROR: %s\n", msg);

	exit(666);
}

bool ShouldPrintHelp(int argc, char** argv)
{
	if(argc == 1)
	{
		return true;
	}

	return
		strcmp(argv[1], "/?") == 0 ||
		strcmp(argv[1], "/help") == 0 ||
		strcmp(argv[1], "--help") == 0;
}

const char* GetExecutableFileName(char* argv0)
{
	static char fileName[256];

	char* s = argv0 + strlen(argv0);
	char* end = s;
	char* start = s;
	bool endFound = false;
	bool startFound = false;

	while(s >= argv0)
	{
		const char c = *s;
		if(!startFound && !endFound && c == '.')
		{
			end = s;
			endFound = true;
		}
		if(!startFound && (c == '\\' || c == '/'))
		{
			start = s + 1;
			break;
		}

		--s;
	}

	strncpy(fileName, start, (size_t)(end - start));
	fileName[sizeof(fileName) - 1] = '\0';

	return fileName;
}

File::File() : file(NULL)
{
}

File::~File()
{
	if(file != NULL)
	{
		fclose((FILE*)file);
	}
}

bool File::Open(const char* filePath, const char* mode)
{
	file = fopen(filePath, mode);

	return file != NULL;
}

bool File::IsValid()
{
	return file != NULL;
}

bool File::Read(void* data, size_t bytes)
{
	return fread(data, bytes, 1, (FILE*)file) == 1;
}

bool File::Write(const void* data, size_t bytes)
{
	return fwrite(data, bytes, 1, (FILE*)file) == 1;
}