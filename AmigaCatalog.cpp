/*
** Copyright 2009-2020 Adrien Destugues, pulkomandy@pulkomandy.tk.
** Distributed under the terms of the MIT License.
*/

#include "AmigaCatalog.h"

#include <iostream>
#include <memory>
#include <new>

#include <arpa/inet.h>
#include <libgen.h>

#include <Application.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <fs_attr.h>
#include <Language.h>
#include <Mime.h>
#include <Path.h>
#include <Resources.h>
#include <Roster.h>
#include <StackOrHeapArray.h>
#include <String.h>
#include <UTF8.h>

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
AmigaCatalog::AmigaCatalog(const entry_ref& owner, const char *language,
	uint32 fingerprint)
	:
	HashMapCatalog("", language, fingerprint)
{
	// This catalog uses the executable name to identify the catalog
	// (not the MIME signature)
	BEntry entry(&owner);
	char buffer[PATH_MAX];
	entry.GetName(buffer);
	fSignature = buffer;

	// This catalog uses the translated language name to identify the catalog
	// (not the ISO language code)
	BLanguage lang(language);
	lang.GetNativeName(fLanguageName);

	// give highest priority to catalog living in sub-folder of app's folder:
	BString catalogName(kCatFolder);
	catalogName << fLanguageName
		<< "/" << fSignature
		<< kCatExtension;

	image_info info;
	int32 cookie = 0;
	status_t r = get_next_image_info(B_CURRENT_TEAM, &cookie, &info);
	BString dirName(dirname(info.name));
	dirName << "/" << catalogName;

	status_t status = ReadFromFile(dirName.String());

	if (status != B_OK) {
		// look in common-etc folder (/boot/home/config/etc):
		BPath commonEtcPath;
		find_directory(B_USER_ETC_DIRECTORY, &commonEtcPath);
		if (commonEtcPath.InitCheck() == B_OK) {
			dirName = BString(commonEtcPath.Path())
							<< "/" << catalogName;
			status = ReadFromFile(dirName.String());
		}
	}

	if (status != B_OK) {
		// look in system-etc folder (/boot/beos/etc):
		BPath systemEtcPath;
		find_directory(B_SYSTEM_ETC_DIRECTORY, &systemEtcPath);
		if (systemEtcPath.InitCheck() == B_OK) {
			dirName = BString(systemEtcPath.Path())
							<< "/" << catalogName;
			status = ReadFromFile(dirName.String());
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

				while (strings.Position() < chunkSize) {
					strings.Read(&strID, sizeof(strID));
					strings.Read(&strLen, sizeof(strLen));
					strID = ntohl(strID);
					strLen = ntohl(strLen);
					if (strLen & 3) {
						strLen &= ~3;
						strLen += 4;
					}
					char strBase[strLen];
					char* strVal = strBase;
					strings.Read(strBase, strLen);

					if (strBase[1] == 0)
					{
						// Skip the \0 marker for menu entries…
						strLen -= 2;
						strVal += 2;
					}

					char outVal[1024];
					int32 outLen = 1024;
					int32 cookie = 0;

					convert_to_utf8(B_ISO1_CONVERSION, strVal, &strLen,
						outVal, &outLen, &cookie);

					// If the UTF-8 version is shorter, it's likely that
					// something went wrong. Keep the original string.
					if (outLen > strLen)
						SetString(strID, outVal);
					else
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
AmigaCatalog::Instantiate(const entry_ref &owner, const char *language,
	uint32 fingerprint)
{
	AmigaCatalog *catalog
		= new(std::nothrow) AmigaCatalog(owner, language, fingerprint);
	if (catalog && catalog->InitCheck() != B_OK) {
		delete catalog;
		return NULL;
	}
	return catalog;
}


// #pragma mark -


extern "C" BCatalogData *
instantiate_catalog(const entry_ref &owner, const char *language,
	uint32 fingerprint)
{
	return AmigaCatalog::Instantiate(owner, language, fingerprint);
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
