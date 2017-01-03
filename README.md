# SFincludes
SFincludes (shorthand for Search Fix Includes) is a tool that goes
through all of your source and header files in a given directory and
tries to fix all `#include ""` preprocessor instructions it can find.

How does it fix the includes? It searches first finds all header files
(.h or .hpp) and then fixes every include with one of the found headers.

The selected header is chosen by the file name. By default the filename should
match exactly. Optionally, fuzzy matching can be done, using the Levenshtein
distance (aka the "edit distance"). When fuzzy search is enabled, it will try to
prioritize files in the same directory as the file currently being fixed.

Optionally the `.h` extension can be replaced by a `.hpp` extension for *all*
your header files. Note that this tool will not try to figure out wether or not
a particular file is a C or a C++ header.

## Command line arguments

    --help                print help
    --src         string  set source directory
    --root        string  set root directory
    --fuzzy       int     maximal edit distance (default: 0)
    --rename-hpp          rename headers files to .hpp
    --dry-run             perform a dry-run

The difference between root and src path is:

 - the **root path** is the path to which the include paths will be made
   relative while fixing.
 - the **src path** is the directory in which it will search for headers and
   source files to fix.

Most often, these paths will be the same. Note that you always need to provide
both.

The `--dry-run` option will output what it would do. Note that there is a
summary of the includes it fixed or left untouched. It is adviced to first
perform a dry run and inspect the summary to see if there are problems (i.e.:
the total number of succesfully processed includes does not match the total
number of includes found).

If you use the `--rename-hpp` flag, you will most likely want to set the
`--fuzzy` argument to `2`, as the filenames will have changed with a distance of
`2`, i.e.: "add `p`, add `p`". Leaving the `--fuzzy` argument to zero, will
rename the files, but will fail finding the includes, and leave the contents of
all files probably untouched.
