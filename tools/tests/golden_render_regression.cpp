// Exact-pixel regression for the production CRenderThread renderer.
#include "../../src/stdafx.h"
#include "../../src/DjVuSource.h"
#include "../../src/RenderThread.h"
#include "../../src/Drawing.h"

#include <bcrypt.h>
#include <fstream>
#include <iterator>
#include <stdio.h>

namespace
{
	class RegressionApplication : public IApplication
	{
	public:
		virtual bool LoadDocSettings(const CString&, DocSettings*) { return false; }
		virtual bool GetCropPages() { return false; }
		virtual DictionaryInfo* GetDictionaryInfo(const CString&, bool) { return NULL; }
		virtual void ReportFatalError() { }
	};

	struct GoldenCase
	{
		string id, fixture, fixtureSha256, displaySettings, hash;
		int page, rotate, displayMode, width, height;
		CSize size;
		bool thumbnail;
	};

	bool Fail(const char* text)
	{
		fprintf(stderr, "golden render regression failed: %s\n", text);
		return false;
	}

	bool ReadFile(const char* path, string& text)
	{
		ifstream input(path, ios::in | ios::binary);
		if (!input) return false;
		text.assign(istreambuf_iterator<char>(input), istreambuf_iterator<char>());
		return true;
	}

	bool ReadString(const string& object, const char* key, string& value)
	{
		const string marker = string("\"") + key + "\"";
		size_t pos = object.find(marker);
		if (pos == string::npos || (pos = object.find(':', pos + marker.length())) == string::npos)
			return false;
		pos = object.find('"', pos + 1);
		if (pos == string::npos) return false;
		size_t end = object.find('"', pos + 1);
		if (end == string::npos) return false;
		value.assign(object, pos + 1, end - pos - 1);
		return true;
	}

	bool ReadInt(const string& object, const char* key, int& value)
	{
		const string marker = string("\"") + key + "\"";
		size_t pos = object.find(marker);
		if (pos == string::npos || (pos = object.find(':', pos + marker.length())) == string::npos)
			return false;
		value = atoi(object.c_str() + pos + 1);
		return true;
	}

	bool ReadBool(const string& object, const char* key, bool& value)
	{
		const string marker = string("\"") + key + "\"";
		size_t pos = object.find(marker);
		if (pos == string::npos || (pos = object.find(':', pos + marker.length())) == string::npos)
			return false;
		pos = object.find_first_not_of(" \t\r\n", pos + 1);
		if (pos == string::npos) return false;
		if (object.compare(pos, 4, "true") == 0) { value = true; return true; }
		if (object.compare(pos, 5, "false") == 0) { value = false; return true; }
		return false;
	}

	int DisplayModeFromName(const string& name)
	{
		if (name == "Color") return CDjVuView::Color;
		if (name == "BlackAndWhite") return CDjVuView::BlackAndWhite;
		if (name == "Background") return CDjVuView::Background;
		if (name == "Foreground") return CDjVuView::Foreground;
		return -1;
	}

	const char* DisplayModeName(int mode)
	{
		if (mode == CDjVuView::Color) return "Color";
		if (mode == CDjVuView::BlackAndWhite) return "BlackAndWhite";
		if (mode == CDjVuView::Background) return "Background";
		if (mode == CDjVuView::Foreground) return "Foreground";
		return "Unknown";
	}

	bool LoadBaseline(const char* path, vector<GoldenCase>& cases)
	{
		string text;
		if (!ReadFile(path, text)) return Fail("could not read baseline");
		if (text.find("\"baseline_commit\": \"b8f9faa\"") == string::npos)
			return Fail("baseline is not anchored to b8f9faa");

		size_t pos = 0;
		while ((pos = text.find("\"id\"", pos)) != string::npos)
		{
			size_t begin = text.rfind('{', pos);
			size_t end = text.find('}', pos);
			if (begin == string::npos || end == string::npos) return Fail("malformed baseline case");
			string object = text.substr(begin, end - begin + 1);
			GoldenCase item;
			string mode;
			int sizeWidth = 0, sizeHeight = 0;
			if (!ReadString(object, "id", item.id) || !ReadString(object, "fixture", item.fixture) ||
				!ReadString(object, "fixtureSha256", item.fixtureSha256) ||
				!ReadInt(object, "page", item.page) || !ReadInt(object, "sizeWidth", sizeWidth) ||
				!ReadInt(object, "sizeHeight", sizeHeight) || !ReadInt(object, "rotation", item.rotate) ||
				!ReadString(object, "displayMode", mode) || !ReadBool(object, "thumbnail", item.thumbnail) ||
				!ReadString(object, "displaySettings", item.displaySettings) || !ReadInt(object, "width", item.width) ||
				!ReadInt(object, "height", item.height) || !ReadString(object, "sha256", item.hash))
				return Fail("baseline case is missing a required field");
			item.size = CSize(sizeWidth, sizeHeight);
			item.displayMode = DisplayModeFromName(mode);
			if (item.displayMode < 0 || item.page < 0 || item.size.cx <= 0 || item.size.cy <= 0 ||
				item.fixtureSha256.length() != 64 || item.displaySettings != "default")
				return Fail("baseline case has unsupported render parameters");
			cases.push_back(item);
			pos = end + 1;
		}
		return !cases.empty() || Fail("baseline has no cases");
	}

	bool CanonicalRgb24(CDIB* bitmap, vector<BYTE>& pixels)
	{
		if (bitmap == NULL || !bitmap->IsValid())
			return false;
		const int width = bitmap->GetWidth();
		const int height = bitmap->GetHeight();
		if (width <= 0 || height <= 0) return false;
		const int bitsPerPixel = bitmap->GetBitsPerPixel();
		if (bitsPerPixel != 1 && bitsPerPixel != 4 && bitsPerPixel != 8 && bitsPerPixel != 24 && bitsPerPixel != 32)
			return false;
		const size_t rowBytes = static_cast<size_t>(width) * 3;
		const size_t sourceRowBytes = (static_cast<size_t>(width) * bitsPerPixel + 7) / 8;
		const size_t stride = (sourceRowBytes + 3) & ~static_cast<size_t>(3);
		pixels.resize(rowBytes * height);
		const bool bottomUp = bitmap->GetBitmapInfo()->bmiHeader.biHeight > 0;
		const RGBQUAD* palette = bitmap->GetPalette();
		const int paletteSize = bitmap->GetColorCount();
		for (int y = 0; y < height; ++y)
		{
			const BYTE* source = bitmap->GetBits() + (bottomUp ? height - 1 - y : y) * stride;
			BYTE* target = &pixels[static_cast<size_t>(y) * rowBytes];
			for (int x = 0; x < width; ++x)
			{
				if (bitsPerPixel == 24 || bitsPerPixel == 32)
				{
					const BYTE* pixel = source + x * (bitsPerPixel / 8);
					target[x*3] = pixel[2];
					target[x*3 + 1] = pixel[1];
					target[x*3 + 2] = pixel[0];
				}
				else
				{
					int color = bitsPerPixel == 8 ? source[x] :
						(bitsPerPixel == 4 ? (source[x/2] >> (x % 2 == 0 ? 4 : 0)) & 15 :
						(source[x/8] >> (7 - x % 8)) & 1);
					if (color >= paletteSize) return false;
					target[x*3] = palette[color].rgbRed;
					target[x*3 + 1] = palette[color].rgbGreen;
					target[x*3 + 2] = palette[color].rgbBlue;
				}
			}
		}
		return true;
	}

	bool Sha256(const vector<BYTE>& bytes, string& hash)
	{
		BCRYPT_ALG_HANDLE algorithm = NULL;
		BCRYPT_HASH_HANDLE context = NULL;
		DWORD objectLength = 0, hashLength = 0, resultLength = 0;
		vector<BYTE> object, digest;
		bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0 &&
			BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &resultLength, 0) == 0 &&
			BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &resultLength, 0) == 0;
		if (ok)
		{
			object.resize(objectLength);
			digest.resize(hashLength);
			ok = BCryptCreateHash(algorithm, &context, &object[0], objectLength, NULL, 0, 0) == 0 &&
				BCryptHashData(context, const_cast<PUCHAR>(&bytes[0]), static_cast<ULONG>(bytes.size()), 0) == 0 &&
				BCryptFinishHash(context, &digest[0], hashLength, 0) == 0;
		}
		if (context != NULL) BCryptDestroyHash(context);
		if (algorithm != NULL) BCryptCloseAlgorithmProvider(algorithm, 0);
		if (!ok) return false;
		static const char hex[] = "0123456789abcdef";
		hash.clear();
		for (size_t i = 0; i < digest.size(); ++i)
		{
			hash += hex[digest[i] >> 4];
			hash += hex[digest[i] & 15];
		}
		return true;
	}

	bool WriteBaseline(const char* path, const vector<GoldenCase>& cases)
	{
		ofstream output(path, ios::out | ios::binary | ios::trunc);
		if (!output) return false;
		output << "{\n  \"schema_version\": 1,\n  \"baseline_commit\": \"b8f9faa\",\n";
		output << "  \"canonicalization\": \"RGB24 top-to-bottom, no stride padding, SHA-256\",\n  \"cases\": [\n";
		for (size_t i = 0; i < cases.size(); ++i)
		{
			const GoldenCase& item = cases[i];
			output << "    {\"id\": \"" << item.id << "\", \"fixture\": \"" << item.fixture
				<< "\", \"fixtureSha256\": \"" << item.fixtureSha256 << "\", \"page\": " << item.page
				<< ", \"size\": \"" << item.size.cx << "x" << item.size.cy << "\", \"sizeWidth\": " << item.size.cx
				<< ", \"sizeHeight\": " << item.size.cy << ", \"rotation\": " << item.rotate
				<< ", \"displayMode\": \"" << DisplayModeName(item.displayMode) << "\", \"thumbnail\": "
				<< (item.thumbnail ? "true" : "false") << ", \"displaySettings\": \"" << item.displaySettings
				<< "\", \"width\": " << item.width << ", \"height\": " << item.height
				<< ", \"sha256\": \"" << item.hash << "\"}" << (i + 1 == cases.size() ? "\n" : ",\n");
		}
		output << "  ]\n}\n";
		return !!output;
	}

	void SaveActual(const GoldenCase& item, CDIB* bitmap)
	{
		::CreateDirectory(_T("tools\\tests\\artifacts"), NULL);
		::CreateDirectory(_T("tools\\tests\\artifacts\\golden-render"), NULL);
		CString path;
		path.Format(_T("tools\\tests\\artifacts\\golden-render\\%S-actual.bmp"), item.id.c_str());
		bitmap->Save(path, CDIB::FormatBMP);
	}
}

int _tmain(int argc, TCHAR** argv)
{
	if (!AfxWinInit(::GetModuleHandle(NULL), NULL, ::GetCommandLine(), 0) || (argc != 3 && argc != 4))
		return 2;
	const bool update = argc == 4 && _tcscmp(argv[3], _T("--update-baseline")) == 0;
	if (argc == 4 && !update) return 2;

	CStringA baselinePath(argv[1]);
	CStringA corpusRoot(argv[2]);
	vector<GoldenCase> cases;
	if (!LoadBaseline(baselinePath, cases)) return 1;

	RegressionApplication application;
	DjVuSource::SetApplication(&application);
	bool passed = true;
	for (size_t index = 0; index < cases.size(); ++index)
	{
		GoldenCase& item = cases[index];
		CString fixturePath;
		fixturePath.Format(_T("%S\\%S"), static_cast<LPCSTR>(corpusRoot), item.fixture.c_str());
		CStringA fixturePathA(fixturePath);
		string fixtureBytes, fixtureHash;
		vector<BYTE> fixtureData;
		if (!ReadFile(fixturePathA, fixtureBytes))
		{
			fprintf(stderr, "golden render regression failed: %s: fixture is unavailable\n", item.id.c_str());
			passed = false;
			continue;
		}
		fixtureData.assign(fixtureBytes.begin(), fixtureBytes.end());
		if (!Sha256(fixtureData, fixtureHash) || fixtureHash != item.fixtureSha256)
		{
			fprintf(stderr, "golden render regression failed: %s: fixture SHA-256 mismatch expected=%s actual=%s\n",
				item.id.c_str(), item.fixtureSha256.c_str(), fixtureHash.c_str());
			passed = false;
			continue;
		}
		DjVuSource* source = DjVuSource::FromFile(fixturePath);
		if (source == NULL || item.page >= source->GetPageCount())
		{
			fprintf(stderr, "golden render regression failed: %s: fixture/page unavailable\n", item.id.c_str());
			passed = false;
			if (source != NULL) source->Release();
			continue;
		}
		GP<DjVuImage> image = source->GetPage(item.page);
		CDIB* bitmap = image != NULL ? CRenderThread::Render(image, item.size, CDisplaySettings(),
			item.displayMode, item.rotate, item.thumbnail) : NULL;
		vector<BYTE> pixels;
		string actual;
		const bool valid = CanonicalRgb24(bitmap, pixels) && Sha256(pixels, actual);
		if (!valid)
		{
			fprintf(stderr, "golden render regression failed: %s: renderer did not produce RGB24 bitmap\n", item.id.c_str());
			passed = false;
		}
		else if (update)
		{
			item.width = bitmap->GetWidth();
			item.height = bitmap->GetHeight();
			item.hash = actual;
			printf("UPDATED %s %dx%d %s\n", item.id.c_str(), item.width, item.height, actual.c_str());
		}
		else if (item.hash.length() != 64 || item.width != bitmap->GetWidth() || item.height != bitmap->GetHeight() || item.hash != actual)
		{
			fprintf(stderr, "GOLDEN_MISMATCH fixture=%s case=%s expected=%dx%d %s actual=%dx%d %s\n", item.fixture.c_str(), item.id.c_str(),
				item.width, item.height, item.hash.c_str(), bitmap->GetWidth(), bitmap->GetHeight(), actual.c_str());
			SaveActual(item, bitmap);
			passed = false;
		}
		else
			printf("PASS %s %dx%d %s\n", item.id.c_str(), item.width, item.height, actual.c_str());
		delete bitmap;
		source->Release();
	}
	if (update && passed && !WriteBaseline(baselinePath, cases)) passed = Fail("could not write requested baseline update");
	printf("Golden render regression: %s\n", passed ? "PASS" : "FAIL");
	return passed ? 0 : 1;
}
