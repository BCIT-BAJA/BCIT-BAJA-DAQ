@echo off
cd %~dp0

for /f %%i in ('svn info --show-item revision') do set r=%%i

for /f "tokens=2 delims==" %%a in ('wmic OS Get localdatetime /value') do set "dt=%%a"
set "YYYY=%dt:~0,4%"
set "MM=%dt:~4,2%"
set "DD=%dt:~6,2%"
set "HH=%dt:~8,2%"
set "Min=%dt:~10,2%"
set "Sec=%dt:~12,2%"

echo /* %YYYY%-%MM%-%DD% %HH%:%Min%:%Sec% */ > version_revision.h
echo #pragma once                         >> version_revision.h
echo #define rc_version_revision()  "%r%" >> version_revision.h
echo #define rc_date_yyyy() "%YYYY%" >> version_revision.h
echo #define rc_date_mm()   "%MM%"   >> version_revision.h
echo #define rc_date_dd()   "%DD%"   >> version_revision.h
echo #define rc_date_hour() "%HH%"   >> version_revision.h
echo #define rc_date_min()  "%Min%"  >> version_revision.h
echo #define rc_date_sec()  "%Sec%"  >> version_revision.h