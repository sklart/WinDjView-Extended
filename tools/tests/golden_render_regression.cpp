// Exact-pixel regression for the production CRenderThread renderer.
#include "../../src/stdafx.h"
#include "../../src/DjVuSource.h"
#include "../../src/RenderThread.h"
#include "../../src/Drawing.h"
#include "../../src/TileRequest.h"

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

	bool IsCommitId(const string& value)
	{
		if (value.length() < 7 || value.length() > 64) return false;
		for (size_t i = 0; i < value.length(); ++i)
			if (!isxdigit(static_cast<unsigned char>(value[i]))) return false;
		return true;
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

	bool LoadBaseline(const char* path, string& baselineCommit, vector<GoldenCase>& cases)
	{
		string text;
		if (!ReadFile(path, text)) return Fail("could not read baseline");
		if (!ReadString(text, "baseline_commit", baselineCommit) || !IsCommitId(baselineCommit))
			return Fail("baseline has invalid provenance commit");

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

	bool WriteBaseline(const char* path, const string& baselineCommit, const vector<GoldenCase>& cases)
	{
		ofstream output(path, ios::out | ios::binary | ios::trunc);
		if (!output) return false;
		output << "{\n  \"schema_version\": 1,\n  \"baseline_commit\": \"" << baselineCommit << "\",\n";
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

	class TileObserver : public Observer
	{
	public:
		TileObserver() : event(::CreateEvent(NULL, TRUE, FALSE, NULL)), count(0), valid(false) {}
		~TileObserver() { ::CloseHandle(event); }
		virtual void OnUpdate(const Observable*, const Message* message)
		{
			if (message == NULL || message->code != PAGE_RENDERED) return;
			const BitmapMsg* bitmap = static_cast<const BitmapMsg*>(message);
			valid = CanonicalRgb24(bitmap->pDIB, pixels) && bitmap->pIdentity != NULL;
			if (bitmap->pIdentity != NULL)
				request = *static_cast<const RenderRequest*>(bitmap->pIdentity);
			delete bitmap->pDIB;
			InterlockedIncrement(&count);
			::SetEvent(event);
		}
		HANDLE event;
		volatile LONG count;
		bool valid;
		RenderRequest request;
		vector<BYTE> pixels;
	};

	bool RunTileEquivalence(const CStringA& corpusRoot)
	{
		const RenderRequest identity(0, CSize(2048, 2048), 0,
			CDjVuView::Color, CDisplaySettings());
		const TileKey first(identity, TileRect(0, 0, 512, 512), 0, 0);
		const TileKey same(identity, TileRect(0, 0, 512, 512), 0, 0);
		const TileKey otherTile(identity, TileRect(512, 0, 512, 512), 1, 0);
		RenderRequest otherRender(identity);
		otherRender.rotation = 1;
		if (!(first == same) || first == otherTile ||
			first == TileKey(otherRender, first.rect, 0, 0))
			return Fail("tile key identity mismatch");
		struct TileCase { const char* fixture; int page, width, height, rotation, mode; bool adjusted, cropped; };
		const TileCase cases[] = {
			{ "watchmaker.djvu", 0, 2301, 1901, 0, CDjVuView::Color, false, false },
			{ "watchmaker.djvu", 0, 2301, 1901, 1, CDjVuView::Color, true, false },
			{ "cable_1973_100133.djvu", 0, 2048, 2051, 0, CDjVuView::BlackAndWhite, false, false },
			{ "cable_1973_100133.djvu", 0, 2048, 2051, 1, CDjVuView::BlackAndWhite, true, true },
			{ "cable_1973_100133.djvu", 0, 2048, 2051, 0, CDjVuView::Color, false, false },
			{ "war_1812.djvu", 3, 2301, 1901, 2, CDjVuView::Background, false, false },
			{ "war_1812.djvu", 3, 2301, 1901, 0, CDjVuView::Foreground, false, false },
			{ "watchmaker.djvu", 0, 2301, 1901, 0, CDjVuView::Color, false, true }
		};
		for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index)
		{
			const TileCase& item = cases[index];
			CString path;
			path.Format(_T("%S\\%S"), static_cast<LPCSTR>(corpusRoot), item.fixture);
			DjVuSource* source = DjVuSource::FromFile(path);
			if (source == NULL || item.page >= source->GetPageCount())
			{
				if (source != NULL) source->Release();
				return Fail("tile equivalence fixture unavailable");
			}
			GP<DjVuImage> image = source->GetPage(item.page);
			CDisplaySettings settings;
			if (item.adjusted)
			{
				settings.bAdjustDisplay = true;
				settings.nBrightness = 12;
				settings.bInvertColors = true;
			}
			settings.bCropPages = item.cropped;
			RenderRequest request(item.page, CSize(item.width, item.height), item.rotation,
				item.mode, settings);
			CDIB* full = image != NULL ? CRenderThread::RenderFullPage(image, request) : NULL;
			CDIB* tiled = image != NULL ? CRenderThread::RenderTiled(image, request) : NULL;
			CDIB* automatic = image != NULL ? CRenderThread::Render(image, request) : NULL;
			if (item.cropped && full != NULL && tiled != NULL && automatic != NULL)
			{
				// Exercise the same post-render crop operation used by viewport jobs.
				const CRect crop(17, 23, item.width - 29, item.height - 31);
				CDIB* croppedFull = full->Crop(crop);
				CDIB* croppedTiled = tiled->Crop(crop);
				CDIB* croppedAutomatic = automatic->Crop(crop);
				delete full;
				delete tiled;
				delete automatic;
				full = croppedFull;
				tiled = croppedTiled;
				automatic = croppedAutomatic;
			}
			vector<BYTE> fullPixels, tiledPixels, automaticPixels;
			const bool equal = CanonicalRgb24(full, fullPixels) &&
				CanonicalRgb24(tiled, tiledPixels) && CanonicalRgb24(automatic, automaticPixels) &&
				fullPixels == tiledPixels && fullPixels == automaticPixels;
			bool workerEqual = true;
			if (equal)
			{
				vector<BYTE> workerReference(fullPixels);
				if (item.cropped)
				{
					PageInfo info = source->GetPageInfo(item.page);
					CSize pageSize(info.szPage);
					if (request.rotation % 2 != 0) swap(pageSize.cx, pageSize.cy);
					const CRect crop = info.GetCropRect((request.rotation + info.nInitialRotate) % 4);
					const double sx = static_cast<double>(request.size.cx) / pageSize.cx;
					const double sy = static_cast<double>(request.size.cy) / pageSize.cy;
					RenderRequest expanded(request);
					expanded.size = expanded.size + CPoint(static_cast<int>((crop.left + crop.right) * sx),
						static_cast<int>((crop.top + crop.bottom) * sy));
					CDIB* raw = CRenderThread::RenderFullPage(image, expanded);
					CDIB* cropped = raw != NULL ? raw->Crop(CRect(
						static_cast<int>(crop.left * sx), static_cast<int>(crop.top * sy),
						static_cast<int>(crop.left * sx) + request.size.cx,
						static_cast<int>(crop.top * sy) + request.size.cy)) : NULL;
					workerEqual = CanonicalRgb24(cropped, workerReference);
					delete cropped;
					delete raw;
				}
				TileObserver observer;
				CRenderThread* worker = new CRenderThread(source, &observer);
				worker->PauseJobs();
				HANDLE fallbackGate = index == 0 ? ::CreateEvent(NULL, TRUE, FALSE, NULL) : NULL;
				if (fallbackGate != NULL) worker->SetTileStartGateForRegression(fallbackGate);
				worker->AddViewportJob(request, CRenderThread::CurrentPageRender);
				worker->AddViewportJob(request, CRenderThread::CurrentPageRender);
				TileGrid grid(item.width, item.height);
				workerEqual = workerEqual && worker->GetQueuedJobCount() == static_cast<int>(grid.Count());
				worker->ResumeJobs();
				if (fallbackGate != NULL)
				{
					bool overlapping = false;
					for (int attempt = 0; attempt < 2000; ++attempt)
					{
						CRenderThread::TileWorkerMetrics snapshot;
						worker->GetTileWorkerMetrics(snapshot);
						if (snapshot.activeTileWorkers >= 2) { overlapping = true; break; }
						::Sleep(1);
					}
					workerEqual = workerEqual && overlapping;
					::SetEvent(fallbackGate);
				}
				workerEqual = workerEqual &&
					::WaitForSingleObject(observer.event, 30000) == WAIT_OBJECT_0 &&
					observer.valid && observer.request == request &&
					observer.pixels == workerReference;
				::Sleep(25);
				workerEqual = workerEqual && InterlockedCompareExchange(&observer.count, 0, 0) == 1;
				int regions = 0, fallbacks = 0;
				worker->GetTileRenderStats(regions, fallbacks);
				CRenderThread::TileWorkerMetrics tileMetrics;
				worker->GetTileWorkerMetrics(tileMetrics);
				printf("tile workers: peak=%d completed=%d rejected=%d fallbacks=%d\n",
					tileMetrics.peakActiveTileWorkers, tileMetrics.completedTileJobs,
					tileMetrics.rejectedTileResults, tileMetrics.tileFallbacks);
				workerEqual = workerEqual && tileMetrics.configuredTileWorkers >= 2 &&
					tileMetrics.configuredTileWorkers <= 4 &&
					tileMetrics.peakActiveTileWorkers >= 1 &&
					tileMetrics.peakActiveTileWorkers <= tileMetrics.configuredTileWorkers &&
					tileMetrics.completedTileJobs >= 1;
				const bool completeRegions = regions == static_cast<int>(grid.Count()) && fallbacks == 0;
				const bool singleFallback = regions == 0 && fallbacks == 1;
				workerEqual = workerEqual && (completeRegions || singleFallback);
				if (item.mode == CDjVuView::BlackAndWhite)
					workerEqual = workerEqual && completeRegions &&
						tileMetrics.peakActiveTileWorkers >= 2 &&
						tileMetrics.completedTileJobs == static_cast<int>(grid.Count());
				if (singleFallback)
					workerEqual = workerEqual && tileMetrics.tileFallbacks == 1 &&
						(tileMetrics.peakActiveTileWorkers == 1 ||
						 tileMetrics.rejectedTileResults > 0);
				if (fallbackGate != NULL)
				{
					for (int attempt = 0; attempt < 2000; ++attempt)
					{
						worker->GetTileWorkerMetrics(tileMetrics);
						if (tileMetrics.activeTileWorkers == 0) break;
						::Sleep(1);
					}
					workerEqual = workerEqual && singleFallback &&
						tileMetrics.peakActiveTileWorkers >= 2 &&
						tileMetrics.rejectedTileResults >= 1;
					if (tileMetrics.activeTileWorkers == 0) ::CloseHandle(fallbackGate);
				}
				if (!workerEqual)
				{
					fprintf(stderr, "tile worker mismatch: regions=%d expected=%llu fallbacks=%d peak=%d completed=%d rejected=%d\n",
						regions, grid.Count(), fallbacks, tileMetrics.peakActiveTileWorkers,
						tileMetrics.completedTileJobs, tileMetrics.rejectedTileResults);
					if (observer.pixels.size() == workerReference.size())
						for (size_t n = 0; n < workerReference.size(); ++n)
							if (observer.pixels[n] != workerReference[n])
							{
								fprintf(stderr, "first different pixel x=%llu y=%llu channel=%llu actual=%u expected=%u\n",
									(n / 3) % item.width, (n / 3) / item.width, n % 3,
									observer.pixels[n], workerReference[n]);
								break;
							}
				}
				worker->Stop();
			}
			if (equal && item.mode == CDjVuView::BlackAndWhite && !item.cropped)
			{
				RenderRequest replacement(request);
				replacement.rotation = 2;
				CDIB* replacementFull = CRenderThread::RenderFullPage(image, replacement);
				vector<BYTE> replacementPixels;
				const bool referenceValid = CanonicalRgb24(replacementFull, replacementPixels);
				delete replacementFull;
				TileObserver observer;
				CRenderThread* worker = new CRenderThread(source, &observer);
				worker->PauseJobs();
				worker->AddViewportJob(request, CRenderThread::CurrentPageRender);
				worker->AddViewportJob(replacement, CRenderThread::CurrentPageRender);
				TileGrid grid(item.width, item.height);
				bool replaced = referenceValid &&
					worker->GetQueuedJobCount() == static_cast<int>(grid.Count());
				CRenderThread::SchedulerMetrics metrics;
				worker->GetSchedulerMetrics(metrics);
				replaced = replaced && metrics.obsoleteJobsRemoved >= static_cast<int>(grid.Count());
				worker->ResumeJobs();
				replaced = replaced && ::WaitForSingleObject(observer.event, 30000) == WAIT_OBJECT_0 &&
					observer.valid && observer.request == replacement &&
					observer.pixels == replacementPixels;
				::Sleep(25);
				replaced = replaced && InterlockedCompareExchange(&observer.count, 0, 0) == 1;
				worker->Stop();
				workerEqual = workerEqual && replaced;
				if (!replaced) fprintf(stderr, "tile queued stale replacement failed\n");

				// Replace a request after at least two real region renders are active.
				// This exercises the worker/batch boundary, not only queue policy.
				RenderRequest live(request);
				RenderRequest liveReplacement(live);
				liveReplacement.rotation = 2;
				CDIB* liveFull = CRenderThread::RenderFullPage(image, liveReplacement);
				vector<BYTE> livePixels;
				bool liveValid = CanonicalRgb24(liveFull, livePixels);
				delete liveFull;
				TileObserver liveObserver;
				CRenderThread* liveWorker = new CRenderThread(source, &liveObserver);
				HANDLE gate = ::CreateEvent(NULL, TRUE, FALSE, NULL);
				liveWorker->SetTileStartGateForRegression(gate);
				liveWorker->AddViewportJob(live, CRenderThread::CurrentPageRender);
				bool started = false;
				const int liveTileCount = static_cast<int>(TileGrid(live.size.cx, live.size.cy).Count());
				for (int attempt = 0; attempt < 2000; ++attempt)
				{
					CRenderThread::TileWorkerMetrics snapshot;
					liveWorker->GetTileWorkerMetrics(snapshot);
					if (snapshot.activeTileWorkers >= 2 &&
						snapshot.completedTileJobs < liveTileCount - 2)
					{
						started = true;
						break;
					}
					::Sleep(1);
				}
				if (started)
				{
					CRenderThread::JobWindows emptyWindow;
					liveWorker->ReconcileJobs(emptyWindow);
					liveWorker->AddViewportJob(liveReplacement, CRenderThread::CurrentPageRender);
				}
				::SetEvent(gate);
				bool livePass = liveValid && started &&
					::WaitForSingleObject(liveObserver.event, 30000) == WAIT_OBJECT_0 &&
					liveObserver.valid && liveObserver.request == liveReplacement &&
					liveObserver.pixels == livePixels;
				::Sleep(25);
				CRenderThread::TileWorkerMetrics liveMetrics;
				liveWorker->GetTileWorkerMetrics(liveMetrics);
				livePass = livePass && InterlockedCompareExchange(&liveObserver.count, 0, 0) == 1 &&
					liveMetrics.rejectedTileResults > 0 &&
					liveMetrics.peakActiveTileWorkers <= 4;
				for (int attempt = 0; attempt < 2000; ++attempt)
				{
					liveWorker->GetTileWorkerMetrics(liveMetrics);
					if (liveMetrics.activeTileWorkers == 0) break;
					::Sleep(1);
				}
				if (liveMetrics.activeTileWorkers == 0) ::CloseHandle(gate);
				liveWorker->Stop();
				workerEqual = workerEqual && livePass;
				if (!livePass)
					fprintf(stderr, "live tile replacement failed: started=%d peak=%d rejected=%d count=%ld\n",
						started, liveMetrics.peakActiveTileWorkers,
						liveMetrics.rejectedTileResults,
						InterlockedCompareExchange(&liveObserver.count, 0, 0));

				// Stop while multiple workers are held on an actual tile job. The
				// stop event must release them without publishing an incomplete page.
				static TileObserver shutdownObserver;
				CRenderThread* shutdownWorker = new CRenderThread(source, &shutdownObserver);
				HANDLE shutdownGate = ::CreateEvent(NULL, TRUE, FALSE, NULL);
				shutdownWorker->SetTileStartGateForRegression(shutdownGate);
				shutdownWorker->AddViewportJob(live, CRenderThread::CurrentPageRender);
				bool shutdownActive = false;
				for (int attempt = 0; attempt < 2000; ++attempt)
				{
					CRenderThread::TileWorkerMetrics snapshot;
					shutdownWorker->GetTileWorkerMetrics(snapshot);
					if (snapshot.activeTileWorkers >= 2) { shutdownActive = true; break; }
					::Sleep(1);
				}
				shutdownWorker->Stop();
				::SetEvent(shutdownGate);
				::Sleep(50);
				workerEqual = workerEqual && shutdownActive &&
					InterlockedCompareExchange(&shutdownObserver.count, 0, 0) == 0;
				// Keep the gate handle alive through asynchronous worker teardown.
				// This test process releases it on exit.
			}
			delete full;
			delete tiled;
			delete automatic;
			source->Release();
			if (!equal || !workerEqual)
			{
				fprintf(stderr, "tile/full-page mismatch: %s rotation=%d mode=%d crop=%d %dx%d\n",
					item.fixture, item.rotation, item.mode, item.cropped, item.width, item.height);
				return false;
			}
			printf("PASS tile/full-page %s rotation=%d mode=%d crop=%d %dx%d\n",
				item.fixture, item.rotation, item.mode, item.cropped, item.width, item.height);
		}
		return true;
	}
}

int _tmain(int argc, TCHAR** argv)
{
	if (!AfxWinInit(::GetModuleHandle(NULL), NULL, ::GetCommandLine(), 0) || (argc != 3 && argc != 5))
		return 2;
	const bool update = argc == 5 && _tcscmp(argv[3], _T("--update-baseline")) == 0;
	if (argc == 5 && !update) return 2;

	CStringA baselinePath(argv[1]);
	CStringA corpusRoot(argv[2]);
	CStringA updateCommitText = update ? CStringA(argv[4]) : CStringA();
	string updateCommit = update ? string(static_cast<LPCSTR>(updateCommitText)) : string();
	if (update && !IsCommitId(updateCommit)) return 2;
	vector<GoldenCase> cases;
	string baselineCommit;
	if (!LoadBaseline(baselinePath, baselineCommit, cases)) return 1;

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
	if (update && passed && !WriteBaseline(baselinePath, updateCommit, cases)) passed = Fail("could not write requested baseline update");
	if (!update && !RunTileEquivalence(corpusRoot)) passed = false;
	printf("Golden render regression: %s\n", passed ? "PASS" : "FAIL");
	return passed ? 0 : 1;
}
