 #                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT( [recordmydesktop], [0.6.0], )
AM_INIT_AUTOMAKE

AC_CONFIG_SRCDIR([src/rmd.c])
AM_CONFIG_HEADER(config.h)



# Checks for programs.

AC_PROG_CC
AM_PROG_CC_C_O

AC_CANONICAL_HOST
AC_C_BIGENDIAN 
# Checks for header files.

AC_HEADER_DIRENT
AC_HEADER_STDC

AC_PATH_X
AC_PATH_XTRA

if test "x$x_libraries" != "x" && test "x$x_libraries" != xNONE ; then
    echo "X libraries are found in $x_libraries"
    LIBS="-L$x_libraries  $LIBS";
fi

if test "x$x_includes" != "x" && test "x$x_includes" != xNONE ; then
    echo "X includes are found in $x_includes"
    CFLAGS="-I$x_includes  $CFLAGS";
fi

AC_ARG_ENABLE(oss,
     [  --enable-oss[=yes]    compile with OSS(don't check for ALSA)],
     [case "${enableval}" in
       yes) oss=true ;;
       no)  oss=false ;;
       *) AC_MSG_ERROR(bad value ${enableval} for --enable-oss) ;;
     esac],[oss=false])

AC_ARG_ENABLE(jack,
     [  --enable-jack[=yes]    compile with Jack support],
     [case "${enableval}" in
       yes) jack=true ;;
       no)  jack=false ;;
       *) AC_MSG_ERROR(bad value ${enableval} for --enable-jack) ;;
     esac],[jack=true])

AC_CHECK_HEADER([alsa/asoundlib.h])
AC_CHECK_HEADER([endian.h],default_endian=true)
if test x$default_endian != xtrue; then
AC_CHECK_HEADER([machine/endian.h],
                AC_DEFINE([HAVE_MACHINE_ENDIAN_H],1,
                            endian.h in $include_path/machine/ subdirectory))
fi

AC_CHECK_HEADER([sys/soundcard.h])

AC_CHECK_HEADERS([sys/time.h unistd.h vorbis/vorbisfile.h fcntl.h])



AC_CHECK_TYPES([u_int16_t],,AC_MSG_ERROR([Type u_int16_t must be available!]))
AC_CHECK_TYPES([u_int32_t],,AC_MSG_ERROR([Type u_int32_t must be available!]))
AC_CHECK_TYPES([u_int64_t],,)

# Checks for libraries.

AC_CHECK_LIB([m],[isnan],,)
AC_CHECK_LIB([z],[deflate],,AC_MSG_ERROR([Can't find zlib]))
AC_CHECK_LIB([popt], poptSetOtherOptionHelp,,AC_MSG_ERROR([Can't find libpopt]))
AC_CHECK_LIB([ICE],[IceOpenConnection],,AC_MSG_ERROR([Can't find libICE]),)
AC_CHECK_LIB([SM],[SmcOpenConnection],,AC_MSG_ERROR([Can't find libSM]),)
AC_CHECK_LIB([X11],[XOpenDisplay],,AC_MSG_ERROR([Can't find libX11]),
            -L$x_libraries $X_PRE_LIBS)
AC_CHECK_LIB([Xext],[XShmQueryVersion],,AC_MSG_ERROR([Can't find libXext]))
AC_CHECK_LIB([Xfixes], [XFixesQueryExtension],,
             AC_MSG_ERROR([Can't find libXfixes]))
AC_CHECK_LIB([Xdamage], [XDamageQueryExtension],,
             AC_MSG_ERROR([Can't find libXdamage]))
AC_CHECK_LIB([vorbis],[vorbis_info_clear],,
             AC_MSG_ERROR([Can't find libvorbis]))
AC_CHECK_LIB([vorbisfile],[ov_open],,
             AC_MSG_ERROR([Can't find libvorbisfile]),-lvorbis)
AC_CHECK_LIB([vorbisenc],[vorbis_encode_init],,
             AC_MSG_ERROR([Can't find libvorbisenc]),-lvorbis)
AC_CHECK_LIB([ogg],[ogg_stream_init],,AC_MSG_ERROR([Can't find libogg]))
AC_CHECK_LIB([theora],[theora_encode_YUVin],,
             AC_MSG_ERROR([Can't find libtheora]))
AC_CHECK_LIB([pthread],[pthread_mutex_lock],,
             AC_MSG_ERROR([Can't find libpthread]))

if test x$jack = xtrue ; then
AC_CHECK_LIB([jack],[jack_activate],
             [LIBS="$LIBS -ljack"
              AC_DEFINE([HAVE_LIBJACK], [1], [Define to 1 if you have libjack.])
              jack_lib_found=true])
fi

if test x$oss = xfalse; then
    AC_CHECK_LIB([asound],[snd_pcm_drain],,
                audio_backend="OSS")
else
    audio_backend="OSS"
fi

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.
AC_FUNC_CLOSEDIR_VOID
AC_FUNC_MALLOC

AC_CONFIG_FILES([Makefile
        src/Makefile
        doc/Makefile ])
AC_OUTPUT

if test x$audio_backend != xOSS; then
    audio_backend="ALSA"
fi
if test x$jack_lib_found = xtrue ; then
    jack_support="Enabled"
else
    jack_support="Disabled"
fi
echo ""
echo ""
echo "****************************************"
echo ""
echo "Audio driver that will be used: $audio_backend"
echo ""
echo "Compile with Jack support: $jack_support"
echo ""
echo "****************************************"
echo ""
echo ""
