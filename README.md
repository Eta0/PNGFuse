PNGFuse
=======

PNGFuse is a portable, lightweight, and cross-platform application written in C++
that allows you to embed and extract full zlib-compressed files within PNG metadata.

It is designed to support:
- Windows and Unix-based operating systems,
- Unicode filenames,
- High performance, with concurrent compression of multiple input files,
- No unnecessary re-encoding of image data; everything else in an image file is left intact.


PNGFuse uses [LodePNG](https://github.com/lvandeve/lodepng) as its sole dependency
to navigate PNG chunks and compress/decompress input files, and does not link to
[libpng](http://www.libpng.org/pub/png/libpng.html) or [zlib](https://zlib.net/) itself.


# Usage
Basic usage of PNGFuse is as simple as dragging and dropping a set of files including a PNG onto the executable,
or writing their names after `PNGFuse.exe` on the command line.
This will fuse all the other files into the first PNG found&sup1;.

For example, selecting `embed.txt` together with `image.png` and dragging them onto `PNGFuse.exe`
will generate a new file `image.fused.png` containing the contents of `embed.txt` within the metadata of `image.png`.
`image.fused.png` will then render identically to `image.png` in all image viewers.

> **NOTE:** The `.fused.png` extension is not required for the program to recognize a fused PNG, and only exists to distinguish the output.

To extract embedded files from the PNG, simply drag the fused PNG file by itself onto the executable,
or provide it as a single command line argument.
This will produce a copy of each of the files stored within the PNG with their original filenames in the current folder.

For example, dragging `image.fused.png` from our earlier example by itself onto `PNGFuse.exe` will produce a copy of `embed.txt` in the current folder (leaving `image.fused.png` intact).

> **CAUTION:** Extracting files from a fused PNG will overwrite identically-named files in the current folder without warning.

&sup1; The ordering is only important when embedding PNGs into other PNGs.
When dragging and dropping multiple files on Windows,
the file you click to begin dragging becomes the first file listed;
on the command line, it uses the specified argument order.

## Other Options
PNGFuse supports additional functionality via command line options. The command line help text for PNGFuse is copied below:
```
usage: PNGFuse.exe [-h] [--list] [--clean] [--overwrite] [--output <PATH>] fuse-host.png [files to fuse...]

fuse subfiles into PNG metadata.

Specify multiple files to perform a fusion into the first PNG listed,
 or specify a single fused PNG to extract its subfiles (without removing them).

positional arguments:
  fuse-host.png         path to a PNG in which to store subfiles
  files to fuse         one or more files to be fused into fuse-host.png

optional arguments:
  -h, --help            show this help message and exit
  -l, --list            list the subfiles present in a fused PNG
  -c, --clean           remove all subfiles from a fused PNG
  -m, --overwrite       modify the input files when fusing or cleaning instead of creating new ones
  -o, --output <PATH>   custom output path for the result of a fuse or clean operation
```
Options are parsed following [POSIX conventions&sup2;](https://www.gnu.org/software/libc/manual/html_node/Argument-Syntax.html)
and their names are case-insensitive.

> **NOTE:** On Windows, any options may also be pre-set in a shortcut to the executable
by appending the flag after the program path in the *Target* field, e.g.
> ```
> Target: "C:\Path\To\PNGFuse.exe" --clean
> ```
> This would create another icon that you may drag and drop fused PNGs onto to instantly remove any embedded files in them.
See also: `--overwrite` for another interesting flag to add as a preset.

&sup2; Excluding support for no separator after `-o`, like `-oout.png`. A space or `=` character is expected to specify the output path.

### List
Typing `PNGFuse.exe --list fuse-host.png` or `PNGFuse.exe -l fuse-host.png`
will list the files currently fused into `fuse-host.png`.
Multiple fused PNGs may be specified to list the files embedded in each of them.

For example, running `PNGFuse.exe -l image.fused.png` from our earlier example might print:
```
embed.txt : 1024 bytes
```

### Clean
Typing `PNGFuse.exe --clean fuse-host.png` or `PNGFuse.exe -c fuse-host.png`
will remove the files currently fused into `fuse-host.png`,
recovering the original pre-fusion state of the image file.
Multiple fused PNGs may be specified to remove the files embedded in each of them.

For example, running `PNGFuse.exe -c image.fused.png` from our earlier example will print:
```
1 subfile removed.
```
And write our original image as `image.png` to the current directory, containing no embedded subfiles.
If we were, for example, to have renamed `image.fused.png` to `fused-image.png` without a `.fused.png` suffix,
it would instead write the cleaned version to `fused-image.unfused.png`.

`--clean` also has the aliases `--remove` and `-r` not stated in the command line help text.

### Overwrite
Adding `--overwrite` or `-m` (for "modify") to the argument list will cause *fuse* and *clean* operations
to write directly into the file specified as `fuse-host.png`,
instead of generating a version with a `.fused.png` or `.unfused.png` extension.

For example, running `PNGFuse.exe -m image.png embed.txt` will add the contents of `embed.txt`
directly into the metadata of the file `image.png`.
Running `PNGFuse.exe -c -m image.png` would then remove `embed.txt` from `image.png`.

`--overwrite` also has the alias `--modify` not stated in the command line help text.

### Output
Adding `--output PATH` or `-o PATH` to the argument list will cause *fuse* and *clean* operations
to write to the specified custom output file path
instead of generating a version with a `.fused.png` or `.unfused.png` extension.
This option is mutually exclusive with `--overwrite`.

For example, running `PNGFuse.exe image.png embed.txt -o image-with-embed.png`
will cause a new file named `image-with-embed.png` to be written containing the contents of `embed.txt` fused into `image.png`.

The `=` character may be used instead of a space to separate the path name from the `-o` option name.
Not recommended when using Powershell because of lexing peculiarities.

## Editing and Sharing
Embedded files are stored using the private `fuSe` [metadata chunk type](http://www.libpng.org/pub/png/spec/1.2/PNG-Structure.html),
which will be retained by conforming PNG editing software, such as MS Paint.
However, common software such as
[Photoshop](https://www.adobe.com/products/photoshop.html), [GIMP](https://www.gimp.org/), [Paint.NET](https://www.getpaint.net/), and [Photopea](https://www.photopea.com/)
are not conforming PNG editors, and it is recommended to save any embedded files before modifying a fused PNG using any of these programs.

Almost all file sharing websites (e.g. [Catbox.moe](https://catbox.moe/) and [Discord](https://discord.com/) attachments)
and a few image hosts such as [Imgur](https://imgur.com/) will retain `fuSe` chunks, and it is safe to store and share fused PNGs there.
However, many sites that re-encode images they host (such as [Reddit](https://www.reddit.com/))
will strip away any metadata containing the embedded files, effectively deleting them.
It is recommended to test to see if your images are transmitted properly when using any websites not listed here.

## Advanced Usage
To directly manipulate individual `fuSe` chunks stored in an image,
such as to delete or reorder individual chunks without extracting their associated files,
[TweakPNG](http://entropymine.com/jason/tweakpng/) by Jason Summers is an excellent and free GUI application to do so on Windows.
To tell which chunk is which, the ordering of the output of `--list` reflects the current ordering of `fuSe` chunks in the image data. 


# License
PNGFuse is free and open-source software provided under the [zlib license](https://opensource.org/licenses/Zlib).

Its sole dependency, LodePNG, is also free and open-source software provided under the zlib license.
[Find the LodePNG license here.](https://github.com/lvandeve/lodepng/blob/master/LICENSE)

I'd love to be notified if you use PNGFuse in one of your projects!


# Build Information
PNGFuse releases are compiled with language standard C++20, using Visual C++ from Visual Studio version 16.0 on Windows and GCC 10.2 on Linux.
Dependencies are `lodepng.cpp` and `lodepng.h` available on the [LodePNG home page](https://lodev.org/lodepng/)
or on [GitHub](https://github.com/lvandeve/lodepng).

