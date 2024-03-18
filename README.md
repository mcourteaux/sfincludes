# SFincludes
SFincludes (shorthand for Search Fix Includes) is a tool that goes
through all of your source and header files in a given directory and
tries to fix all `#include ""` (and optionally `#include <>`) preprocessor
instructions it can find, by looking for files with similar names, in a set of
directories you specify.

## 👀 Why SFincludes?
This is the go-to solution to fix your include statements after reorganizing
your files into different directories. Instead of going through a loop of
[compile, fix include error, repeat]; `SFincludes` analyzes your project and
automatically fixes all your include statements.

## ⚙️  How?
How does it fix the includes? It first gathers all header files (`.h` or
`.hpp`) found in the specified include directories. Next it processes all files
one-by-one found in the specified source directories, and then fixes every
include with one of the found headers. Which header is chosen depends on the
configuration you pass such as fuzzy filename matching, allowing changing
system-includes to user-includes and visa-versa.

The selected header is chosen by file name. By default the filename should
match exactly. When there are multiple files with the exact same name found
during the header-searching process, the one with the minimal changes in path
will be preferred (the other options will be reported as alternatives that were
considered but not chosen). Optionally, fuzzy matching can be done, using a
customized Levenshtein distance (also known as the "edit distance").

Optionally the `.h` extension can be replaced by a `.hpp` extension for *all*
your header files. Note that this tool will _not_ try to figure out whether or
not a particular file is a C or a C++ header. Note that this feature is
unsuitable for projects that use a mix of C and C++.

## 📥 Installation

```bash
git clone https://github.com/mcourteaux/sfincludes
cd sfincludes
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
sudo make install
```

## 🖥️ Command line arguments

```
  --help                     Produce help message.
  --src arg                  Add a source directory to process. [repeat --src
                             to specify more]
  --user-include-path arg    Add user include search path directory (cfr. gcc
                             -Ipath) [repeat --user-include-path to specify
                             more]
  --sys-include-path arg     Add system include search path directory (cfr. gcc
                             -isystem). [repeat --sys-include-path to specify
                             more]
  --fuzzy arg (=0)           Maximal filename edit distance (costs: insert=4,
                             change=2, capitalize=1).
  --process-system-includes  Also process #include <> statements.
  --system-to-user           Replace #include <> with #include "" when the file
                             is found user include search path. Only when
                             --process-system-includes.
  --user-to-system           Replace #include "" with #include <> when the file
                             is found in the system include search path.
  --prefer-relative-to-root  Also rewrite correct includes to be relative to
                             their corresponding search path root.
  --rename-hpp               Rename .h headers files to .hpp.
  --no-dry-run               Actually perform the changes.
  --verbose                  Be verbose.
  --omit-untouched           Do not list the untouched files. Useful for
                             reviewing.
  --omit-system-failed       Do not list the system includes which were not
                             resolved. Useful for reviewing.
```

Meaning of the paths:

 - an **include-path** is a path which will be used to find header files which
   can be included. If an include can be mapped onto a header in this folder,
   the include will be rewritten, relative to this path. You can add as many
   include paths as you like (by repeating the flag).
 - a **src path** is a directory in which it will search for C/C++ files
   (headers and source files) to fix: it will rewrite the `#include` statements
   in these files. You can add as many source folders as you like, which is
   typical if you have a `src/` and an `include/` directory in your project
   (note that you likely also want to fix the include statements within the
   header files itself).

Often, these paths will be the same. Note that you always need to provide both.

By default, `sfincludes` will output what it would do. Note that there is a
summary of the includes it fixed or left untouched. It is advised to first
perform a dry run and inspect the summary to see if there are problems (i.e.,
the total number of successfully processed includes does not match the total
number of includes found). Once confident, changes can actually be executed by
running `sfincludes` with the flag `--no-dry-run`.

If you use the `--rename-hpp` flag, you will most likely want to set the
`--fuzzy` argument to `8`, as the filenames will have changed with a distance of
`8`, i.e., "add `p` (cost 4), and add `p` (cost 4)". Leaving the `--fuzzy`
argument to zero, will rename the files, but will fail finding the includes,
and leave the those includes untouched.

## 🔥 Example

In this example, I moved a couple of related header files to their own separate
subdirectory within the project file structure. For example `neonraw/JRS.hpp`
moved to `neonraw/jrs/JRS.hpp` along with some related files.
For this project, I have a script set up to invoke `SFincludes`:

```bash
#!/bin/bash

SFI=sfincludes
ZDIR=$(dirname "$0")

THIRD_PARTY_SYS_INCLUDES="\
  --sys-include-path ./NeonRAW/ext/tracy/public/ \
  --sys-include-path ./3rd/halide_repo/distrib/include/"

# NeonRAW main
$SFI --process-system-includes --prefer-relative-to-root --system-to-user --user-to-system \
  --src $ZDIR/NeonRAW/src \
  --src $ZDIR/NeonRAW/include \
  --src $ZDIR/NeonRAW/generators \
  --user-include-path $ZDIR/NeonRAW/include \
  --sys-include-path $ZDIR/color/include \
  $THIRD_PARTY_SYS_INCLUDES $@
```
which outputs something like this (only an excerpt, stripped away most output for demonstration puproses):
```
User inlucde path : ./NeonRAW/include
System inlucde path : ./color/include
System inlucde path : ./NeonRAW/ext/tracy/public/
System inlucde path : ./3rd/halide_repo/distrib/include/
Source : ./NeonRAW/src
Source : ./NeonRAW/include
Source : ./NeonRAW/generators
Process system includes.
Convert system includes to user includes when a corresponding file is found in the user include search path.
Convert user includes to system includes when a corresponding file is found in the system include search path.
Prefer include paths to be always written relative to the root.
Fuzzy search : 0
Dry run. (Use --no-dry-run to effectively write changes back to filesystem.)

Index headers in: "./NeonRAW/include"
Index headers in: "./color/include"
Index headers in: "./NeonRAW/ext/tracy/public/"
Index headers in: "./3rd/halide_repo/distrib/include/"


Processing source directory: ./NeonRAW/src...
    Process ./NeonRAW/src/neonraw/image_processing/BilateralGrid.cpp ...
        ✅ Untouched include: "neonraw/image_processing/BilateralGrid.hpp"
        ✅ Untouched include: "neonraw/Engine.hpp"
        ✅ Untouched include: "neonraw/performance/perfrep.hpp"
        💄 Change include type: <neonraw/datasource/DataSource.hpp>  ->  "neonraw/datasource/DataSource.hpp"
        ❓ Failed to fix include: <Eigen/LU>
        ❓ Failed to fix include: <cmath>
        💄 Change include type: "tracy/Tracy.hpp"  ->  <tracy/Tracy.hpp>
        ❓ Failed to fix include: <spdlog/spdlog.h>
        ❓ Failed to fix include: <halide_image_io.h>
    Process ./NeonRAW/src/neonraw/ioutils.cpp ...
        ✅ Untouched include: "neonraw/ioutils.hpp"
    Process ./NeonRAW/src/neonraw/datasource/SentinelDataSource.cpp ...
        ✅ Untouched include: "neonraw/datasource/SentinelDataSource.hpp"
        ✅ Untouched include: <HalideRuntime.h>
    Process ./NeonRAW/src/neonraw/datasource/RawSpeedDataSource.cpp ...
        ✅ Untouched include: "neonraw/datasource/RawSpeedDataSource.hpp"
        ✅ Untouched include: "neonraw/datasource/RawSpeedByproducts.hpp"
        ✅ Untouched include: "neonraw/utils.hpp"
        ✅ Untouched include: "neonraw/adobe_coeff.h"
        ✅ Untouched include: "neonraw/image_processing/BilinearDemosaicer.hpp"
        ✅ Untouched include: "neonraw/math/matrix_pseudo_inverse.hpp"
        ❓ Failed to fix include: <neonraw_RAW_to_xyY.h>
        ❓ Failed to fix include: <decoders/RawDecoderException.h>
        ✅ Untouched include: <color.hpp>
        ❓ Failed to fix include: <Eigen/LU>
        ❓ Failed to fix include: <spdlog/spdlog.h>
        💄 Change include type: "tracy/Tracy.hpp"  ->  <tracy/Tracy.hpp>
        ❓ Failed to fix include: <openssl/sha.h>
    Process ./NeonRAW/src/neonraw/datasource/MemoryBufferDataSource.cpp ...
        ✅ Untouched include: "neonraw/datasource/MemoryBufferDataSource.hpp"
        ✅ Untouched include: <HalideRuntime.h>
    Process ./NeonRAW/src/neonraw/datasource/DataSource.cpp ...
        ✅ Untouched include: "neonraw/datasource/DataSource.hpp"
        ✅ Untouched include: "neonraw/mat.hpp"
        ✅ Untouched include: "neonraw/math/matrix_pseudo_inverse.hpp"
        👕 Replace include path: "neonraw/JRS.hpp"  ->  "neonraw/jrs/JRS.hpp"  (distance: fn=0; dir=16) from "./NeonRAW/include"
        ✅ Untouched include: <color.hpp>

<stripped away a lot...>

    Process ./NeonRAW/include/neonraw/datasource/SentinelDataSource.hpp ...
        👕 Replace include path: "DataSource.hpp"  ->  "neonraw/datasource/DataSource.hpp"  (distance: fn=0; dir=0) from "./NeonRAW/include"
           - Alternative: neonraw/datasource/DataSource.hpp  (distance: fn=0; dir=3) from "./NeonRAW/include"

Replaced path: 15 / 348
Sys-to-user  : 2 / 348
User-to-sys  : 11 / 348
Untouched    : 131 / 348
Failed       : 189 / 348
```

Notice how I had Tracy (a third-party library) included with `#include ""`
syntax, which I prefer to change to `#include <>`, as it is external to the
current project. I indicated this preference by passing `--user-to-system`.
The Tracy include path itself was passed as a system search path via
`--sys-include-path`. `SFincludes` uses this information and rewrites the include
to use `<...>` instead of `"..."` (indicated with the 💄 emoji).

In the report for last file in this example, notice how it rewrites a
file-relative path to search-path relative path. In
`include/neonraw/datasource/` there is both `DataSource.hpp` and
`SentinelDataSource.hpp`. When processing `SentinelDataSource.hpp`, `SFincludes` detects
the `#include "DataSource.hpp"` refers to the file in the same directory.
Because I pass `--prefer-relative-to-root`, it rewrites this to the full path:
`neonraw/datasource/DataSource.hpp`. Path rewrites are indicated with the 👕 emoji.

Note that it is not recommended to add actual system includes to the command
line arguments, as that just slows down the entire process, without any actual
benefit. Additionally, if you miss any of the real system search paths, you
risk incorrectly rewriting includes with the wrong target.

When reviewing during the dry-runs and interested in only seeing the changes,
you can additionally pass `--omit-untouched` and `--omit-system-failed` to get
a simplified report. For example:
```
Processing source directory: /home/martijn/zec/NeonRAW/include...
    Process /home/martijn/zec/NeonRAW/include/neonraw/jrs/Unique.hpp ...
    Process /home/martijn/zec/NeonRAW/include/neonraw/jrs/JRS.hpp ...
        👕 Replace include path: "JRS_fwd.hpp"  ->  "neonraw/jrs/JRS_fwd.hpp"  (distance: fn=0; dir=0) from "/home/martijn/zec/NeonRAW/include"
           - Alternative: neonraw/jrs/JRS_fwd.hpp  (distance: fn=0; dir=3) from "/home/martijn/zec/NeonRAW/include"
        👕 Replace include path: "ResourceMemory.hpp"  ->  "neonraw/jrs/ResourceMemory.hpp"  (distance: fn=0; dir=0) from "/home/martijn/zec/NeonRAW/include"
           - Alternative: neonraw/jrs/ResourceMemory.hpp  (distance: fn=0; dir=3) from "/home/martijn/zec/NeonRAW/include"
        👕 Replace include path: "assert.hpp"  ->  "neonraw/debug/assert.hpp"  (distance: fn=0; dir=12) from "/home/martijn/zec/NeonRAW/include"
        👕 Replace include path: "build_type.hpp"  ->  "neonraw/debug/build_type.hpp"  (distance: fn=0; dir=12) from "/home/martijn/zec/NeonRAW/include"
        👕 Replace include path: "DotVisualizer.hpp"  ->  "neonraw/jrs/DotVisualizer.hpp"  (distance: fn=0; dir=0) from "/home/martijn/zec/NeonRAW/include"
           - Alternative: neonraw/jrs/DotVisualizer.hpp  (distance: fn=0; dir=3) from "/home/martijn/zec/NeonRAW/include"
        👕 Replace include path: "JRS_impl.hpp"  ->  "neonraw/jrs/JRS_impl.hpp"  (distance: fn=0; dir=0) from "/home/martijn/zec/NeonRAW/include"
           - Alternative: neonraw/jrs/JRS_impl.hpp  (distance: fn=0; dir=3) from "/home/martijn/zec/NeonRAW/include"
    Process /home/martijn/zec/NeonRAW/include/neonraw/jrs/ResourceMemory.hpp ...
    Process /home/martijn/zec/NeonRAW/include/neonraw/jrs/DotVisualizer.hpp ...
        👕 Replace include path: "Unique.hpp"  ->  "neonraw/jrs/Unique.hpp"  (distance: fn=0; dir=0) from "/home/martijn/zec/NeonRAW/include"
           - Alternative: neonraw/jrs/Unique.hpp  (distance: fn=0; dir=3) from "/home/martijn/zec/NeonRAW/include"
        👕 Replace include path: "demangle.hpp"  ->  "neonraw/debug/demangle.hpp"  (distance: fn=0; dir=12) from "/home/martijn/zec/NeonRAW/include"
    Process /home/martijn/zec/NeonRAW/include/neonraw/jrs/JRS_impl.hpp ...
        👕 Replace include path: "JRS.hpp"  ->  "neonraw/jrs/JRS.hpp"  (distance: fn=0; dir=0) from "/home/martijn/zec/NeonRAW/include"
           - Alternative: neonraw/jrs/JRS.hpp  (distance: fn=0; dir=3) from "/home/martijn/zec/NeonRAW/include"
        👕 Replace include path: "demangle.hpp"  ->  "neonraw/debug/demangle.hpp"  (distance: fn=0; dir=12) from "/home/martijn/zec/NeonRAW/include"
    Process /home/martijn/zec/NeonRAW/include/neonraw/jrs/ResourceTracker.hpp ...
        👕 Replace include path: "JRS.hpp"  ->  "neonraw/jrs/JRS.hpp"  (distance: fn=0; dir=0) from "/home/martijn/zec/NeonRAW/include"
           - Alternative: neonraw/jrs/JRS.hpp  (distance: fn=0; dir=3) from "/home/martijn/zec/NeonRAW/include"
```
