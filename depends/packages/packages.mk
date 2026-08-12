packages:=boost openssl
native_packages := native_ccache native_comparisontool

qt_native_packages = native_protobuf
qt_packages = qrencode protobuf zlib libpng

qt46_linux_packages = qt46 expat dbus libxcb xcb_proto libXau util-macros xproto freetype libX11 xextproto libXext xtrans libICE libSM
qt5_linux_packages= qt expat dbus libxcb xcb_proto libXau util-macros xproto freetype fontconfig libX11 xextproto libXext xtrans

qt_darwin_packages=qt
qt_mingw32_packages=qt

qt_linux_$(USE_LINUX_STATIC_QT5):=$(qt5_linux_packages)
qt_linux_:=$(qt5_linux_packages)
qt_linux_packages:=$(qt_linux_$(USE_LINUX_STATIC_QT5))

wallet_packages=bdb

ifneq ($(build_os),darwin)
darwin_native_packages=native_libuuid native_openssl native_cctools native_cdrkit native_libdmg-hfsplus
endif
