/*
** Copyright 2009-2015 Adrien Destugues, pulkomandy@pulkomandy.tk.
** Distributed under the terms of the MIT License.
*/

#include "AmigaCatalog.h"

#include <iostream>
#include <memory>
#include <new>

#include <Application.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <fs_attr.h>
#include <Mime.h>
#include <Path.h>
#include <Resources.h>
#include <Roster.h>
#include <StackOrHeapArray.h>
#include <String.h>

#include <LocaleRoster.h>
#include <Catalog.h>


class BMessage;


using BPrivate::HashMapCatalog;
using BPrivate::AmigaCatalog;


/*	This add-on implements reading of Amiga catalog files. These are IFF files
 *	of type CTLG and used to localize Amiga applications. In most cases you
 *	should use the Haiku standard catalogs instead, unless:
 *	- You are porting an application from Amiga and want to use the same locale
 *		files
 *	- You want to use ID-based string lookup instead of string-to-string, for
 *		performance reasons.
 */


static const char *kCatFolder = "Catalogs/";
static const char *kCatExtension = ".catalog";

const char *AmigaCatalog::kCatMimeType
	= "locale/x-vnd.Be.locale-catalog.amiga";

static int16 kCatArchiveVersion = 1;
	// version of the catalog archive structure, bump this if you change it!


/*
 * constructs a AmigaCatalog with given signature and language and reads
 * the catalog from disk.
 * InitCheck() will be B_OK if catalog could be loaded successfully, it will
 * give an appropriate error-code otherwise.
 */
AmigaCatalog::AmigaCatalog(const char *signature, const char *language,
	uint32 fingerprint)
	:
	HashMapCatalog(signature, language, fingerprint)
{
	// give highest priority to catalog living in sub-folder of app's folder:
	app_info appInfo;
	be_app->GetAppInfo(&appInfo);
	node_ref nref;
	nref.device = appInfo.ref.device;
	nref.node = appInfo.ref.directory;
	BDirectory appDir(&nref);
	BString catalogName(kCatFolder);
	catalogName << fLanguageName
		<< "/" << fSignature
		<< kCatExtension;
	BPath catalogPath(&appDir, catalogName.String());
	status_t status = ReadFromFile(catalogPath.Path());

	if (status != B_OK) {
		// look in common-etc folder (/boot/home/config/etc):
		BPath commonEtcPath;
		find_directory(B_USER_ETC_DIRECTORY, &commonEtcPath);
		if (commonEtcPath.InitCheck() == B_OK) {
			catalogName = BString(commonEtcPath.Path())
							<< kCatFolder << fLanguageName
							<< "/" << fSignature
							<< kCatExtension;
			status = ReadFromFile(catalogName.String());
		}
	}

	if (status != B_OK) {
		// look in system-etc folder (/boot/beos/etc):
		BPath systemEtcPath;
		find_directory(B_SYSTEM_ETC_DIRECTORY, &systemEtcPath);
		if (systemEtcPath.InitCheck() == B_OK) {
			catalogName = BString(systemEtcPath.Path())
							<< kCatFolder << fLanguageName
							<< "/" << fSignature
							<< kCatExtension;
			status = ReadFromFile(catalogName.String());
		}
	}

	fInitCheck = status;
}


/*
 * constructs an empty AmigaCatalog with given sig and language.
 * This is used for editing/testing purposes.
 * InitCheck() will always be B_OK.
 */
AmigaCatalog::AmigaCatalog(const char *path, const char *signature,
	const char *language)
	:
	HashMapCatalog(signature, language, 0),
	fPath(path)
{
	fInitCheck = B_OK;
}


AmigaCatalog::~AmigaCatalog()
{
}


status_t
AmigaCatalog::ReadFromFile(const char *path)
{
	if (!path)
		path = fPath.String();

	BFile source(path, B_READ_ONLY);
	if (source.InitCheck() != B_OK)
		return source.InitCheck();

	// Now read all the data from the file

	int32 tmp;
	source.Read(&tmp, sizeof(tmp)); // IFF header
	if (ntohl(tmp) != 'FORM')
		return B_BAD_DATA;

	int32 dataSize;
	source.Read(&dataSize, sizeof(dataSize)); // File size
	dataSize = ntohl(dataSize);
	source.Read(&tmp, sizeof(tmp)); // File type
	if (ntohl(tmp) != 'CTLG')
		return B_BAD_DATA;

	dataSize -= 4; // Type is included in data size.

	while(dataSize > 0) {
		int32 chunkID, chunkSize;
		source.Read(&chunkID, sizeof(chunkID));
		source.Read(&chunkSize, sizeof(chunkSize));
		chunkSize = ntohl(chunkSize);

		// Round to word
		if (chunkSize & 1) chunkSize++;

		BStackOrHeapArray<char, 256> chunkData(chunkSize);
		source.Read(chunkData, chunkSize);

		chunkID = ntohl(chunkID);

		switch(chunkID) {
			case 'FVER': // Version
				fSignature = chunkData;
				break;
			case 'LANG': // Language
				fLanguageName = chunkData;
				break;

			case 'STRS': // Catalog strings
			{
				BMemoryIO strings(chunkData, chunkSize);
				int32 strID, strLen;
				puts("");

				while (strings.Position() < chunkSize) {
					strings.Read(&strID, sizeof(strID));
					strings.Read(&strLen, sizeof(strLen));
					strID = ntohl(strID);
					strLen = ntohl(strLen);
					if (strLen & 3) {
						strLen &= ~3;
						strLen += 4;
					}
					char strVal[strLen];
					strings.Read(strVal, strLen);

					SetString(strID, strVal);
				}
				break;
			}

			case 'CSET': // Unknown/unused
			default:
				break;
		}
		
		dataSize -= chunkSize + 8;
	}

	fPath = path;
	fFingerprint = ComputeFingerprint();
	return B_OK;
}


status_t
AmigaCatalog::WriteToFile(const char *path)
{
	return B_NOT_SUPPORTED;
}


/*
 * writes mimetype, language-name and signature of catalog into the
 * catalog-file.
 */
void
AmigaCatalog::UpdateAttributes(BFile& catalogFile)
{
	static const int bufSize = 256;
	char buf[bufSize];
	uint32 temp;
	if (catalogFile.ReadAttr("BEOS:TYPE", B_MIME_STRING_TYPE, 0, &buf, bufSize)
			<= 0
		|| strcmp(kCatMimeType, buf) != 0) {
		catalogFile.WriteAttr("BEOS:TYPE", B_MIME_STRING_TYPE, 0,
			kCatMimeType, strlen(kCatMimeType)+1);
	}
	if (catalogFile.ReadAttr(BLocaleRoster::kCatLangAttr, B_STRING_TYPE, 0,
		&buf, bufSize) <= 0
		|| fLanguageName != buf) {
		catalogFile.WriteAttr(BLocaleRoster::kCatLangAttr, B_STRING_TYPE, 0,
			fLanguageName.String(), fLanguageName.Length()+1);
	}
	if (catalogFile.ReadAttr(BLocaleRoster::kCatSigAttr, B_STRING_TYPE, 0,
		&buf, bufSize) <= 0
		|| fSignature != buf) {
		catalogFile.WriteAttr(BLocaleRoster::kCatSigAttr, B_STRING_TYPE, 0,
			fSignature.String(), fSignature.Length()+1);
	}
	if (catalogFile.ReadAttr(BLocaleRoster::kCatFingerprintAttr, B_UINT32_TYPE,
		0, &temp, sizeof(uint32)) <= 0) {
		catalogFile.WriteAttr(BLocaleRoster::kCatFingerprintAttr, B_UINT32_TYPE,
			0, &fFingerprint, sizeof(uint32));
	}
}


void
AmigaCatalog::UpdateAttributes(const char* path)
{
	BEntry entry(path);
	BFile node(&entry, B_READ_WRITE);
	UpdateAttributes(node);
}


BCatalogData *
AmigaCatalog::Instantiate(const char *signature, const char *language,
	uint32 fingerprint)
{
	AmigaCatalog *catalog
		= new(std::nothrow) AmigaCatalog(signature, language, fingerprint);
	if (catalog && catalog->InitCheck() != B_OK) {
		delete catalog;
		return NULL;
	}
	return catalog;
}


extern "C" BCatalogData *
instantiate_catalog(const char *signature, const char *language,
	uint32 fingerprint)
{
	AmigaCatalog *catalog
		= new(std::nothrow) AmigaCatalog(signature, language, fingerprint);
	if (catalog && catalog->InitCheck() != B_OK) {
		delete catalog;
		return NULL;
	}
	return catalog;
}


extern "C" BCatalogData *
create_catalog(const char *signature, const char *language)
{
	AmigaCatalog *catalog
		= new(std::nothrow) AmigaCatalog("emptycat", signature, language);
	return catalog;
}


extern "C" status_t
get_available_languages(BMessage* availableLanguages,
	const char* sigPattern = NULL, const char* langPattern = NULL,
	int32 fingerprint = 0)
{
	// TODO
	return B_ERROR;
}


uint8 gCatalogAddOnPriority = 80;
