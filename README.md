# cecup
GUI backup tool inspired by FreeFileSync and Rsync

## Logic
This tool is made to facilitate the correct and intuitive backup of a file
system. A correct backup must be, in general, a 1 to 1 copy from the source.
Everything must be preserved: Directory structure, file data and metatada
and links (both symbolic and hard).

This program, therefore, facilitates this processes by creating two lists
of files: one of all the files in the source, and other of all the files in the
destination. This way, the user can intuitively perform the backup, by first
previewing and confirming:
- which files are missing on the backup and will 
- which files are newer on the source
- which files are on the backup, but (no longer) exist on the source
- which files have different size
- which files are meant to be ignored

## TODO
- Hard links:
  add data structure to the inode map to track all names to each inode.
  then we will be able to correctly know if a hardlink is missing on the
  destination or not. Currently it uses the number of links as a heuristic,
  which will not work if some name is on another directory.
