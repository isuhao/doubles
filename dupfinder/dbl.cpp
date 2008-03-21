/******************************************************************************
    Duplicate Files Finder - search for duplicate files
    Copyright (C) 2007-2008 Matthias Boehm

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

******************************************************************************/

#include "stdinc.h"

using namespace std;
#include "os_cc_specific.h"
#include "dbl.h"
#include "file.h"

/********* options ************/

#if defined(__UNIX__) && !defined(_WIN32)

// for unix, there is no native routine implemented, 
// so use wxWidgets
#define FINDFILES_USE_WXWIDGETS

#else

// for windows, there is a native routine, 
// which is much faster than the wxWidget's one

#ifndef FINDFILES_USE_WXWIDGETS
// #define FINDFILES_USE_WXWIDGETS
#endif

#endif /* defined(__UNIX__) && !defined(_WIN32) */


// option, which controls profiling
// on windows platforms
// include "profile.h" always after this definition!
// #define PROFILE

#include "profile.h"


// for testing
// #define TEST
// #define RANDOM_NOISE

#define REFRESH_INTERVAL 1 /* in seconds */


#ifdef PROFILE
LARGE_INTEGER all;
LARGE_INTEGER disk;
LARGE_INTEGER comparetime;
LARGE_INTEGER fileseek;
LARGE_INTEGER fileopen;
LARGE_INTEGER fileread;
#endif /* defined(PROFILE) */

void DuplicateFilesFinder::FindDuplicateFiles(list<DuplicatesGroup> & duplicates) {
	
	multiset_fileinfosize sortedbysize;
	findfileinfo ffi;
	
	ffi.pFilesBySize = &sortedbysize;
	ffi.paths = this->paths;

	FindFiles(ffi, this->gui, this->bQuiet);

	GetEqualFiles(sortedbysize, this->gui, this->bQuiet);

	multiset_fileinfosize::iterator it1;
	list<fileinfoequal>::iterator it2;
	DuplicatesGroup dupl;

	for(it1 = sortedbysize.begin(); it1 != sortedbysize.end(); it1++) {
		dupl.size = it1->size;
		for(it2 = it1->equalfiles.begin(); !it1->equalfiles.empty(); it2 = it1->equalfiles.begin()) {
			dupl.files = it2->files;
			// immediately delete memory not needed, to be keeping memory 
			// usage low
			duplicates.push_back(dupl);
			it1->equalfiles.erase(it2);
		}
	}
}

class AddFileToList : public wxExtDirTraverser
{
public:
	AddFileToList(findfileinfo * pInfo, const pathinfo *ppi, wxULongLong &_nFiles, 
		guiinfo * _guii /*= NULL*/) : ffi(pInfo), pi(ppi), guii(_guii), 
		bDirChanged(true), nFiles(_nFiles) {
#ifdef PROFILE
		__OnFile.QuadPart = 0;
		__OnDir.QuadPart = 0;
		__findsize.QuadPart = 0;
		__normalize.QuadPart = 0;
		__finddir.QuadPart = 0;
		__insert.QuadPart = 0;
#endif
		tlast = time(NULL);
	}

	virtual wxDirTraverseResult OnFile(const wxString &filename, const wxULongLong *pSize)
	{
		if(IsSymLink(filename)) {
			return UpdateInfo(NULL);
		}
		STARTTIME(__OnFile);
		// fileinfo fi;
		File f;
		fileinfosize fis;
		multiset_fileinfosize_it it2;
		wxULongLong size;
		STARTTIME(__findsize);
		// slow
		if(pSize == NULL) {
			size = wxFileName::GetSize(filename);
		} else {
			size = *pSize;
		}
		STOPTIME(__findsize);
		const bool bIncludeZeroFiles = true; // later make an option out of this?
		
		bool bFitsMinSize = size >= pi->nMinSize;
		bool bFitsMaxSize = size <= pi->nMaxSize || pi->nMaxSize == 0;

		assert(pi->nMaxSize >= pi->nMinSize || pi->nMaxSize == 0);
		
		if(size != wxInvalidSize && 
			(size != 0 || bIncludeZeroFiles) &&
			bFitsMinSize &&
			bFitsMaxSize) {
			// init structure
			STARTTIME(__insert);
			// fi.name = filename;
			// fi.data = NULL;
			f.SetName(filename);

			fis.size = size;
			it2 = ffi->pFilesBySize->find(fis);

			
			if(it2 != ffi->pFilesBySize->end()) {
				it2->files.push_back(f);
			}
			else {
				// the next line actually is not needed
				fis.size = size;
				fis.files.push_back(f);
				ffi->pFilesBySize->insert(fis);
			}
			STOPTIME(__insert);
		}
		STOPTIME(__OnFile);

		nFiles++;

		return UpdateInfo(NULL);
	}

	virtual wxDirTraverseResult OnFile(const wxString & filename)
	{
		return OnFile(filename, NULL);
	}

	virtual wxDirTraverseResult OnExtFile(const FileData &data) {
		return OnFile(data.name, &data.size);
	}

	virtual wxDirTraverseResult OnDir(const wxString &dirname)
	{
		// do NOT follow links! 
		if(IsSymLink(dirname)) {
			return wxDIR_IGNORE;
		}
		
		return UpdateInfo(&dirname);
	}

	wxDirTraverseResult UpdateInfo(const wxString *dirname) 
	{
		if(guii) { 
			guii->theApp->Yield();
			while(guii->bPause && guii->bContinue) {
				wxMilliSleep(10);
				guii->theApp->Yield();
			}
			if(!guii->bContinue) {
				return wxDIR_STOP;
			}

			if(dirname) {
				bDirChanged = true;
				curdir = *dirname;
			}

			time_t tcur;
			tcur = time(NULL);
				
			if(tcur - tlast >= REFRESH_INTERVAL ) {
				if(bDirChanged) {
					guii->out->SetValue(curdir);
				}
				wxString tmp;
				tmp.Printf(_T("%") wxLongLongFmtSpec _T("u file(s), %i size(s)"), 
					nFiles.GetValue(), 
					ffi->pFilesBySize->size());
				guii->nfiles->SetLabel(tmp);
			}

			tlast = tcur;
		}
		return wxDIR_CONTINUE;
	}

private:
	findfileinfo *ffi;
	const pathinfo *pi;
	time_t tlast;
	
	guiinfo *guii;

	// for status display
	wxString curdir;
	bool bDirChanged;
	wxULongLong &nFiles;

#ifdef PROFILE
public:
	LARGE_INTEGER __OnFile;
	LARGE_INTEGER __OnDir;
	LARGE_INTEGER __findsize;
	LARGE_INTEGER __normalize;
	LARGE_INTEGER __finddir;
	LARGE_INTEGER __insert;
#endif

};

static bool Traverse(const wxDir &root, wxExtDirTraverser &sink, const wxString &mask, int flags) 
{
#ifdef FINDFILES_USE_WXWIDGETS
	return root.Traverse(sink, mask, flags) != (size_t)-1;
#else
	Traverse(root.GetName(), mask, flags, sink);
	return true;
#endif
}

void	FindFiles(findfileinfo &ffi, guiinfo *guii, bool quiet)
{
	fileinfosize fis;
	list<File>::iterator it;
	multiset_fileinfosize_it it2;
	bool bDeleted;
	wxULongLong nFiles1, nFiles2;
	wxULongLong nSizes1;
	wxULongLong nDroppedFiles;
	list<pathinfo> &paths = ffi.paths;
	list<pathinfo>::const_iterator it3;

#ifdef PROFILE
	LARGE_INTEGER __all = {0, 0};
#endif


	if(!quiet) {
		wxLogMessage(_T("Step 1: Searching files... "));
	}

	nFiles1 = 0;

	for (it3 = paths.begin(); it3 != paths.end(); it3++) {
		if(!quiet) {
			wxLogMessage(_T("        ... in \"%s\" ... "), it3->path.c_str());
		}
		
		AddFileToList traverser(&ffi, &*it3, nFiles1, guii);
		wxString dirname = it3->path;
		wxDir dir(dirname);
		STARTTIME(__all);
		Traverse(dir, traverser, it3->Mask, 
			wxDIR_FILES | (it3->bGoIntoSubDirs ? wxDIR_DIRS : 0 ) | 
			(it3->bSearchHidden ? wxDIR_HIDDEN : 0));
		STOPTIME(__all);
		// _ftprintf(stderr, _T("\n"));
		if(guii) {
			if(!guii->bContinue) {
				return;
			}
		}

		
	#ifdef PROFILE
		_ftprintf(stderr, _T("all:      % 8.3f\n"), SECONDS(__all));
		_ftprintf(stderr, _T("OnFile:   % 8.3f (%.3f %%) \n"), SECONDS(traverser.__OnFile), 
			SECONDS(traverser.__OnFile)/SECONDS(__all)*100.0);
		_ftprintf(stderr, _T("OnDir:    % 8.3f (%.3f %%) \n"), SECONDS(traverser.__OnDir), 
			SECONDS(traverser.__OnDir)/SECONDS(__all)*100.0);
		_ftprintf(stderr, _T("Rest:     % 8.3f (%.3f %%) \n\n"), SECONDS(__all)-SECONDS(traverser.__OnFile)-SECONDS(traverser.__OnDir), 
			(SECONDS(__all)-SECONDS(traverser.__OnFile)-SECONDS(traverser.__OnDir))/SECONDS(__all)*100.0);
		_ftprintf(stderr, _T("findsize: % 8.3f (%.3f %%) \n\n"), SECONDS(traverser.__findsize), 
			SECONDS(traverser.__findsize)/SECONDS(traverser.__OnFile)*100.0);
		_ftprintf(stderr, _T("insert:   % 8.3f (%.3f %%) \n"), SECONDS(traverser.__insert), 
			SECONDS(traverser.__insert)/SECONDS(traverser.__OnFile)*100.0);
		_ftprintf(stderr, _T("normalize:% 8.3f (%.3f %%) \n"), SECONDS(traverser.__normalize), 
			SECONDS(traverser.__normalize)/SECONDS(traverser.__OnDir)*100.0);
		_ftprintf(stderr, _T("finddir:  % 8.3f (%.3f %%) \n"), SECONDS(traverser.__finddir), 
			SECONDS(traverser.__finddir)/SECONDS(traverser.__OnDir)*100.0);

	#endif
	}

	nSizes1 = ffi.pFilesBySize->size();

	multiset_fileinfosize_it it2tmp;
	
	nFiles2 = 0;
	nDroppedFiles = 0;

	for(it2 = ffi.pFilesBySize->begin(); it2 != ffi.pFilesBySize->end(); bDeleted ? it2 : it2++) {
		if(it2->files.size() > 1) {
			bDeleted = false;
			nFiles2 += it2->files.size();
		}
		else { 
			it2tmp = it2;
			it2++;

			ffi.pFilesBySize->erase(it2tmp);
			bDeleted = true;

			nDroppedFiles++;
		}
	}

	if(!quiet) {
		wxLogMessage(_T("        %") wxLongLongFmtSpec _T("u files have to be compared. \n"), 
		nFiles2.GetValue());
	}

	if(guii) {
		wxString tmp;
		tmp.Printf(_T("%") wxLongLongFmtSpec _T("u file(s), %") wxLongLongFmtSpec _T("u size(s)"), 
			nFiles1.GetValue(), 
			nSizes1.GetValue());
		guii->nfiles->SetLabel(tmp);

		tmp.Printf(_T("%") wxLongLongFmtSpec _T("u file(s), %u sizes"), 
			nFiles2.GetValue(), ffi.pFilesBySize->size());
		guii->cfiles->SetLabel(tmp);
	}


}

void	GetEqualFiles(multiset_fileinfosize & sortedbysize, guiinfo *guii, bool quiet)
{
	list<File>::iterator it, it3;
	multiset_fileinfosize_it it2;
	list<fileinfoequal>::iterator it4;
	int sizeN;
	size_t nSortedBySizeLen;
	time_t tlast, tnow;
	wxULongLong sumsize;
	wxULongLong nDoubleFiles;
	wxULongLong nDifferentFiles;
	wxString output;
	bool bStart = true;

#ifdef TEST
	list<File>::iterator __it3;
	list<File>::iterator __it5, __it;
	multiset_fileinfosize_it __it2;
	list<fileinfoequal>::iterator __it4;
#endif /*defined(TEST) */

#ifdef RANDOM_NOISE
	{
		srand(time(NULL));
		const int randmax = 10000;
		for(int i = 0; i < randmax; i++) {
			int j, k;
			list<File>::iterator st1, st2;
			int a, b;
			a = rand() % sortedbysize.size();
			for(it2 = sortedbysize.begin(), j = 0; j < a; it2++, j++) {}
			b = rand() % it2->files.size();
			for(k = 0, it = it2->files.begin(); k < b; k++, it++) { }
			st1 = it;
			a = rand() % sortedbysize.size();
			for(it2 = sortedbysize.begin(), j = 0; j < a; it2++, j++) {}
			b = rand() % it2->files.size();
			for(k = 0, it = it2->files.begin(); k < b; k++, it++) {}
			st2 = it;

			wxString tmp = st2->GetName();
			st2->SetName(st1->GetName());
			st1->SetName(tmp);
		}
	}
#endif /* RANDOM_NOISE */
	

	if(!quiet) {
		wxLogMessage(_T("Step 2: Comparing files with same size for equality... "));
	}

	tlast = time(NULL);

	sizeN = 0;

	nDifferentFiles = 0;
	sumsize = 0;
	nDoubleFiles = 0;

	STARTTIME(comparetime);

	nSortedBySizeLen = sortedbysize.size();
	
	for(it2 = sortedbysize.begin(); it2 != sortedbysize.end(); it2++) {
		// printf("size %" I64 ": %i file(s) \n", it2->size.QuadPart, it2->files.size());
		sizeN++;
		tnow = time(NULL);
		if(tnow - tlast >= REFRESH_INTERVAL || bStart == true) {
			bStart = false; // at the beginning information should also be displayed!
			if(guii) {
				output.Printf(_T("size %i/%i (%") wxLongLongFmtSpec _T("u bytes, %u files)"), 
					sizeN, nSortedBySizeLen, it2->size.GetValue(), it2->files.size() );
				guii->wProgress->SetLabel(output);
				guii->wSpeed->SetLabel(_T("--"));
			}
			else if (!quiet) {
				deleteline(output.Length());
				output.Printf(_T("size %i/%i (%i files of size %") wxLongLongFmtSpec _T("u)")
					/*" %i kb/s" */, 
					sizeN, nSortedBySizeLen, it2->files.size(), it2->size.GetValue() /*, 0*/);

				_ftprintf(stderr, _T("%s"), output.c_str());
			}
			tlast = tnow;	
		}
		assert(it2->files.size() > 1);
		if(it2->files.size() > 1) { /* in fact, this isn't neccesarry any more */
			bool bDeleted3;
			bool bFirstDouble;
			fileinfoequal fiq;
			list<File>::iterator ittmp;
			bool bEqual;
			for(it = it2->files.begin(); it != it2->files.end(); /*it++*/) {
				bFirstDouble = true;
				it3 = it;
				for(it3++; it3 != it2->files.end(); bDeleted3 ? it3 : it3++) {
					bDeleted3 = false;
					bEqual = comparefiles(*it, *it3, it2->size, guii, quiet);

					if(guii) {
						if(!guii->bContinue) {
							return;
						}
					}

#ifdef TEST
					if(bEqual != comparefiles0(*it, *it3)) { assert(0 == 1); abort(); }
#endif
					if(bEqual) {
						if(bFirstDouble) {
							bFirstDouble = false;
							fiq.files.clear();
							fiq.files.push_back(*it);
							fiq.files.push_back(*it3);
							it2->equalfiles.push_back(fiq);
						}
						else {
							it2->equalfiles.back().files.push_back(*it3);
						}

						nDoubleFiles++;
						sumsize += it2->size;
						
						bDeleted3 = true;
						ittmp = it3;
						it3++;
						ittmp->Close();
						it2->files.erase(ittmp);
					}

					// nComparedBytes.QuadPart += (*it).size.QuadPart;
				}
				if(!bFirstDouble) {
					nDifferentFiles++;
				}
#ifdef TEST
				/* no doubles found */
				if(bFirstDouble) {
					bool bNotEqual = true;
					__it3 = it;
					for(__it3++; __it3 != it2->files.end(); __it3++) {
						bEqual = comparefiles0(*it, *__it3);
						if(bEqual) {
							bNotEqual = false;
							break;
						}
					}
					assert(bNotEqual);
					if(!bNotEqual) { abort(); }
				}
#endif
				it2->files.front().Close();
				it2->files.pop_front();
				it = it2->files.begin();
			}
		} /* if size > 1 */
		/* Close files */
		for(it = it2->files.begin(); it != it2->files.end(); it++) {
			it->Close();
		} 
		it2->files.clear();
	}

	STOPTIME(comparetime);

	if(!quiet) {
		deleteline(output.Length());
		wxLogMessage(_T("        done. \n"));

		wxLogMessage(_T("Found %") wxLongLongFmtSpec _T("u files, of which exist at least one more copy. "), 
			nDifferentFiles.GetValue());
		wxLogMessage(_T("%") wxLongLongFmtSpec _T("u duplicates consume altogether %") wxLongLongFmtSpec
			 _T("u bytes (%") wxLongLongFmtSpec _T("u kb, %") wxLongLongFmtSpec _T("u mb)\n"), 
			nDoubleFiles.GetValue(), sumsize.GetValue(), sumsize.GetValue()/1024, sumsize.GetValue()/1024/1024);
	}

#ifdef TEST
	for(__it2 = sortedbysize.begin(); __it2 != sortedbysize.end(); __it2++) {
		for(__it4 = __it2->equalfiles.begin(); __it4 != __it2->equalfiles.end(); __it4++) {
			for(__it = __it4->files.begin(); __it != __it4->files.end(); __it++) {
				__it5 = __it;
				for(__it5++; __it5 != __it4->files.end(); __it5++) {
					if(!comparefiles0((*__it), (*__it5))) {
						_ftprintf(stderr, _T("Error: %s != %s\n"), 
							__it->GetName().c_str(), 
							__it5->GetName().c_str());
					}
				}
			}
		}
	}
#endif

}

void	deleteline(int n) {
	int t;
	for(t = 0; t < n; t++) {
		_ftprintf(stderr, _T("\b"));
	}
	for(t = 0; t < n; t++) {
		_ftprintf(stderr, _T(" "));
	}
	for(t = 0; t < n; t++) {
		_ftprintf(stderr, _T("\b"));
	}
}

#ifdef TEST
#include "filetest.cpp"
#endif

bool	comparefiles(File &f1, File &f2, const wxULongLong &size, guiinfo *guii, bool quiet) {
	bool bResult;
	static char *b1, *b2;
	char *pb1, *pb2;
	if(b1 == NULL) { b1 = new char[File::GetBufSize()]; }
	if(b2 == NULL) { b2 = new char[File::GetBufSize()]; }
	unsigned int BUFSIZE = File::GetBufSize();
	unsigned int n1, n2;
	wxULongLong nBytesRead = 0, nPrevBytesRead = 0;
	wxString output;
	time_t tstart, tcurrent;

	if(!f1.Open(size)) {
		return false;
	}
	if(!f2.Open(size)) {
		return false;
	}

	// seek to the beginning
	if( !f1.Restart() ) {
		return false;
	}
	if( !f2.Restart() ) {
		return false;
	}

	tstart = time(NULL);

	while(1) {
		if(guii) {
			guii->theApp->Yield();
			while(guii->bPause && guii->bContinue) {
				wxMilliSleep(10);
				guii->theApp->Yield();
			}
			if(!guii->bContinue) {
				return false;
			}
		}

		n1 = n2 = BUFSIZE;
		bool br1, br2;

		pb1 = b1;
		pb2 = b2;

		br1 = f1.Read(&pb1, n1);
		br2 = f2.Read(&pb2, n2);

		nBytesRead += n1;
		nBytesRead += n2;

		if(n1 != n2 || !br1 || !br2) {
			bResult = false;
			goto End;
		}

		if(memcmp(pb1, pb2, n1) != 0) {
			bResult = false;
			goto End;
		}

		if(n1 < BUFSIZE)
			break;

		tcurrent = time(NULL);
		if(tcurrent - tstart >= REFRESH_INTERVAL) {
			// display status
			if(guii) {
				output.Printf(_T(" %.2f mb/sec"), 
					(nBytesRead-nPrevBytesRead).ToDouble()/REFRESH_INTERVAL/1024.0/1024.0);
				guii->wSpeed->SetLabel(output);

			}
			else if(!quiet) {
				deleteline(output.Length());
				output.Printf(_T(" %.2f mb/sec"), (nBytesRead-nPrevBytesRead).ToDouble()/REFRESH_INTERVAL/1024.0/1024.0);
				_ftprintf(stderr, _T("%s"), output.c_str());

			}

			nPrevBytesRead = nBytesRead;
			tstart = tcurrent;
		}
	}

	bResult = true;

End:	if(!quiet) {
		deleteline(output.Length());
	}
	return bResult;
}






