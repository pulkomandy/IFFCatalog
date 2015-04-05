Amiga applications use a locale catalog format based on IFF. It is fairly simple:

* type: CTLG
* chunks: FVER, LANG, CSET, STRS

* FVER: the standard version chunk; contain the version of the app to be localized
* LANG: holds a string which is the name of the target language
* CSET: I don't know what this is, apparently unused.
* STRS: the translated strings. The format is 2 DWORDs followed by the string data.
  The first dword is the string ID, the second is the length. Each string is padded
  so each entry in the table starts on a DWORD boundary.

String lookup is done by ID only, unlike the Haiku native catalog formats which
does lookup by strings instead. This is faster as there is no need to hash the
source string, and no possibility of hash collision.

This project is distributed under the terms of the MIT license.
