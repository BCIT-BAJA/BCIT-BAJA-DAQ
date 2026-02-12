cd "$(dirname "$(realpath "$0")")";
r=`svn info --show-item revision`

d=$(date '+%Y-%m-%d %H:%M:%S')
yyyy=${d:0:4}
mm=${d:5:2}
dd=${d:8:2}
hour=${d:11:2}
min=${d:14:2}
sec=${d:17:2}

echo "/* ${d} */"  > version_revision.h
echo "#pragma once"                        >> version_revision.h
echo "#define rc_version_revision() \"${r}\"" >> version_revision.h
echo "#define rc_date_yyyy() \"${yyyy}\""  >> version_revision.h
echo "#define rc_date_mm()   \"${mm}\""    >> version_revision.h
echo "#define rc_date_dd()   \"${dd}\""    >> version_revision.h
echo "#define rc_date_hour() \"${hour}\""  >> version_revision.h
echo "#define rc_date_min()  \"${min}\""   >> version_revision.h
echo "#define rc_date_sec()  \"${sec}\""   >> version_revision.h

cat version_revision.h