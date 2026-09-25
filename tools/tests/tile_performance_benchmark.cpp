// Informational production-path benchmark for bounded viewport tile workers.
#include "../../src/stdafx.h"
#include "../../src/DjVuSource.h"
#include "../../src/RenderThread.h"
#include "../../src/Drawing.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <stdio.h>

namespace
{
	class BenchmarkApplication : public IApplication
	{
	public:
		virtual bool LoadDocSettings(const CString&, DocSettings*) { return false; }
		virtual bool GetCropPages() { return false; }
		virtual DictionaryInfo* GetDictionaryInfo(const CString&, bool) { return NULL; }
		virtual void ReportFatalError() { }
	};

	bool CanonicalRgb24(CDIB* bitmap, vector<BYTE>& pixels)
	{
		if (bitmap == NULL || !bitmap->IsValid()) return false;
		const int width = bitmap->GetWidth(), height = bitmap->GetHeight();
		const int bpp = bitmap->GetBitsPerPixel();
		if (width <= 0 || height <= 0 ||
			(bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32)) return false;
		const size_t stride = (((static_cast<size_t>(width) * bpp + 7) / 8) + 3) & ~static_cast<size_t>(3);
		pixels.resize(static_cast<size_t>(width) * height * 3);
		const bool bottomUp = bitmap->GetBitmapInfo()->bmiHeader.biHeight > 0;
		const RGBQUAD* palette = bitmap->GetPalette();
		const int paletteSize = bitmap->GetColorCount();
		for (int y = 0; y < height; ++y)
		{
			const BYTE* row = bitmap->GetBits() + (bottomUp ? height - 1 - y : y) * stride;
			BYTE* target = &pixels[static_cast<size_t>(y) * width * 3];
			for (int x = 0; x < width; ++x)
			{
				if (bpp == 24 || bpp == 32)
				{
					const BYTE* pixel = row + x * (bpp / 8);
					target[x*3] = pixel[2]; target[x*3+1] = pixel[1]; target[x*3+2] = pixel[0];
				}
				else
				{
					const int color = bpp == 8 ? row[x] :
						(bpp == 4 ? (row[x/2] >> (x % 2 == 0 ? 4 : 0)) & 15 :
						 (row[x/8] >> (7 - x % 8)) & 1);
					if (color >= paletteSize) return false;
					target[x*3] = palette[color].rgbRed;
					target[x*3+1] = palette[color].rgbGreen;
					target[x*3+2] = palette[color].rgbBlue;
				}
			}
		}
		return true;
	}

	double Milliseconds(const LARGE_INTEGER& first, const LARGE_INTEGER& last,
		const LARGE_INTEGER& frequency)
	{
		return 1000.0 * static_cast<double>(last.QuadPart - first.QuadPart) /
			static_cast<double>(frequency.QuadPart);
	}

	class ResultObserver : public Observer
	{
	public:
		struct Publication
		{
			RenderRequest request;
			CDIB* bitmap;
			LARGE_INTEGER time;
		};
		ResultObserver() : event(::CreateEvent(NULL, TRUE, FALSE, NULL)), unexpected(0) {}
		~ResultObserver() { Clear(); ::CloseHandle(event); }
		void Begin(const RenderRequest& wanted)
		{
			lock.Lock();
			Clear();
			expected = wanted;
			unexpected = 0;
			::ResetEvent(event);
			lock.Unlock();
		}
		void Expect(const RenderRequest& wanted)
		{
			lock.Lock();
			expected = wanted;
			::ResetEvent(event);
			lock.Unlock();
		}
		virtual void OnUpdate(const Observable*, const Message* message)
		{
			if (message == NULL || message->code != PAGE_RENDERED) return;
			const BitmapMsg* bitmap = static_cast<const BitmapMsg*>(message);
			LARGE_INTEGER now;
			::QueryPerformanceCounter(&now);
			lock.Lock();
			const RenderRequest* identity = static_cast<const RenderRequest*>(bitmap->pIdentity);
			if (identity == NULL) ++unexpected;
			else
			{
				Publication publication = { *identity, bitmap->pDIB, now };
				results.push_back(publication);
				if (*identity == expected) ::SetEvent(event);
				else ++unexpected;
			}
			if (identity == NULL) delete bitmap->pDIB;
			lock.Unlock();
		}
		bool ExpectedTime(LARGE_INTEGER& when)
		{
			lock.Lock();
			bool ok = false;
			for (size_t i = 0; i < results.size(); ++i)
				if (results[i].request == expected)
				{
					when = results[i].time;
					ok = true;
				}
			lock.Unlock();
			return ok;
		}
		bool Verify(const RenderRequest& first, const vector<BYTE>& firstReference,
			const RenderRequest& last, const vector<BYTE>& lastReference, int expectedCount)
		{
			lock.Lock();
			bool ok = unexpected == 0 && static_cast<int>(results.size()) == expectedCount;
			for (size_t i = 0; i < results.size(); ++i)
			{
				vector<BYTE> pixels;
				const bool firstResult = expectedCount == 2 && i == 0;
				ok = CanonicalRgb24(results[i].bitmap, pixels) && ok &&
					results[i].request == (firstResult ? first : last) &&
					pixels == (firstResult ? firstReference : lastReference);
			}
			lock.Unlock();
			return ok;
		}
		HANDLE event;
	private:
		void Clear()
		{
			for (size_t i = 0; i < results.size(); ++i) delete results[i].bitmap;
			results.clear();
		}
		CCriticalSection lock;
		RenderRequest expected;
		vector<Publication> results;
		int unexpected;
	};

	struct Scenario
	{
		const char* name;
		const char* fixture;
		int firstPage, middlePage, finalPage;
		int rotation, displayMode;
		bool crop, adjust;
		enum Kind { Single, Sequential, Rapid } kind;
	};

	struct Sample
	{
		double totalMs, pageMs;
		int completedTiles, peakWorkers, fallbacks, rejected;
	};

	struct Stats { double median, minimum, maximum; };

	Stats Summarize(const vector<double>& values)
	{
		vector<double> sorted(values);
		sort(sorted.begin(), sorted.end());
		Stats result = { sorted[sorted.size()/2], sorted.front(), sorted.back() };
		return result;
	}

	RenderRequest MakeRequest(const Scenario& scenario, int page)
	{
		CDisplaySettings settings;
		settings.bCropPages = scenario.crop;
		if (scenario.adjust)
		{
			settings.bAdjustDisplay = true;
			settings.nBrightness = 12;
			settings.bInvertColors = true;
		}
		return RenderRequest(page, CSize(3072, 3072), scenario.rotation,
			scenario.displayMode, settings);
	}

	bool Reference(DjVuSource* source, const RenderRequest& request, vector<BYTE>& pixels)
	{
		GP<DjVuImage> image = source->GetPage(request.page);
		if (image == NULL) return false;
		RenderRequest expanded(request);
		CRect crop;
		double sx = 0, sy = 0;
		if (request.displaySettings.bCropPages)
		{
			PageInfo info = source->GetPageInfo(request.page);
			CSize pageSize(info.szPage);
			if (request.rotation % 2 != 0) swap(pageSize.cx, pageSize.cy);
			if (pageSize.cx <= 0 || pageSize.cy <= 0) return false;
			crop = info.GetCropRect((request.rotation + info.nInitialRotate) % 4);
			sx = static_cast<double>(request.size.cx) / pageSize.cx;
			sy = static_cast<double>(request.size.cy) / pageSize.cy;
			expanded.size = request.size + CPoint(
				static_cast<int>((crop.left + crop.right) * sx),
				static_cast<int>((crop.bottom + crop.top) * sy));
		}
		CDIB* result = CRenderThread::RenderFullPage(image, expanded);
		if (result != NULL && request.displaySettings.bCropPages &&
			(crop.left + crop.right + crop.bottom + crop.top > 1))
		{
			CDIB* cropped = result->Crop(CRect(
				static_cast<int>(crop.left * sx), static_cast<int>(crop.top * sy),
				static_cast<int>(crop.left * sx) + request.size.cx,
				static_cast<int>(crop.top * sy) + request.size.cy));
			delete result;
			result = cropped;
		}
		const bool ok = CanonicalRgb24(result, pixels);
		delete result;
		return ok;
	}

	bool WaitForResult(ResultObserver& observer, const LARGE_INTEGER& start,
		const LARGE_INTEGER& frequency, double& elapsed)
	{
		if (::WaitForSingleObject(observer.event, 60000) != WAIT_OBJECT_0) return false;
		LARGE_INTEGER when;
		if (!observer.ExpectedTime(when)) return false;
		elapsed = Milliseconds(start, when, frequency);
		return true;
	}

	bool Drain(CRenderThread* worker)
	{
		for (int attempt = 0; attempt < 30000; ++attempt)
		{
			CRenderThread::TileWorkerMetrics metrics;
			worker->GetTileWorkerMetrics(metrics);
			if (metrics.activeTileWorkers == 0 && worker->GetQueuedJobCount() == 0)
				return true;
			::Sleep(1);
		}
		return false;
	}

	bool RunPass(CRenderThread* worker, ResultObserver& observer, const Scenario& scenario,
		const vector<BYTE>& firstReference, const vector<BYTE>& finalReference,
		const LARGE_INTEGER& frequency, Sample& sample)
	{
		const RenderRequest first = MakeRequest(scenario, scenario.firstPage);
		const RenderRequest middle = MakeRequest(scenario, scenario.middlePage);
		const RenderRequest last = MakeRequest(scenario, scenario.finalPage);
		worker->ResetTileWorkerMetricsForBenchmark();
		observer.Begin(scenario.kind == Scenario::Sequential ? first : last);
		LARGE_INTEGER start, end;
		::QueryPerformanceCounter(&start);
		HANDLE gate = NULL;
		if (scenario.kind == Scenario::Rapid)
		{
			gate = ::CreateEvent(NULL, TRUE, FALSE, NULL);
			if (gate == NULL) return false;
			worker->SetTileStartGateForRegression(gate);
		}
		worker->AddViewportJob(first, CRenderThread::CurrentPageRender);
		if (scenario.kind == Scenario::Sequential)
		{
			double ignored = 0;
			if (!WaitForResult(observer, start, frequency, ignored)) return false;
			observer.Expect(last);
			worker->AddViewportJob(last, CRenderThread::CurrentPageRender);
		}
		else if (scenario.kind == Scenario::Rapid)
		{
			bool active = false;
			for (int attempt = 0; attempt < 2000; ++attempt)
			{
				CRenderThread::TileWorkerMetrics metrics;
				worker->GetTileWorkerMetrics(metrics);
				if (metrics.activeTileWorkers >= 1) { active = true; break; }
				::Sleep(1);
			}
			if (!active) { ::SetEvent(gate); return false; }
			worker->AddViewportJob(middle, CRenderThread::CurrentPageRender);
			worker->AddViewportJob(last, CRenderThread::CurrentPageRender);
			CRenderThread::JobWindows windows;
			windows.renderPages.insert(last.page);
			worker->ReconcileJobs(windows);
			::SetEvent(gate);
		}
		if (!WaitForResult(observer, start, frequency, sample.pageMs) || !Drain(worker))
			return false;
		::QueryPerformanceCounter(&end);
		sample.totalMs = Milliseconds(start, end, frequency);
		if (!observer.Verify(first, firstReference, last, finalReference,
			scenario.kind == Scenario::Sequential ? 2 : 1)) return false;
		CRenderThread::TileWorkerMetrics metrics;
		worker->GetTileWorkerMetrics(metrics);
		sample.completedTiles = metrics.completedTileJobs;
		sample.peakWorkers = metrics.peakActiveTileWorkers;
		sample.fallbacks = metrics.tileFallbacks;
		sample.rejected = metrics.rejectedTileResults;
		if (gate != NULL)
		{
			worker->SetTileStartGateForRegression(NULL);
			::CloseHandle(gate);
		}
		return sample.peakWorkers >= 1 && sample.peakWorkers <= metrics.configuredTileWorkers;
	}

	bool RunScenario(const Scenario& scenario, int workers, const CString& corpusRoot,
		const LARGE_INTEGER& frequency, vector<Sample>& measured)
	{
		CString path;
		path.Format(_T("%s\\%S"), static_cast<LPCTSTR>(corpusRoot), scenario.fixture);
		DjVuSource* source = DjVuSource::FromFile(path);
		if (source == NULL)
		{
			fprintf(stderr, "tile benchmark failed: cannot open %s\n", scenario.fixture);
			return false;
		}
		const int count = source->GetPageCount();
		if (scenario.firstPage >= count || scenario.middlePage >= count ||
			scenario.finalPage >= count)
		{
			fprintf(stderr, "tile benchmark failed: %s has only %d pages\n", scenario.fixture, count);
			source->Release();
			return false;
		}
		vector<BYTE> firstReference, finalReference;
		const bool references = Reference(source, MakeRequest(scenario, scenario.firstPage), firstReference) &&
			Reference(source, MakeRequest(scenario, scenario.finalPage), finalReference);
		if (!references)
		{
			fprintf(stderr, "tile benchmark failed: full-page reference unavailable for %s\n", scenario.name);
			source->Release();
			return false;
		}
		ResultObserver observer;
		CRenderThread* worker = new CRenderThread(source, &observer, workers);
		bool ok = true;
		for (int run = -1; run < 5; ++run)
		{
			Sample sample = {};
			if (!RunPass(worker, observer, scenario, firstReference, finalReference, frequency, sample))
			{
				fprintf(stderr, "tile benchmark failed: %s workers=%d run=%d\n",
					scenario.name, workers, run);
				ok = false;
				break;
			}
			if (run >= 0) measured.push_back(sample);
		}
		worker->Stop();
		source->Release();
		return ok && measured.size() == 5;
	}
}

int _tmain(int argc, TCHAR** argv)
{
	if (!AfxWinInit(::GetModuleHandle(NULL), NULL, ::GetCommandLine(), 0) || argc != 3)
		return 2;
	BenchmarkApplication application;
	DjVuSource::SetApplication(&application);
	const CString corpusRoot(argv[1]);
	const CStringA outputDir(argv[2]);
	LARGE_INTEGER frequency;
	::QueryPerformanceFrequency(&frequency);
	const Scenario scenarios[] = {
		{ "bw_large", "cable_1973_100133.djvu", 0, 0, 0, 0, CDjVuView::BlackAndWhite, false, false, Scenario::Single },
		{ "scanned_large", "big_scanned_page.djvu", 0, 0, 0, 0, CDjVuView::Color, false, false, Scenario::Single },
		{ "rotation", "cable_1973_100133.djvu", 0, 0, 0, 1, CDjVuView::BlackAndWhite, false, false, Scenario::Single },
		{ "crop", "cable_1973_100133.djvu", 0, 0, 0, 1, CDjVuView::BlackAndWhite, true, false, Scenario::Single },
		{ "adjustments", "cable_1973_100133.djvu", 0, 0, 0, 0, CDjVuView::BlackAndWhite, false, true, Scenario::Single },
		{ "sequential", "cable_1973_100133.djvu", 0, 0, 1, 0, CDjVuView::BlackAndWhite, false, false, Scenario::Sequential },
		{ "rapid_distant", "watchmaker.djvu", 0, 11, 6, 0, CDjVuView::Color, false, false, Scenario::Rapid }
	};
	const int workerCounts[] = { 1, 2, 4 };
	const int scenarioCount = sizeof(scenarios) / sizeof(scenarios[0]);
	vector<vector<vector<Sample> > > samples(scenarioCount, vector<vector<Sample> >(3));
	for (int i = 0; i < scenarioCount; ++i)
		for (int w = 0; w < 3; ++w)
			if (!RunScenario(scenarios[i], workerCounts[w], corpusRoot, frequency, samples[i][w]))
				return 1;
	const string root(static_cast<LPCSTR>(outputDir));
	ofstream raw((root + "/samples.csv").c_str(), ios::out | ios::trunc);
	ofstream summary((root + "/summary.csv").c_str(), ios::out | ios::trunc);
	ofstream comparisons((root + "/comparisons.csv").c_str(), ios::out | ios::trunc);
	if (!raw || !summary || !comparisons) return 1;
	raw << "scenario,workers,run,total_ms,page_rendered_ms,tiles_completed,peak_active_workers,tile_fallbacks,rejected_stale_tiles\n";
	summary << "scenario,workers,metric,median,min,max\n";
	comparisons << "scenario,from_workers,to_workers,total_speedup,page_rendered_speedup\n";
	const char* metricNames[] = { "total_ms", "page_rendered_ms", "tiles_completed",
		"peak_active_workers", "tile_fallbacks", "rejected_stale_tiles" };
	vector<vector<vector<Stats> > > metrics(scenarioCount,
		vector<vector<Stats> >(3, vector<Stats>(6)));
	raw << fixed << setprecision(3);
	summary << fixed << setprecision(3);
	comparisons << fixed << setprecision(3);
	for (int i = 0; i < scenarioCount; ++i)
	{
		for (int w = 0; w < 3; ++w)
		{
			vector<double> values[6];
			for (size_t n = 0; n < samples[i][w].size(); ++n)
			{
				const Sample& s = samples[i][w][n];
				const double fields[] = { s.totalMs, s.pageMs,
					static_cast<double>(s.completedTiles), static_cast<double>(s.peakWorkers),
					static_cast<double>(s.fallbacks), static_cast<double>(s.rejected) };
				raw << scenarios[i].name << ',' << workerCounts[w] << ',' << n;
				for (int k = 0; k < 6; ++k)
				{
					raw << ',' << fields[k];
					values[k].push_back(fields[k]);
				}
				raw << '\n';
			}
			for (int k = 0; k < 6; ++k)
			{
				metrics[i][w][k] = Summarize(values[k]);
				const Stats& s = metrics[i][w][k];
				summary << scenarios[i].name << ',' << workerCounts[w] << ',' <<
					metricNames[k] << ',' << s.median << ',' << s.minimum << ',' << s.maximum << '\n';
			}
			printf("%s workers=%d total_ms median=%.3f min=%.3f max=%.3f page_ms median=%.3f tiles=%.0f peak=%.0f fallback=%.0f rejected=%.0f\n",
				scenarios[i].name, workerCounts[w], metrics[i][w][0].median,
				metrics[i][w][0].minimum, metrics[i][w][0].maximum,
				metrics[i][w][1].median, metrics[i][w][2].median,
				metrics[i][w][3].median, metrics[i][w][4].median,
				metrics[i][w][5].median);
		}
		const int pairs[][2] = { {0, 1}, {0, 2}, {1, 2} };
		for (int p = 0; p < 3; ++p)
		{
			const int from = pairs[p][0], to = pairs[p][1];
			const double totalRatio = metrics[i][to][0].median > 0 ?
				metrics[i][from][0].median / metrics[i][to][0].median : 0;
			const double pageRatio = metrics[i][to][1].median > 0 ?
				metrics[i][from][1].median / metrics[i][to][1].median : 0;
			comparisons << scenarios[i].name << ',' << workerCounts[from] << ',' <<
				workerCounts[to] << ',' << totalRatio << ',' << pageRatio << '\n';
			printf("%s %d->%d total=%.3fx page=%.3fx\n", scenarios[i].name,
				workerCounts[from], workerCounts[to], totalRatio, pageRatio);
		}
	}
	printf("Tile performance benchmark: PASS (1 warm-up + 5 measured per scenario/worker count)\n");
	return 0;
}
