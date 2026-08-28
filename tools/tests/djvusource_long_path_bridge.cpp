#include "../../src/stdafx.h"
#include "../../src/DjVuSource.h"
#include "../../src/libdjvu/DataPool.h"

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
}

extern "C" int RunDjVuSourceLongPathBridge(LPCTSTR path)
{
	RegressionApplication application;
	DjVuSource::SetApplication(&application);
	DjVuSource* source = DjVuSource::FromFile(path);
	if (source == NULL)
	{
		DjVuSource::SetApplication(NULL);
		return 1;
	}
	bool decoded = false;
	if (source->GetPageCount() > 0)
	{
		GP<DjVuImage> page = source->GetPage(0);
		decoded = !!page && page->get_width() > 0 && page->get_height() > 0;
	}
	source->Release();
	DataPool::close_all();
	DjVuSource::SetApplication(NULL);
	return decoded ? 0 : 1;
}
