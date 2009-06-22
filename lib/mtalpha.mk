########### MT-Alpha components ###########

EXTRA_DIST += \
	src/mtalpha/mtdiv.s \
	src/mtalpha/divide.S \
	src/mtalpha/memset.S \
	src/mtalpha/memcpy.s \
	src/malloc_intrinsics.h \
	src/malloc_intrinsics.c \
	src/malloc.c \
	src/strlib.c

nobase_dist_pkgdata_DATA += \
	mtalpha-sim/include/svp_os.h \
	mtalpha-sim/include/svp_asmdiv.h

if ENABLE_MTALPHA

nobase_pkglib_DATA += \
   mtalpha-sim/libsl.a

mtalpha_sim_libsl_a_CONTENTS = \
	mtalpha-sim/mtdiv.o \
	mtalpha-sim/roman.o \
	mtalpha-sim/io.o \
	mtalpha-sim/divl.o \
	mtalpha-sim/divq.o \
	mtalpha-sim/reml.o \
	mtalpha-sim/remq.o \
	mtalpha-sim/memset.o \
	mtalpha-sim/memcpy.o \
	mtalpha-sim/malloc_intrinsics.o \
	mtalpha-sim/malloc.o \
	mtalpha-sim/strlib.o

SLC_MTALPHA = $(SLC) -b ppp-mtalpha -nostdlib

mtalpha-sim/io.o: $(srcdir)/src/io.sl
	$(AM_V_at)$(MKDIR_P) mtalpha-sim
	$(slc_verbose)$(SLC_MTALPHA) -c -o $@ $<

mtalpha-sim/div.o: $(srcdir)/src/div.sl
	$(AM_V_at)$(MKDIR_P) mtalpha-sim
	$(slc_verbose)$(SLC_MTALPHA) -c -o $@ $<

mtalpha-sim/roman.o: $(srcdir)/src/roman.sl
	$(AM_V_at)$(MKDIR_P) mtalpha-sim
	$(slc_verbose)$(SLC_MTALPHA) -c -o $@ $<

mtalpha-sim/malloc_intrinsics.o: $(srcdir)/src/malloc_intrinsics.c
	$(AM_V_at)$(MKDIR_P) mtalpha-sim
	$(slc_verbose)$(SLC_MTALPHA) -c -o $@ $<

MALLOC_DEFS = -DLACKS_SYS_TYPES_H \
	-DHAVE_MMAP=0 \
	-DMORECORE_CANNOT_TRIM \
	-DMALLOC_FAILURE_ACTION="" \
	-DLACKS_UNISTD_H \
	-DLACKS_ERRNO_H \
	-DLACKS_STDLIB_H \
	-DLACKS_STRING_H \
	-DLACKS_SYS_PARAM_H \
	-DLACKS_TIME

mtalpha-sim/malloc.o: $(srcdir)/src/malloc.c
	$(AM_V_at)$(MKDIR_P) mtalpha-sim
	$(slc_verbose)$(SLC_MTALPHA) -c -o $@ $< $(MALLOC_DEFS)

mtalpha-sim/strlib.o: $(srcdir)/src/strlib.c
	$(AM_V_at)$(MKDIR_P) mtalpha-sim
	$(slc_verbose)$(SLC_MTALPHA) -c -o $@ $<

mtalpha-sim/mtdiv.o: $(srcdir)/src/mtalpha/mtdiv.s
	$(AM_V_at)$(MKDIR_P) mtalpha-sim
	$(slc_verbose)$(SLC_MTALPHA) -c -o $@ $<

mtalpha-sim/divl.s: $(srcdir)/src/mtalpha/divide.S
	$(AM_V_at)$(MKDIR_P) mtalpha-sim
	$(AM_V_GEN)$(CPP) -o $@ $< -DDIV -DINTSIZE

mtalpha-sim/reml.s: $(srcdir)/src/mtalpha/divide.S
	$(AM_V_at)$(MKDIR_P) mtalpha-sim
	$(AM_V_GEN)$(CPP) -o $@ $< -DDIV -DINTSIZE

mtalpha-sim/divq.s: $(srcdir)/src/mtalpha/divide.S
	$(AM_V_at)$(MKDIR_P) mtalpha-sim
	$(AM_V_GEN)$(CPP) -o $@ $< -DDIV

mtalpha-sim/remq.s: $(srcdir)/src/mtalpha/divide.S
	$(AM_V_at)$(MKDIR_P) mtalpha-sim
	$(AM_V_GEN)$(CPP) -o $@ $< -DREM

mtalpha-sim/memseti.s: $(srcdir)/src/mtalpha/memset.S
	$(AM_V_at)$(MKDIR_P) mtalpha-sim
	$(AM_V_GEN)$(CPP) -o $@ $<

mtalpha-sim/memset.o: mtalpha-sim/memseti.s
	$(AM_V_at)$(MKDIR_P) mtalpha-sim
	$(slc_verbose)$(SLC_MTALPHA) -c -o $@ $<

mtalpha-sim/divl.o: mtalpha-sim/divl.s
	$(AM_V_at)$(MKDIR_P) mtalpha-sim
	$(slc_verbose)$(SLC_MTALPHA) -c -o $@ $<

mtalpha-sim/reml.o: mtalpha-sim/reml.s
	$(AM_V_at)$(MKDIR_P) mtalpha-sim
	$(slc_verbose)$(SLC_MTALPHA) -c -o $@ $<

mtalpha-sim/divq.o: mtalpha-sim/divq.s
	$(AM_V_at)$(MKDIR_P) mtalpha-sim
	$(slc_verbose)$(SLC_MTALPHA) -c -o $@ $<

mtalpha-sim/remq.o: mtalpha-sim/remq.s
	$(AM_V_at)$(MKDIR_P) mtalpha-sim
	$(slc_verbose)$(SLC_MTALPHA) -c -o $@ $<

mtalpha-sim/memcpy.o: $(srcdir)/src/mtalpha/memcpy.s
	$(AM_V_at)$(MKDIR_P) mtalpha-sim
	$(slc_verbose)$(SLC_MTALPHA) -c -o $@ $<

mtalpha-sim/libsl.a: $(mtalpha_sim_libsl_a_CONTENTS)
	$(AM_V_at)rm -f $@
	$(AM_V_AR)$(AR_MTALPHA) cru $@ $^
	$(AM_V_at)$(RANLIB_MTALPHA) $@

endif

CLEANFILES += \
	mtalpha-sim/io.o \
	mtalpha-sim/roman.o \
	mtalpha-sim/div.o \
	mtalpha-sim/mtdiv.o \
	mtalpha-sim/divl.o \
	mtalpha-sim/reml.o \
	mtalpha-sim/divq.o \
	mtalpha-sim/remq.o \
	mtalpha-sim/divl.s \
	mtalpha-sim/reml.s \
	mtalpha-sim/divq.s \
	mtalpha-sim/remq.s \
	mtalpha-sim/memcpy.o \
	mtalpha-sim/memseti.s \
	mtalpha-sim/memset.o \
	mtalpha-sim/malloc_intrinsics.o \
	mtalpha-sim/malloc.o \
	mtalpha-sim/strlib.o \
	mtalpha-sim/libsl.a

