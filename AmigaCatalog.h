/*
 * Copyright 2009-2015, Adrien Destugues, pulkomandy@pulkomandy.tk.
 * Distributed under the terms of the MIT License.
 */
#ifndef _AMIGA_CATALOG_H_
#define _AMIGA_CATALOG_H_


#include <HashMapCatalog.h>
#include <DataIO.h>
#include <String.h>


class BFile;

namespace BPrivate {


class AmigaCatalog : public HashMapCatalog {
	public:
		AmigaCatalog(const char *signature, const char *language,
			uint32 fingerprint);
				// constructor for normal use
		AmigaCatalog(entry_ref *appOrAddOnRef);
				// constructor for embedded catalog
		AmigaCatalog(const char *path, const char *signature,
			const char *language);
				// constructor for editor-app

		~AmigaCatalog();

		// implementation for editor-interface:
		status_t ReadFromFile(const char *path = NULL);
		status_t WriteToFile(const char *path = NULL);

		static BCatalogData *Instantiate(const char *signature,
			const char *language, uint32 fingerprint);

		static const char *kCatMimeType;

	private:
		void UpdateAttributes(BFile& catalogFile);
		void UpdateAttributes(const char* path);

		mutable BString		fPath;
};


} // namespace BPrivate


#endif /* _AMIGA_CATALOG_H_ */
