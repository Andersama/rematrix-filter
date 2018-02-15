# Exit if something fails
set -e

git fetch --tags

# Leave rematrix-filter (not obs-studio) folder
cd ../

# clone special repo into place
git clone --recursive https://github.com/Andersama/obs-studio.git

# cd back in
cd obs-studio
# get all changes
git fetch --all
#check the branch we're on
git branch
#checkout the install branch for the plugin
git checkout -b rematrix-filter-install
# check the branch we're on again
git branch
# hard reset the branch to the origin
git reset --hard origin/rematrix-filter-install
git submodule update --init --recursive
cd plugins
cd rematrix-filter
#update the submodule
git log master --pretty=oneline --max-count=3
git checkout master
git status
git pull
git log master --pretty=oneline --max-count=3
#back out to plugins
cd ../
#back out to obs-studio
cd ../

#back out of obs-studio like original (now we have custom obs-studio in place)
cd ../

#install deps

# Install Packages app so we can build a package later
# http://s.sudre.free.fr/Software/Packages/about.html
curl -L -O https://s3-us-west-2.amazonaws.com/obs-nightly/Packages.pkg -f --retry 5 -C -
sudo installer -pkg ./Packages.pkg -target /

brew update

#Base OBS Deps
brew install qt5 jack

# Fetch and untar prebuilt OBS deps that are compatible with older versions of OSX
curl -L -O https://s3-us-west-2.amazonaws.com/obs-nightly/osx-deps.tar.gz -f --retry 5 -C -
tar -xf ./osx-deps.tar.gz -C /tmp

# Fetch vlc codebase
curl -L -o vlc-master.zip https://github.com/videolan/vlc/archive/master.zip -f --retry 5 -C -
unzip -q ./vlc-master.zip

# Get sparkle
curl -L -o ./sparkle.tar.bz2 https://github.com/sparkle-project/Sparkle/releases/download/1.16.0/Sparkle-1.16.0.tar.bz2
mkdir ./sparkle
tar -xf ./sparkle.tar.bz2 -C ./sparkle

# CEF Stuff
curl -kLO http://opensource.spotify.com/cefbuilds/cef_binary_3.2883.1540.gedbfb20_macosx64.tar.bz2 -f --retry 5 -C -
tar -xf ./cef_binary_3.2883.1540.gedbfb20_macosx64.tar.bz2
cd ./cef_binary_3.2883.1540.gedbfb20_macosx64
mkdir build
cd ./build
cmake -DCMAKE_CXX_FLAGS="-std=c++11 -stdlib=libc++" -DCMAKE_EXE_LINKER_FLAGS="-std=c++11 -stdlib=libc++" -DCMAKE_OSX_DEPLOYMENT_TARGET=10.9 ..
make -j4
mkdir libcef_dll
cd ../../
