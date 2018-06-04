@echo off
SETLOCAL EnableDelayedExpansion

REM Check if obs-studio build exists.
REM If the obs-studio directory does exist, check if the last OBS tag built
REM matches the latest OBS tag.
REM If the tags match, do not build obs-studio.
REM If the tags do not match, build obs-studio.
REM If the obs-studio directory doesn't exist, build obs-studio.
echo Checking for obs-studio build...

set OBSLatestTagPrePull=0
set OBSLatestTagPostPull=0

set "BuildOBS="

REM Check the last tag successfully built by CI.
if exist C:\projects\obs-studio-last-tag-built.txt (
  set /p OBSLastTagBuilt=<C:\projects\obs-studio-last-tag-built.txt
) else (
  set OBSLastTagBuilt=0
)

if exist C:\projects\obs-studio (
  echo obs-studio directory exists
  echo   Updating tag info
  cd C:\projects\obs-studio
  git fetch --all
  git branch
  cd C:\projects\obs-studio\plugins
  dir
  cd C:\projects\obs-studio
  git checkout -b rematrix-filter-install
  
  git describe --tags --abbrev=0 > C:\projects\latest-obs-studio-tag-pre-pull.txt
  set /p OBSLatestTagPrePull=<C:\projects\latest-obs-studio-tag-pre-pull.txt
  
  git branch
  git reset --hard origin/rematrix-filter-install
  git submodule update --init --recursive
  cd C:\projects\obs-studio\plugins
  dir
  cd C:\projects\obs-studio\plugins\rematrix-filter\
  dir
  git log master --pretty=oneline --max-count=3
  git checkout master
  git status
  git pull
  
  git describe --tags --abbrev=0 > C:\projects\latest-obs-studio-tag-post-pull.txt
  set /p OBSLatestTagPostPull=<C:\projects\latest-obs-studio-tag-post-pull.txt
  
)

REM Check the obs-studio tags for mismatches.
REM If a new tag was pulled, set the build flag.
if not %OBSLatestTagPrePull%==%OBSLatestTagPostPull% (
  echo Latest tag pre-pull: %OBSLatestTagPrePull%
  echo Latest tag post-pull: %OBSLatestTagPostPull%
  echo Tags do not match.  Need to rebuild OBS.
  set BuildOBS=true
)

if not exist C:\projects\obs-studio (
  echo obs-studio directory does not exist
  cd C:\projects
  git clone --recursive https://github.com/Andersama/obs-studio.git
  
  cd C:\projects\obs-studio
  git fetch --all
  git branch
  cd C:\projects\obs-studio\plugins
  dir
  cd C:\projects\obs-studio
  git checkout -b rematrix-filter-install
  git branch
  git reset --hard origin/rematrix-filter-install
  git submodule update --init --recursive
  cd C:\projects\obs-studio\plugins
  dir
  cd C:\projects\obs-studio\plugins\rematrix-filter\
  dir
  git log master --pretty=oneline --max-count=3
  git checkout master
  git status
  git pull
  
  git describe --tags --abbrev=0 > C:\projects\obs-studio-latest-tag.txt
  set /p OBSLatestTag=<C:\projects\obs-studio-latest-tag.txt
  
  set BuildOBS=true
)

REM Some debug info
echo:
echo Latest tag pre-pull: %OBSLatestTagPrePull%
echo Latest tag post-pull: %OBSLatestTagPostPull%
echo Latest tag: %OBSLatestTag%
echo Last built OBS tag: %OBSLastTagBuilt%


if defined BuildOBS (
  echo BuildOBS: true
) else (
  echo BuildOBS: false
)

echo Building obs-studio w/ rematrix filter...
echo:

cd C:\projects\obs-studio\

if exist dependencies2015.zip (curl -kLO https://obsproject.com/downloads/dependencies2015.zip -f --retry 5 -z dependencies2015.zip) else (curl -kLO https://obsproject.com/downloads/dependencies2015.zip -f --retry 5 -C -)
REM if exist dependencies2017.zip (curl -kLO https://obsproject.com/downloads/dependencies2017.zip -f --retry 5 -z dependencies2017.zip) else (curl -kLO https://obsproject.com/downloads/dependencies2017.zip -f --retry 5 -C -)
if exist vlc.zip (curl -kLO https://obsproject.com/downloads/vlc.zip -f --retry 5 -z vlc.zip) else (curl -kLO https://obsproject.com/downloads/vlc.zip -f --retry 5 -C -)

if exist dependencies2015 rmdir /s /q C:\projects\obs-studio\dependencies2015
7z x dependencies2015.zip -odependencies2015
REM 7z x dependencies2017.zip -odependencies2017

if exist vlc rmdir /s /q C:\projects\obs-studio\vlc
7z x vlc.zip -ovlc

echo Setting Up VLC
echo: 

set VLCPath=%CD%\vlc
echo Setting Up QT
echo:

call C:\projects\rematrix-filter\CI\install-setup-qt.cmd

echo   Removing previous build dirs...
if exist build rmdir /s /q C:\projects\obs-studio\build
if exist build32 rmdir /s /q C:\projects\obs-studio\build32
if exist build64 rmdir /s /q C:\projects\obs-studio\build64
echo   Making new build dirs...
mkdir build
mkdir build32
mkdir build64

echo Setting DepsPath
echo:
set DepsPath32=%CD%\dependencies2015\win32
set DepsPath64=%CD%\dependencies2015\win64

echo   Running cmake for obs-studio %OBSLatestTag% 32-bit...
cd ./build32
cmake -G "Visual Studio 14 2015" -DCOPIED_DEPENDENCIES=false -DCOPY_DEPENDENCIES=true ..
echo:
echo:
echo   Running cmake for obs-studio %OBSLatestTag% 64-bit...
cd ../build64
cmake -G "Visual Studio 14 2015 Win64" -DCOPIED_DEPENDENCIES=false -DCOPY_DEPENDENCIES=true ..
echo:
echo:
REM  echo   Building obs-studio %OBSLatestTag% 32-bit ^(Build Config: %build_config%^)...
REM  call msbuild /m /p:Configuration=%build_config% C:\projects\obs-studio\build32\obs-studio.sln /logger:"C:\Program Files\AppVeyor\BuildAgent\Appveyor.MSBuildLogger.dll"
REM  echo   Building obs-studio %OBSLatestTag% 64-bit ^(Build Config: %build_config%^)...
REM  call msbuild /m /p:Configuration=%build_config% C:\projects\obs-studio\build64\obs-studio.sln /logger:"C:\Program Files\AppVeyor\BuildAgent\Appveyor.MSBuildLogger.dll"
REM  cd ..
REM  git describe --tags --abbrev=0 > C:\projects\obs-studio-last-tag-built.txt
REM  set /p OBSLastTagBuilt=<C:\projects\obs-studio-last-tag-built.txt
