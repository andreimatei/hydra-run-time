########### MT-Alpha components ###########

EXTRA_DIST += \
	mtalpha-sim/slrt.s \
	mtalpha-sim/main.c \
	mtalpha-sim/callgate.s

nobase_dist_pkgdata_DATA += \
	t-spr/slimpl.m4 \
	t-spr/include/sl_support.h \
	t-sppalpha/slimpl.m4 \
	t-sppalpha/include/sl_support.h

if ENABLE_SLC_MTALPHA

nobase_pkglib_DATA += \
	mtalpha-sim/slrt.o \
	mtalpha-sim/libslc.a

mtalpha_sim_libslc_a_CONTENTS = \
	mtalpha-sim/callgate.o \
	mtalpha-sim/main.o

mtalpha-sim/lib%.a:
	$(AM_V_at)rm -f $@
	$(AM_V_AR)$(AR_MTALPHA) cru $@ $^
	$(AM_V_at)$(RANLIB_MTALPHA) $@

mtalpha-sim/libslc.a: $(mtalpha_sim_libslc_a_CONTENTS)

SLC_MTALPHA = $(SLC_RUN) -b mtalpha -nostdlib

mtalpha-sim/%.o: $(srcdir)/mtalpha-sim/%.s
	$(AM_V_at)$(MKDIR_P) mtalpha-sim
	$(slc_verbose)$(SLC_MTALPHA) -c -o $@ $<

mtalpha-sim/%.o: $(srcdir)/mtalpha-sim/%.c
	$(AM_V_at)$(MKDIR_P) mtalpha-sim
	$(slc_verbose)$(SLC_MTALPHA) -c -o $@ $<

CLEANFILES += \
	mtalpha-sim/callgate.o \
	mtalpha-sim/main.o \
	mtalpha-sim/slrt.o \
	mtalpha-sim/libslc.a

endif

