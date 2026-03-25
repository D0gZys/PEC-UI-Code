# Copie SRC_FILE vers DST_FILE uniquement si DST_FILE n'existe pas encore.
# Utilisation :
#   cmake -DSRC_FILE=<src> -DDST_FILE=<dst> -P CopyIfNotExists.cmake
if (NOT EXISTS "${DST_FILE}")
    file(COPY_FILE "${SRC_FILE}" "${DST_FILE}")
endif()
