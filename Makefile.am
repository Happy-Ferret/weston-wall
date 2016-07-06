AM_DISTCHECK_CONFIGURE_FLAGS = \
	--with-westonplugindir="$$dc_install_base/$(westonplugindir)" \
	$(null)

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}
AM_MAKEFLAGS = --no-print-directory

westonmodule_LTLIBRARIES =

EXTRA_DIST = \
	autogen.sh \
	README.md \
	$(null)
CLEANFILES = \
	$(dist_pkgdata_DATA:.xml=-server-protocol.h) \
	$(dist_pkgdata_DATA:.xml=-client-protocol.h) \
	$(null)


AM_CPPFLAGS += \
	-I $(srcdir)/src/ \
	-I $(builddir)/src/ \
	$(null)

AM_CFLAGS += \
	$(WESTON_CFLAGS) \
	$(null)

MODULES_LDFLAGS = \
	-avoid-version \
	-module \
	-shared \
	$(null)


# notification-area
westonmodule_LTLIBRARIES += notification-area.la
CLEANFILES += $(nodist_weston_notification_area_la_SOURCES)

notification_area_la_SOURCES = \
	src/notification-area.c \
	$(null)

nodist_notification_area_la_SOURCES = \
	src/unstable/notification-area/notification-area-unstable-v1-protocol.c \
	src/unstable/notification-area/notification-area-unstable-v1-server-protocol.h \
	$(null)

notification_area_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	$(MODULES_LDFLAGS) \
	$(null)

notification_area_la_LIBADD = \
	$(WESTON_LIBS) \
	$(null)

notification-area.la src/notification-area.lo: src/unstable/notification-area/notification-area-unstable-v1-server-protocol.h


# Wayland protocol code generation rules
src/%-protocol.c: $(wwprotocoldir)/%.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(WAYLAND_SCANNER) code < $< > $@

src/%-server-protocol.h: $(wwprotocoldir)/%.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(WAYLAND_SCANNER) server-header < $< > $@

src/%-client-protocol.h: $(wwprotocoldir)/%.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(WAYLAND_SCANNER) client-header < $< > $@

-include local-rules.mk

#
# Hooks
#

install-data-hook: la-files-install-hook
uninstall-westonmoduleLTLIBRARIES uninstall-hook: la-files-uninstall-hook

ww_remove_la = $(if $(strip $($(1)_LTLIBRARIES)),( cd $(DESTDIR)$($(1)dir) && rm -f $($(1)_LTLIBRARIES) ))
ww_remove_so_from_la = $(if $(strip $($(1)_LTLIBRARIES)),for la in $($(1)_LTLIBRARIES); do \
		$(INSTALL) "$(abs_builddir)/$(LT_OBJDIR)"/$${la}i "$(DESTDIR)$($(1)dir)"/$${la}; \
	done )

# *.la files cleanup
la-files-install-hook:
	$(call ww_remove_la,westonmoduledir)

# Remove shared object files at uninstall since
# we remove *.la files at install
la-files-uninstall-hook:
	$(call ww_remove_so_from_la,westonmoduledir)
