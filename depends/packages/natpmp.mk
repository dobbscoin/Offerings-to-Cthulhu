package=natpmp
$(package)_version=20230423
$(package)_download_path=https://miniupnp.tuxfamily.org/files/
$(package)_file_name=lib$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=0684ed2c8406437e7519a1bd20ea83780db871b3a3a5d752311ba3e889dbfc70

define $(package)_set_vars
$(package)_build_opts=CC="$($(package)_cc)"
$(package)_build_opts_darwin=LIBTOOL="$($(package)_libtool)"
# Two mingw-only adjustments:
#   - natpmp.h resolves to __declspec(dllimport) unless the library is declared
#     static (natpmp_declspec.h), which breaks compiling the library itself.
#   - upstream's LIBOBJS is natpmp.o + getgateway.o, but on Windows natpmp.c
#     does #define gettimeofday natpmp_gettimeofday, and that lives in
#     wingettimeofday.c. Without it in the archive the daemon link dies on an
#     undefined natpmp_gettimeofday.
$(package)_build_opts_mingw32=CPPFLAGS=-DNATPMP_STATICLIB LIBOBJS="natpmp.o getgateway.o wingettimeofday.o"
$(package)_build_env+=CFLAGS="$($(package)_cflags) $($(package)_cppflags)" AR="$($(package)_ar)"
endef

# The release tarball ships without the VERSION file its Makefile cats for the
# soname. Harmless for a static build, but it prints a shell error per invocation
# and we would rather the build log stay readable.
define $(package)_preprocess_cmds
  echo $($(package)_version) > VERSION
endef

define $(package)_build_cmds
	$(MAKE) libnatpmp.a $($(package)_build_opts)
endef

define $(package)_stage_cmds
	mkdir -p $($(package)_staging_prefix_dir)/include $($(package)_staging_prefix_dir)/lib &&\
	install natpmp.h natpmp_declspec.h $($(package)_staging_prefix_dir)/include &&\
	install libnatpmp.a $($(package)_staging_prefix_dir)/lib
endef
