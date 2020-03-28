Packaging for macOS
===================

To package the plugin for macOS, follow these steps:

* Install `Packages` from http://s.sudre.free.fr/Software/Packages/
    * Alternatively run `curl -o './Packages.pkg' --retry-connrefused -s --retry-delay 1 'https://s3-us-west-2.amazonaws.com/obs-nightly/Packages.pkg'`
    * Then install `sudo installer -pkg ./Packages.pkg -target /`
* Change to the root folder of the `rematrix-filter` repository
* Create necessary directoriy via `mkdir build`
* Copy over `rematrix-plugin.so` via `cp ../obs-studio/build/plugins/rematrix-filter/rematrix-filter.so ./build/`
* Copy over the `data` directory via `cp -r ./data ./build/`
* Finally run `packagesbuild ./CI/macos/rematrix-filter.pkgproj`
