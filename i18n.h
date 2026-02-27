#if !defined(I18N_H)
#define I18N_H

#include <libintl.h>
#include <locale.h>

#if !defined(GETTEXT_PACKAGE)
#define GETTEXT_PACKAGE "cecup"
#endif

#if !defined(LOCALEDIR)
#define LOCALEDIR "/usr/local/share/locale"
#endif

#define _(String) gettext(String)
#define N_(String) String

#endif /* I18N_H */
