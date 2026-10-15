# CopyFileProgressBar C++

Copy a file with a progress bar, transfer speed and elapsed time.

```
Copying "E:\videos\movie.mkv" -> "D:\backup\movie.mkv" (4.2 GB)
[################------------------------]  41%  1.7 GB / 4.2 GB  185.3 MB/s
```

## Usage

Pass the paths on the command line:

```
CopyFileProgressBar.exe "E:\videos\movie.mkv" "D:\backup"
```

or run it without arguments and type them in when asked. Type paths
normally (`E:\folder\file.txt`); you don't need to double the backslashes.
Paths with spaces work, and quotes from Explorer's "Copy as path" are removed
for you.

- If the destination is a folder, the file is copied into it with its original name.
- If the destination file exists, you're asked before it is overwritten.
- Copying a file onto itself is refused.
- If the copy fails (for example, the disk is full), the partial file is deleted.
- Output is colored in a terminal. Colors turn off automatically when the
  output is redirected to a file, or when the `NO_COLOR` environment variable is set.

The program exits with `0` on success, `1` if the copy failed or was
cancelled, and `2` for bad arguments.

## Building

Open `CopyFileProgressBar.sln` in Visual Studio 2019 or later. The project
uses C++17 (for `std::filesystem`).

On Linux or macOS, from the repository root:

```
make
./copy-progress ~/videos/movie.mkv ~/backup/
```

`make clean` deletes the built program.
